/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "plugin/JamProcessor.h"
#include "ui/Widgets.h"

#include <memory>
#include <vector>

namespace jamtaba::ui
{

/** BPM or BPI: the server's value, and a proposal the user can vote for. */
class TempoControl final : public yup::Component
{
public:
    TempoControl (yup::String label, int minimum, int maximum);

    void setCurrent (int value);
    std::function<void (int)> onVote;

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    bool mouseWheel (const yup::MouseEvent& event, const yup::MouseWheelData& wheel) override;

private:
    yup::Rectangle<float> upArea() const;
    yup::Rectangle<float> downArea() const;
    yup::Rectangle<float> voteArea() const;
    void nudge (int delta);

    yup::String label;
    int minimum, maximum;
    int current = 0, proposed = 0;
};

/** The interval as a row of beats, with the beat in progress lit, and the
    chord progression from chat above it when there is one. */
class BeatBar final : public yup::Component
{
public:
    explicit BeatBar (JamProcessor& processor) : processor (processor) {}

    void paint (yup::Graphics& g) override;
    void refreshDisplay (double lastFrameTimeSeconds) override;

private:
    JamProcessor& processor;
    int lastBeat = -1, lastBpi = 0;
    float lastFraction = -1.0f;
};

/** One strip: name, toggles, fader with meter, pan. Local and remote strips
    share it, with different toggles. */
class ChannelStrip final : public yup::Component
{
public:
    enum class Kind
    {
        local,
        remote,
    };

    ChannelStrip (JamProcessor& processor, Kind kind);

    /** For remote strips: the user and channel this strip controls. */
    void setRemote (const SessionModel::User& user, const SessionModel::Channel& channel);
    const std::string& getUserName() const noexcept { return userName; }
    int getChannelIndex() const noexcept { return channelIndex; }

    void update (double frameSeconds);

    void resized() override;
    void paint (yup::Graphics& g) override;

    static constexpr float localWidth = 136.0f;
    static constexpr float remoteWidth = 96.0f;

private:
    audio::StripControls* controls();
    audio::Meter* meter();
    void showInputMenu();
    void controlsChanged();

    JamProcessor& processor;
    Kind kind;
    std::string userName;
    int channelIndex = 0;
    int slot = -1;
    yup::String title, subtitle;
    yup::Color color = theme::accent;
    double lastIntervalTime = 0.0;
    bool receiving = true;

    Fader fader;
    LevelMeter levelMeter;
    PanSlider pan;
    ToggleChip muteChip { "M", theme::mute };
    ToggleChip soloChip { "S", theme::solo };
    ToggleChip transmitChip { "XMIT", theme::transmit };
    ToggleChip monitorChip { "MON", theme::accent };
    ToggleChip receiveChip { "RX", theme::receive };
    FlatButton inputButton { "In 1+2" };
    float playFraction = -1.0f;
};

/** The other musicians: one group of strips per user, scrolling sideways. */
class RemoteArea final : public yup::Component
{
public:
    explicit RemoteArea (JamProcessor& processor) : processor (processor) {}

    void rebuild();
    void update (double frameSeconds);

    void resized() override;
    void paint (yup::Graphics& g) override;
    bool mouseWheel (const yup::MouseEvent& event, const yup::MouseWheelData& wheel) override;

private:
    struct Group
    {
        yup::String name;
        yup::Color color;
        float x = 0.0f, width = 0.0f;
    };

    float contentWidth() const;

    JamProcessor& processor;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    std::vector<Group> groups;
    float scroll = 0.0f;
};

/** Chat log and input, collapsible. */
class ChatPanel final : public yup::Component
{
public:
    explicit ChatPanel (JamProcessor& processor);

    void update();

    void resized() override;
    void paint (yup::Graphics& g) override;
    bool mouseWheel (const yup::MouseEvent& event, const yup::MouseWheelData& wheel) override;

private:
    void rebuildLog();
    void send();

    JamProcessor& processor;
    LineEdit input { "Say something, or !vote bpm 120" };
    yup::StyledText log;
    float logHeight = 0.0f;
    float logWidth = 0.0f;
    float scrollUp = 0.0f; // pixels above the newest line
    std::uint32_t seenVersion = 0xffffffff;
};

/** The server's licence, shown before joining a server that has one. */
class LicencePrompt final : public yup::Component
{
public:
    explicit LicencePrompt (JamProcessor& processor);

    void resized() override;
    void paint (yup::Graphics& g) override;

private:
    yup::Rectangle<float> panelBounds() const;

    JamProcessor& processor;
    FlatButton acceptButton { "Accept and join", true };
    FlatButton declineButton { "Decline" };
};

/** Inside a server: transport on top, local strip on the left, the others
    in the middle, chat on the right. */
class JamRoom final : public yup::Component
{
public:
    explicit JamRoom (JamProcessor& processor);

    void layoutChanged();

    void resized() override;
    void paint (yup::Graphics& g) override;
    void refreshDisplay (double lastFrameTimeSeconds) override;

private:
    JamProcessor& processor;

    TempoControl bpmControl { "BPM", 40, 400 };
    TempoControl bpiControl { "BPI", 2, 64 };
    BeatBar beatBar;
    Fader metronomeFader { false };
    ToggleChip metronomeMute { "M", theme::mute };
    Fader masterFader { false };
    LevelMeter masterMeter { false };
    FlatButton chatButton { "Chat" };
    FlatButton leaveButton { "Leave" };

    ChannelStrip localStrip;
    RemoteArea remoteArea;
    ChatPanel chat;
    LicencePrompt licence;

    yup::Rectangle<float> nameArea, metronomeLabel, masterLabel;
    bool hadChords = false;
};

} // namespace jamtaba::ui
