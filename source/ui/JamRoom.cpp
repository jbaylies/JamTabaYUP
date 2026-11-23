/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "ui/JamRoom.h"

#include <algorithm>
#include <cmath>

namespace jamtaba::ui
{

namespace
{
double nowMs()
{
    return yup::Time::getMillisecondCounterHiRes();
}

constexpr float chatWidth = 300.0f;
constexpr float groupHeaderHeight = 26.0f;
} // namespace

//==============================================================================
TempoControl::TempoControl (yup::String labelText, int minimumValue, int maximumValue)
    : yup::Component ("TempoControl")
    , label (std::move (labelText))
    , minimum (minimumValue)
    , maximum (maximumValue)
{
}

void TempoControl::setCurrent (int value)
{
    if (value == current)
        return;
    if (proposed == current || proposed == 0)
        proposed = value;
    current = value;
    repaint();
}

yup::Rectangle<float> TempoControl::voteArea() const { return getLocalBounds().removeFromRight (58.0f).reduced (2.0f, 6.0f); }
yup::Rectangle<float> TempoControl::upArea() const { return getLocalBounds().withTrimmedRight (58.0f).removeFromRight (22.0f).removeFromTop (getHeight() * 0.5f); }
yup::Rectangle<float> TempoControl::downArea() const { return getLocalBounds().withTrimmedRight (58.0f).removeFromRight (22.0f).removeFromBottom (getHeight() * 0.5f); }

void TempoControl::paint (yup::Graphics& g)
{
    auto area = getLocalBounds();
    g.setFillColor (theme::panelRaised);
    g.fillRoundedRect (area.reduced (0.5f), 6.0f);

    auto text = area.withTrimmedRight (80.0f).reduced (10.0f, 4.0f);
    drawText (g, label, theme::font (10.0f), text.removeFromTop (12.0f), theme::faint);
    const bool changed = proposed != current && current > 0;
    drawText (g, current > 0 ? yup::String (changed ? proposed : current) : yup::String ("--"), theme::monoFont (20.0f), text,
              changed ? theme::accent : theme::text);

    const auto arrow = [&] (yup::Rectangle<float> r, bool up)
    {
        yup::Path path;
        const auto c = r.getCenter();
        const float s = 4.0f;
        path.startNewSubPath (c.getX() - s, c.getY() + (up ? s * 0.5f : -s * 0.5f));
        path.lineTo (c.getX() + s, c.getY() + (up ? s * 0.5f : -s * 0.5f));
        path.lineTo (c.getX(), c.getY() + (up ? -s * 0.5f : s * 0.5f));
        path.closeSubPath();
        g.setFillColor (theme::dim);
        g.fillPath (path);
    };
    arrow (upArea(), true);
    arrow (downArea(), false);

    const auto vote = voteArea();
    g.setFillColor (changed ? theme::accent : theme::panel);
    g.fillRoundedRect (vote, 5.0f);
    drawText (g, "Vote", theme::font (12.0f), vote, changed ? theme::background : theme::faint, yup::Justification::center);
}

void TempoControl::nudge (int delta)
{
    if (current <= 0)
        return;
    proposed = std::clamp (proposed + delta, minimum, maximum);
    repaint();
}

void TempoControl::mouseDown (const yup::MouseEvent& event)
{
    const auto p = event.getPosition();
    const int step = event.getModifiers().isShiftDown() ? 10 : 1;
    if (upArea().contains (p))
        nudge (step);
    else if (downArea().contains (p))
        nudge (-step);
    else if (voteArea().contains (p) && proposed != current && current > 0 && onVote)
        onVote (proposed);
}

bool TempoControl::mouseWheel (const yup::MouseEvent&, const yup::MouseWheelData& wheel)
{
    nudge (wheel.getDeltaY() > 0.0f ? 1 : -1);
    return true;
}

//==============================================================================
void BeatBar::tick()
{
    const auto& audio = processor.getEngine().getAudio();
    const int spi = audio.getSamplesPerInterval();
    const float fraction = audio.isClockRunning() && spi > 0 ? static_cast<float> (audio.getIntervalPosition()) / static_cast<float> (spi) : -1.0f;
    if (audio.getCurrentBeat() != lastBeat || audio.getBpi() != lastBpi || std::abs (fraction - lastFraction) > 0.004f)
    {
        lastBeat = audio.getCurrentBeat();
        lastBpi = audio.getBpi();
        lastFraction = fraction;
        repaint();
    }
}

void BeatBar::paint (yup::Graphics& g)
{
    auto area = getLocalBounds();
    const auto& audio = processor.getEngine().getAudio();
    const auto& model = processor.uiModel();

    if (! audio.isClockRunning() || audio.getBpi() <= 0)
    {
        g.setFillColor (theme::panel);
        g.fillRoundedRect (area, 6.0f);
        const auto message = model.connection == SessionModel::Connection::connected ? "Waiting for the server's tempo..." : "Connecting...";
        drawText (g, message, theme::font (13.0f), area, theme::dim, yup::Justification::center);
        return;
    }

    const int bpi = audio.getBpi();
    const int beat = std::clamp (audio.getCurrentBeat(), 0, bpi - 1);
    const double spb = audio.getSampleRate() * 60.0 / std::max (1, audio.getBpm());
    const float beatFraction = static_cast<float> (std::clamp (audio.getIntervalPosition() / spb - beat, 0.0, 1.0));

    const float gap = bpi > 32 ? 1.0f : 3.0f;
    const auto segmentX = [&] (int index) { return area.getX() + (area.getWidth() + gap) * static_cast<float> (index) / static_cast<float> (bpi); };

    if (! model.chords.isEmpty())
    {
        const auto chordRow = area.removeFromTop (20.0f);
        area.removeFromTop (3.0f);
        const auto& measures = model.chords.measures;
        const int count = static_cast<int> (measures.size());
        for (int m = 0; m < count; ++m)
        {
            const int firstBeat = m * bpi / count;
            const int endBeat = (m + 1) * bpi / count;
            const auto cell = yup::Rectangle<float> (segmentX (firstBeat), chordRow.getY(), segmentX (endBeat) - segmentX (firstBeat) - gap, chordRow.getHeight());
            const bool currentMeasure = beat >= firstBeat && beat < endBeat;
            g.setFillColor (currentMeasure ? theme::panelRaised.brighter (0.1f) : theme::panel);
            g.fillRoundedRect (cell, 4.0f);

            yup::String chords;
            for (const auto& chord : measures[static_cast<size_t> (m)])
                chords << (chords.isEmpty() ? "" : "  ") << theme::str (chord);
            drawText (g, chords, theme::font (13.0f), cell.reduced (6.0f, 0.0f), currentMeasure ? theme::text : theme::dim);
        }
    }

    for (int i = 0; i < bpi; ++i)
    {
        const auto segment = yup::Rectangle<float> (segmentX (i), area.getY(), segmentX (i + 1) - segmentX (i) - gap, area.getHeight());
        g.setFillColor (i < beat ? theme::accent.withAlpha (0.35f) : theme::track);
        g.fillRoundedRect (segment, 4.0f);

        if (i == beat)
        {
            g.setFillColor (theme::accent);
            g.fillRoundedRect (segment.withWidth (std::max (4.0f, segment.getWidth() * beatFraction)), 4.0f);
        }

        if (segment.getWidth() >= 16.0f)
            drawText (g, yup::String (i + 1), theme::monoFont (std::min (13.0f, segment.getHeight() * 0.45f)), segment,
                      i == beat ? theme::background : (i == 0 ? theme::text : theme::dim), yup::Justification::center);
    }
}

//==============================================================================
ChannelStrip::ChannelStrip (JamProcessor& p, Kind stripKind)
    : yup::Component ("ChannelStrip")
    , processor (p)
    , kind (stripKind)
{
    addAndMakeVisible (fader);
    addAndMakeVisible (levelMeter);
    addAndMakeVisible (pan);
    addAndMakeVisible (muteChip);
    addAndMakeVisible (soloChip);

    const auto changed = [this] { controlsChanged(); };
    fader.onChange = [changed] (float) { changed(); };
    pan.onChange = [changed] (float) { changed(); };
    muteChip.onToggle = [changed] (bool) { changed(); };
    soloChip.onToggle = [changed] (bool) { changed(); };

    if (kind == Kind::local)
    {
        auto& settings = processor.getSettings();
        title = "You";
        subtitle = theme::str (settings.channelName);
        addAndMakeVisible (transmitChip);
        addAndMakeVisible (monitorChip);
        transmitChip.setOn (settings.transmit);
        monitorChip.setOn (settings.monitor);
        fader.setGain (settings.localGain);
        pan.setPan (settings.localPan);
        muteChip.setOn (settings.localMute);
        transmitChip.onToggle = [changed] (bool) { changed(); };
        monitorChip.onToggle = [changed] (bool) { changed(); };

        if (processor.isStandalone())
        {
            addAndMakeVisible (inputButton);
            inputButton.onClick = [this] { showInputMenu(); };
            const char* names[] = { "In 1+2", "In 1", "In 2" };
            inputButton.setText (names[static_cast<int> (settings.inputMode)]);
        }
    }
    else
    {
        addAndMakeVisible (receiveChip);
        receiveChip.setOn (true);
        receiveChip.onToggle = [this] (bool on)
        {
            receiving = on;
            processor.setReceiving (userName, channelIndex, on);
        };
    }
}

void ChannelStrip::setRemote (const SessionModel::User& user, const SessionModel::Channel& channel)
{
    userName = user.fullName;
    channelIndex = channel.index;
    slot = channel.slot;
    title = channel.name.empty() ? yup::String ("channel ") + yup::String (channel.index + 1) : theme::str (channel.name);
    color = theme::userColor (user.colorIndex);
    receiving = channel.receiving;
    receiveChip.setOn (receiving);
    fader.setAccent (color);

    if (auto* c = controls())
    {
        fader.setGain (c->gain.load());
        pan.setPan (c->pan.load());
        muteChip.setOn (c->mute.load());
        soloChip.setOn (c->solo.load());
    }
    repaint();
}

audio::StripControls* ChannelStrip::controls()
{
    auto& audio = processor.getEngine().getAudio();
    if (kind == Kind::local)
        return &audio.getLocalChannel (0).controls;
    return slot >= 0 ? &audio.getSlot (slot).controls : nullptr;
}

audio::Meter* ChannelStrip::meter()
{
    auto& audio = processor.getEngine().getAudio();
    if (kind == Kind::local)
        return &audio.getLocalChannel (0).meter;
    return slot >= 0 ? &audio.getSlot (slot).meter : nullptr;
}

void ChannelStrip::controlsChanged()
{
    if (kind == Kind::local)
    {
        auto& settings = processor.getSettings();
        settings.localGain = fader.getGain();
        settings.localPan = pan.getPan();
        settings.localMute = muteChip.isOn();
        settings.transmit = transmitChip.isOn();
        settings.monitor = monitorChip.isOn();
        processor.applySettings();
        processor.getEngine().getAudio().getLocalChannel (0).controls.solo.store (soloChip.isOn());
        return;
    }

    if (auto* c = controls())
    {
        c->gain.store (fader.getGain());
        c->pan.store (pan.getPan());
        c->mute.store (muteChip.isOn());
        c->solo.store (soloChip.isOn());
        processor.rememberRemoteMix (userName, channelIndex);
    }
}

void ChannelStrip::showInputMenu()
{
    auto menu = yup::PopupMenu::create (yup::PopupMenu::Options().withRelativePosition (&inputButton));
    const auto mode = static_cast<int> (processor.getSettings().inputMode);
    menu->addItem ("Inputs 1 + 2 (stereo)", 1, true, mode == 0);
    menu->addItem ("Input 1 (mono)", 2, true, mode == 1);
    menu->addItem ("Input 2 (mono)", 3, true, mode == 2);
    menu->show ([this] (int id)
    {
        if (id <= 0)
            return;
        processor.getSettings().inputMode = static_cast<JamSettings::InputMode> (id - 1);
        processor.applySettings();
        runUiLocked (*this, processor.getUiLock(), [this, id]
        {
            const char* names[] = { "In 1+2", "In 1", "In 2" };
            inputButton.setText (names[id - 1]);
        });
    });
}

void ChannelStrip::update (double frameSeconds)
{
    if (auto* m = meter())
        levelMeter.update (*m, frameSeconds);

    if (kind == Kind::remote)
    {
        if (const auto* channel = processor.uiModel().findChannel (userName, channelIndex))
        {
            if (channel->lastIntervalTime != lastIntervalTime)
            {
                lastIntervalTime = channel->lastIntervalTime;
                repaint();
            }
        }
        if (nowMs() - lastIntervalTime < 700.0)
            repaint (getLocalBounds().removeFromBottom (12.0f));
    }
    else
    {
        const auto& audio = processor.getEngine().getAudio();
        const float fraction = audio.isClockRunning() && audio.getSamplesPerInterval() > 0
                                 ? static_cast<float> (audio.getIntervalPosition()) / static_cast<float> (audio.getSamplesPerInterval())
                                 : -1.0f;
        if (std::abs (fraction - playFraction) > 0.01f)
        {
            playFraction = fraction;
            repaint (getLocalBounds().removeFromBottom (12.0f));
        }
    }
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (8.0f, 6.0f);
    area.removeFromTop (kind == Kind::local ? 36.0f : 22.0f); // titles
    area.removeFromBottom (10.0f);                            // status

    if (kind == Kind::local)
    {
        if (processor.isStandalone())
        {
            inputButton.setBounds (area.removeFromTop (26.0f));
            area.removeFromTop (4.0f);
        }
        auto row = area.removeFromTop (22.0f);
        transmitChip.setBounds (row.removeFromLeft (row.getWidth() * 0.5f).reduced (1.0f, 0.0f));
        monitorChip.setBounds (row.reduced (1.0f, 0.0f));
    }
    else
    {
        receiveChip.setBounds (area.removeFromTop (22.0f).reduced (1.0f, 0.0f));
    }

    area.removeFromTop (4.0f);
    auto row = area.removeFromTop (22.0f);
    muteChip.setBounds (row.removeFromLeft (row.getWidth() * 0.5f).reduced (1.0f, 0.0f));
    soloChip.setBounds (row.reduced (1.0f, 0.0f));

    pan.setBounds (area.removeFromBottom (20.0f));
    area.removeFromBottom (4.0f);
    area.removeFromTop (6.0f);
    levelMeter.setBounds (area.removeFromRight (std::min (18.0f, area.getWidth() * 0.3f)));
    area.removeFromRight (4.0f);
    fader.setBounds (area);
}

void ChannelStrip::paint (yup::Graphics& g)
{
    auto area = getLocalBounds();
    g.setFillColor (theme::panel);
    g.fillRoundedRect (area.reduced (2.0f, 0.0f), 6.0f);

    auto titles = area.reduced (10.0f, 6.0f).removeFromTop (kind == Kind::local ? 36.0f : 22.0f);
    if (kind == Kind::local)
    {
        drawText (g, title, theme::font (14.0f), titles.removeFromTop (18.0f), theme::text);
        drawText (g, subtitle, theme::font (11.0f), titles, theme::dim);
    }
    else
    {
        drawText (g, title, theme::font (12.0f), titles, receiving ? theme::text : theme::faint);
    }

    // Status line: the local interval's progress, or a flash when a remote interval arrives
    auto status = area.reduced (10.0f, 6.0f).removeFromBottom (5.0f);
    g.setFillColor (theme::track);
    g.fillRoundedRect (status, 2.0f);
    if (kind == Kind::local)
    {
        if (playFraction >= 0.0f)
        {
            g.setFillColor (transmitChip.isOn() ? theme::transmit : theme::faint);
            g.fillRoundedRect (status.withWidth (status.getWidth() * playFraction), 2.0f);
        }
    }
    else
    {
        const double age = nowMs() - lastIntervalTime;
        if (lastIntervalTime > 0.0 && age < 700.0)
        {
            g.setFillColor (theme::receive.withAlpha (static_cast<float> (1.0 - age / 700.0)));
            g.fillRoundedRect (status, 2.0f);
        }
    }
}

//==============================================================================
void RemoteArea::rebuild()
{
    strips.clear();
    groups.clear();

    float x = 0.0f;
    for (const auto& user : processor.uiModel().users)
    {
        if (user.channels.empty())
            continue;

        Group group { theme::str (user.displayName), theme::userColor (user.colorIndex), x, 0.0f };
        for (const auto& channel : user.channels)
        {
            auto strip = std::make_unique<ChannelStrip> (processor, ChannelStrip::Kind::remote);
            strip->setRemote (user, channel);
            addAndMakeVisible (*strip);
            strips.push_back (std::move (strip));
            group.width += ChannelStrip::remoteWidth;
        }
        x += group.width + 12.0f;
        groups.push_back (group);
    }

    scroll = std::clamp (scroll, 0.0f, std::max (0.0f, contentWidth() - getWidth()));
    resized();
    repaint();
}

float RemoteArea::contentWidth() const
{
    return groups.empty() ? 0.0f : groups.back().x + groups.back().width;
}

void RemoteArea::update (double frameSeconds)
{
    for (auto& strip : strips)
        strip->update (frameSeconds);
}

void RemoteArea::resized()
{
    size_t index = 0;
    for (const auto& group : groups)
    {
        float x = group.x - scroll;
        while (index < strips.size() && x < group.x - scroll + group.width - 1.0f)
        {
            strips[index++]->setBounds (x, groupHeaderHeight, ChannelStrip::remoteWidth, getHeight() - groupHeaderHeight);
            x += ChannelStrip::remoteWidth;
        }
    }
}

void RemoteArea::paint (yup::Graphics& g)
{
    if (groups.empty())
    {
        drawText (g, "Nobody else is here yet. Others show up here when they join.", theme::font (14.0f),
                  getLocalBounds(), theme::dim, yup::Justification::center);
        return;
    }

    for (const auto& group : groups)
    {
        const auto header = yup::Rectangle<float> (group.x - scroll + 2.0f, 0.0f, group.width - 4.0f, groupHeaderHeight - 4.0f);
        g.setFillColor (group.color.withAlpha (0.18f));
        g.fillRoundedRect (header, 5.0f);
        g.setFillColor (group.color);
        g.fillRoundedRect (header.withWidth (4.0f), 2.0f);
        drawText (g, group.name, theme::font (13.0f), header.reduced (10.0f, 0.0f), theme::text);
    }
}

bool RemoteArea::mouseWheel (const yup::MouseEvent&, const yup::MouseWheelData& wheel)
{
    const float delta = wheel.getDeltaX() != 0.0f ? wheel.getDeltaX() : wheel.getDeltaY();
    const float newScroll = std::clamp (scroll - delta * 40.0f, 0.0f, std::max (0.0f, contentWidth() - getWidth()));
    if (newScroll != scroll)
    {
        scroll = newScroll;
        resized();
        repaint();
    }
    return true;
}

//==============================================================================
ChatPanel::ChatPanel (JamProcessor& p)
    : yup::Component ("ChatPanel")
    , processor (p)
{
    addAndMakeVisible (input);
    input.onReturn = [this] { send(); };
}

void ChatPanel::send()
{
    const auto text = input.getText().trim();
    if (text.isEmpty())
        return;
    processor.getEngine().sendChat (text.toStdString());
    input.setText ({}, yup::dontSendNotification);
}

void ChatPanel::rebuildLog()
{
    const auto& chat = processor.uiModel().chat;
    const float width = std::max (40.0f, getWidth() - 24.0f);

    auto modifier = log.startUpdate();
    modifier.clear();
    modifier.setMaxSize ({ width, 1.0e6f });
    modifier.setWrap (yup::StyledText::wrap);

    const auto start = chat.size() > 200 ? chat.size() - 200 : 0;
    for (size_t i = start; i < chat.size(); ++i)
    {
        const auto& line = chat[i];
        using Kind = SessionModel::ChatLine::Kind;
        const yup::String newline = i + 1 < chat.size() ? "\n" : "";
        switch (line.kind)
        {
            case Kind::message:
            case Kind::privateMessage:
                modifier.appendText (theme::str (line.user) + (line.kind == Kind::privateMessage ? " (private)" : "") + "  ",
                                     theme::accent, theme::font (13.0f));
                modifier.appendText (theme::str (line.text) + newline, theme::text, theme::font (13.0f));
                break;
            case Kind::server:
                modifier.appendText (theme::str (line.text) + newline, theme::mute, theme::font (12.5f));
                break;
            case Kind::notice:
                modifier.appendText (theme::str (line.text) + newline, theme::faint, theme::font (12.5f));
                break;
        }
    }
}

void ChatPanel::update()
{
    const auto version = processor.uiModel().chatVersion;
    if (version == seenVersion && std::abs (getWidth() - 24.0f - logWidth) < 0.5f)
        return;

    seenVersion = version;
    logWidth = getWidth() - 24.0f;
    rebuildLog();
    logHeight = log.getComputedTextBounds().getHeight();
    scrollUp = 0.0f;
    repaint();
}

void ChatPanel::resized()
{
    input.setBounds (getLocalBounds().reduced (10.0f).removeFromBottom (34.0f));
}

void ChatPanel::paint (yup::Graphics& g)
{
    g.setFillColor (theme::panel);
    g.fillRoundedRect (getLocalBounds(), 8.0f);

    auto area = getLocalBounds().reduced (12.0f, 10.0f);
    drawText (g, "Chat", theme::font (14.0f), area.removeFromTop (20.0f), theme::text);
    area.removeFromBottom (44.0f);
    area.removeFromTop (6.0f);

    g.setClipPath (area);
    const float y = area.getBottom() - logHeight + scrollUp;
    g.fillFittedText (log, { area.getX(), y, area.getWidth(), logHeight });
}

bool ChatPanel::mouseWheel (const yup::MouseEvent&, const yup::MouseWheelData& wheel)
{
    const float visible = getHeight() - 90.0f;
    scrollUp = std::clamp (scrollUp + wheel.getDeltaY() * 40.0f, 0.0f, std::max (0.0f, logHeight - visible));
    repaint();
    return true;
}

//==============================================================================
LicencePrompt::LicencePrompt (JamProcessor& p)
    : yup::Component ("LicencePrompt")
    , processor (p)
{
    addAndMakeVisible (acceptButton);
    addAndMakeVisible (declineButton);
    acceptButton.onClick = [this] { processor.getEngine().acceptLicence (true); };
    declineButton.onClick = [this] { processor.getEngine().acceptLicence (false); };
}

yup::Rectangle<float> LicencePrompt::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCenter (std::min (620.0f, getWidth() - 40.0f), std::min (520.0f, getHeight() - 40.0f));
}

void LicencePrompt::resized()
{
    auto buttons = panelBounds().reduced (20.0f).removeFromBottom (38.0f);
    acceptButton.setBounds (buttons.removeFromRight (160.0f));
    buttons.removeFromRight (10.0f);
    declineButton.setBounds (buttons.removeFromRight (110.0f));
}

void LicencePrompt::paint (yup::Graphics& g)
{
    g.setFillColor (yup::Color (0xc0000000));
    g.fillAll();

    const auto panel = panelBounds();
    g.setFillColor (theme::panelRaised);
    g.fillRoundedRect (panel, 10.0f);

    auto area = panel.reduced (20.0f);
    drawText (g, "This server asks you to accept its licence", theme::font (17.0f), area.removeFromTop (26.0f), theme::text);
    area.removeFromBottom (50.0f);
    area.removeFromTop (8.0f);

    yup::StyledText body;
    {
        auto modifier = body.startUpdate();
        modifier.setMaxSize ({ area.getWidth(), area.getHeight() });
        modifier.setWrap (yup::StyledText::wrap);
        modifier.appendText (theme::str (processor.uiModel().licenceText), theme::dim, theme::font (13.0f));
    }
    g.setClipPath (area);
    g.fillFittedText (body, area);
}

//==============================================================================
JamRoom::JamRoom (JamProcessor& p)
    : yup::Component ("JamRoom")
    , processor (p)
    , beatBar (p)
    , localStrip (p, ChannelStrip::Kind::local)
    , remoteArea (p)
    , chat (p)
    , licence (p)
{
    setOpaque (true);

    for (auto* c : std::initializer_list<yup::Component*> { &bpmControl, &bpiControl, &beatBar, &metronomeFader, &metronomeMute,
                                                           &masterFader, &masterMeter, &chatButton, &leaveButton,
                                                           &localStrip, &remoteArea, &chat })
        addAndMakeVisible (c);
    addChildComponent (licence);

    auto& settings = processor.getSettings();
    metronomeFader.setGain (settings.metronomeGain);
    metronomeMute.setOn (settings.metronomeMute);
    masterFader.setGain (settings.masterGain);
    chat.setVisible (settings.chatVisible);

    bpmControl.onVote = [this] (int bpm) { processor.getEngine().voteBpm (bpm); };
    bpiControl.onVote = [this] (int bpi) { processor.getEngine().voteBpi (bpi); };
    metronomeFader.onChange = [this] (float gain)
    {
        processor.getSettings().metronomeGain = gain;
        processor.applySettings();
    };
    metronomeMute.onToggle = [this] (bool on)
    {
        processor.getSettings().metronomeMute = on;
        processor.applySettings();
    };
    masterFader.onChange = [this] (float gain)
    {
        processor.getSettings().masterGain = gain;
        processor.applySettings();
    };
    chatButton.onClick = [this]
    {
        runUiLocked (*this, processor.getUiLock(), [this]
        {
            auto& s = processor.getSettings();
            s.chatVisible = ! s.chatVisible;
            chat.setVisible (s.chatVisible);
            resized();
        });
    };
    leaveButton.onClick = [this] { processor.disconnect(); };

    layoutChanged();
}

void JamRoom::layoutChanged()
{
    remoteArea.rebuild();
}

void JamRoom::tick (double frameSeconds)
{
    const auto& audio = processor.getEngine().getAudio();
    bpmControl.setCurrent (audio.isClockRunning() ? audio.getBpm() : processor.uiModel().bpm);
    bpiControl.setCurrent (audio.isClockRunning() ? audio.getBpi() : processor.uiModel().bpi);

    beatBar.tick();
    localStrip.update (frameSeconds);
    remoteArea.update (frameSeconds);
    masterMeter.update (processor.getEngine().getAudio().getMasterMeter(), frameSeconds);
    if (chat.isVisible())
        chat.update();
}

void JamRoom::syncStructure()
{
    if (processor.uiModel().chords.isEmpty() == hadChords)
        resized(); // the chord row appeared or went away

    const bool showLicence = processor.uiModel().connection == SessionModel::Connection::licence;
    if (licence.isVisible() != showLicence)
    {
        licence.setVisible (showLicence);
        licence.setBounds (getLocalBounds());
    }
}

void JamRoom::resized()
{
    auto area = getLocalBounds().reduced (12.0f);

    auto top = area.removeFromTop (44.0f);
    leaveButton.setBounds (top.removeFromRight (80.0f).reduced (0.0f, 4.0f));
    top.removeFromRight (8.0f);
    chatButton.setBounds (top.removeFromRight (70.0f).reduced (0.0f, 4.0f));
    top.removeFromRight (16.0f);
    auto master = top.removeFromRight (190.0f);
    masterLabel = master.removeFromLeft (52.0f);
    masterMeter.setBounds (master.removeFromBottom (10.0f).reduced (8.0f, 0.0f));
    masterFader.setBounds (master);
    top.removeFromRight (12.0f);
    auto metronome = top.removeFromRight (200.0f);
    metronomeLabel = metronome.removeFromLeft (70.0f);
    metronomeMute.setBounds (metronome.removeFromRight (26.0f).withSizeKeepingCenter (26.0f, 22.0f));
    metronomeFader.setBounds (metronome);

    nameArea = top.removeFromLeft (std::max (120.0f, top.getWidth() - 316.0f)); // server name, painted
    bpmControl.setBounds (top.removeFromLeft (150.0f));
    top.removeFromLeft (8.0f);
    bpiControl.setBounds (top.removeFromLeft (150.0f));

    area.removeFromTop (10.0f);
    const bool chords = ! processor.uiModel().chords.isEmpty();
    hadChords = chords;
    beatBar.setBounds (area.removeFromTop (chords ? 60.0f : 36.0f));
    area.removeFromTop (12.0f);

    localStrip.setBounds (area.removeFromLeft (ChannelStrip::localWidth));
    area.removeFromLeft (12.0f);
    if (chat.isVisible())
    {
        chat.setBounds (area.removeFromRight (std::min (chatWidth, area.getWidth() * 0.4f)));
        area.removeFromRight (12.0f);
    }
    remoteArea.setBounds (area);
    licence.setBounds (getLocalBounds());
}

void JamRoom::paint (yup::Graphics& g)
{
    g.setFillColor (theme::background);
    g.fillAll();

    const auto& model = processor.uiModel();
    auto name = nameArea;
    drawText (g, theme::str (model.serverAddress), theme::font (15.0f), name.removeFromTop (24.0f), theme::text);
    yup::String info;
    if (model.maxUsers > 0)
        info << model.userCount << " / " << model.maxUsers << " users";
    if (! model.topic.empty())
        info << (info.isEmpty() ? "" : "   ") << theme::str (model.topic);
    drawText (g, info, theme::font (11.5f), name, theme::dim);

    drawText (g, "Master", theme::font (11.0f), masterLabel, theme::dim);
    drawText (g, "Metronome", theme::font (11.0f), metronomeLabel, theme::dim);
}

} // namespace jamtaba::ui
