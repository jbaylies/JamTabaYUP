/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

namespace jamtaba::ui
{

void drawText (yup::Graphics& g, const yup::String& text, const yup::Font& font,
               yup::Rectangle<float> area, yup::Color color, yup::Justification justification)
{
    if (text.isEmpty() || area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
        return;
    g.setFillColor (color);
    g.fillFittedText (text, font, area, justification);
}

//==============================================================================
namespace
{
constexpr float minDb = -60.0f;
constexpr float maxFaderDb = 6.0f;

float gainToDb (float gain) noexcept
{
    return gain <= 0.0f ? -100.0f : 20.0f * std::log10 (gain);
}
} // namespace

float Fader::gainToPosition (float g) noexcept
{
    // Mostly linear in dB, with the bottom stretch going to silence
    const float db = gainToDb (g);
    if (db <= minDb)
        return 0.0f;
    const float u = (db - minDb) / (maxFaderDb - minDb);
    return std::clamp (std::pow (u, 1.6f), 0.0f, 1.0f);
}

float Fader::positionToGain (float position) noexcept
{
    if (position <= 0.001f)
        return 0.0f;
    const float u = std::pow (std::clamp (position, 0.0f, 1.0f), 1.0f / 1.6f);
    return std::pow (10.0f, (minDb + u * (maxFaderDb - minDb)) / 20.0f);
}

Fader::Fader (bool isVertical)
    : yup::Component ("Fader")
    , vertical (isVertical)
{
}

void Fader::setGain (float newGain)
{
    newGain = std::clamp (newGain, 0.0f, 2.0f);
    if (newGain != gain)
    {
        gain = newGain;
        repaint();
    }
}

yup::Rectangle<float> Fader::trackBounds() const
{
    auto bounds = getLocalBounds();
    return vertical ? bounds.reduced (0.0f, 8.0f) : bounds.reduced (8.0f, 0.0f);
}

void Fader::setFromPosition (float position)
{
    const float newGain = positionToGain (position);
    if (std::abs (newGain - gain) > 1.0e-6f)
    {
        gain = newGain;
        repaint();
        if (onChange)
            onChange (gain);
    }
}

void Fader::paint (yup::Graphics& g)
{
    const auto area = trackBounds();
    const float position = gainToPosition (gain);
    const float unity = gainToPosition (1.0f);

    if (vertical)
    {
        const auto groove = area.withSizeKeepingCenter (4.0f, area.getHeight());
        g.setFillColor (theme::track);
        g.fillRoundedRect (groove, 2.0f);

        const float y = area.getBottom() - position * area.getHeight();
        g.setFillColor (accentColor.withAlpha (0.8f));
        g.fillRoundedRect (groove.withTrimmedTop (y - groove.getY()), 2.0f);

        const float unityY = area.getBottom() - unity * area.getHeight();
        g.setFillColor (theme::faint);
        g.fillRect (area.getX(), std::round (unityY), area.getWidth(), 1.0f);

        const auto thumb = yup::Rectangle<float> (area.getX(), y - 6.0f, area.getWidth(), 12.0f);
        g.setFillColor (theme::panelRaised.brighter (0.25f));
        g.fillRoundedRect (thumb, 3.0f);
        g.setFillColor (theme::text);
        g.fillRect (thumb.getX() + 3.0f, std::round (y), thumb.getWidth() - 6.0f, 1.0f);
    }
    else
    {
        const auto groove = area.withSizeKeepingCenter (area.getWidth(), 4.0f);
        g.setFillColor (theme::track);
        g.fillRoundedRect (groove, 2.0f);

        const float x = area.getX() + position * area.getWidth();
        g.setFillColor (accentColor.withAlpha (0.8f));
        g.fillRoundedRect (groove.withWidth (x - groove.getX()), 2.0f);

        const float unityX = area.getX() + unity * area.getWidth();
        g.setFillColor (theme::faint);
        g.fillRect (std::round (unityX), area.getY(), 1.0f, area.getHeight());

        const auto thumb = yup::Rectangle<float> (x - 6.0f, area.getY(), 12.0f, area.getHeight());
        g.setFillColor (theme::panelRaised.brighter (0.25f));
        g.fillRoundedRect (thumb, 3.0f);
        g.setFillColor (theme::text);
        g.fillRect (std::round (x), thumb.getY() + 3.0f, 1.0f, thumb.getHeight() - 6.0f);
    }
}

void Fader::mouseDown (const yup::MouseEvent& event)
{
    dragStart = event.getPosition();
    dragStartPosition = gainToPosition (gain);

    // A click on the track jumps there; a click on the thumb just grabs it
    const auto area = trackBounds();
    const float clicked = vertical ? (area.getBottom() - dragStart.getY()) / area.getHeight()
                                   : (dragStart.getX() - area.getX()) / area.getWidth();
    if (std::abs (clicked - dragStartPosition) * (vertical ? area.getHeight() : area.getWidth()) > 8.0f)
    {
        dragStartPosition = std::clamp (clicked, 0.0f, 1.0f);
        setFromPosition (dragStartPosition);
    }
}

void Fader::mouseDrag (const yup::MouseEvent& event)
{
    const auto area = trackBounds();
    const float scale = event.getModifiers().isShiftDown() ? 0.2f : 1.0f;
    const float delta = vertical ? (dragStart.getY() - event.getPosition().getY()) / area.getHeight()
                                 : (event.getPosition().getX() - dragStart.getX()) / area.getWidth();
    setFromPosition (std::clamp (dragStartPosition + delta * scale, 0.0f, 1.0f));
}

void Fader::mouseDoubleClick (const yup::MouseEvent&)
{
    setFromPosition (gainToPosition (1.0f));
}

bool Fader::mouseWheel (const yup::MouseEvent&, const yup::MouseWheelData& wheel)
{
    const float delta = vertical ? wheel.getDeltaY() : (wheel.getDeltaX() != 0.0f ? wheel.getDeltaX() : wheel.getDeltaY());
    setFromPosition (std::clamp (gainToPosition (gain) + delta * 0.02f, 0.0f, 1.0f));
    return true;
}

//==============================================================================
void PanSlider::setPan (float newPan)
{
    newPan = std::clamp (newPan, -1.0f, 1.0f);
    if (newPan != pan)
    {
        pan = newPan;
        repaint();
    }
}

void PanSlider::paint (yup::Graphics& g)
{
    const auto area = getLocalBounds().reduced (4.0f, 0.0f);
    const auto groove = area.withSizeKeepingCenter (area.getWidth(), 4.0f);
    g.setFillColor (theme::track);
    g.fillRoundedRect (groove, 2.0f);

    const float centre = area.getCenterX();
    const float x = centre + pan * area.getWidth() * 0.5f;
    g.setFillColor (theme::accent.withAlpha (0.8f));
    g.fillRect (std::min (centre, x), groove.getY(), std::abs (x - centre), groove.getHeight());

    g.setFillColor (theme::faint);
    g.fillRect (std::round (centre), area.getY() + 2.0f, 1.0f, area.getHeight() - 4.0f);

    g.setFillColor (theme::text);
    g.fillRoundedRect (yup::Rectangle<float> (x - 3.0f, area.getCenterY() - 5.0f, 6.0f, 10.0f), 2.0f);
}

void PanSlider::mouseDown (const yup::MouseEvent& event)
{
    dragStartX = event.getPosition().getX();
    dragStartPan = pan;
}

void PanSlider::mouseDrag (const yup::MouseEvent& event)
{
    const float width = std::max (1.0f, getWidth() - 8.0f);
    const float scale = event.getModifiers().isShiftDown() ? 0.2f : 1.0f;
    float newPan = dragStartPan + (event.getPosition().getX() - dragStartX) / (width * 0.5f) * scale;
    if (std::abs (newPan) < 0.04f)
        newPan = 0.0f; // a little detent at the centre
    newPan = std::clamp (newPan, -1.0f, 1.0f);
    if (newPan != pan)
    {
        pan = newPan;
        repaint();
        if (onChange)
            onChange (pan);
    }
}

void PanSlider::mouseDoubleClick (const yup::MouseEvent&)
{
    pan = 0.0f;
    repaint();
    if (onChange)
        onChange (pan);
}

//==============================================================================
ToggleChip::ToggleChip (yup::String text, yup::Color colorWhenOn)
    : yup::Component ("ToggleChip")
    , label (std::move (text))
    , onColor (colorWhenOn)
{
}

void ToggleChip::setOn (bool shouldBeOn)
{
    if (on != shouldBeOn)
    {
        on = shouldBeOn;
        repaint();
    }
}

void ToggleChip::paint (yup::Graphics& g)
{
    const auto area = getLocalBounds().reduced (1.0f);
    if (on)
    {
        g.setFillColor (onColor);
        g.fillRoundedRect (area, 4.0f);
    }
    else
    {
        g.setFillColor (hover ? theme::panelRaised.brighter (0.1f) : theme::panelRaised);
        g.fillRoundedRect (area, 4.0f);
        g.setStrokeColor (theme::hairline);
        g.setStrokeWidth (1.0f);
        g.strokeRoundedRect (area, 4.0f);
    }
    drawText (g, label, theme::font (std::min (11.0f, area.getHeight() * 0.62f)), area,
              on ? theme::background : (hover ? theme::text : theme::dim), yup::Justification::center);
}

void ToggleChip::mouseDown (const yup::MouseEvent&)
{
    on = ! on;
    repaint();
    if (onToggle)
        onToggle (on);
}

void ToggleChip::mouseEnter (const yup::MouseEvent&)
{
    hover = true;
    repaint();
}

void ToggleChip::mouseExit (const yup::MouseEvent&)
{
    hover = false;
    repaint();
}

//==============================================================================
FlatButton::FlatButton (yup::String buttonText, bool isPrimary)
    : yup::Component ("FlatButton")
    , text (std::move (buttonText))
    , primary (isPrimary)
{
}

void FlatButton::paint (yup::Graphics& g)
{
    const auto area = getLocalBounds().reduced (1.0f);
    auto fill = primary ? theme::accent : theme::panelRaised;
    if (! enabledLook)
        fill = theme::panelRaised.darker (0.2f);
    else if (pressed)
        fill = fill.darker (0.15f);
    else if (hover)
        fill = fill.brighter (0.12f);

    g.setFillColor (fill);
    g.fillRoundedRect (area, 5.0f);
    if (! primary)
    {
        g.setStrokeColor (theme::hairline);
        g.setStrokeWidth (1.0f);
        g.strokeRoundedRect (area, 5.0f);
    }
    drawText (g, text, theme::font (std::min (13.0f, area.getHeight() * 0.5f)), area,
              ! enabledLook ? theme::faint : (primary ? theme::background : theme::text), yup::Justification::center);
}

void FlatButton::mouseDown (const yup::MouseEvent&)
{
    pressed = true;
    repaint();
}

void FlatButton::mouseUp (const yup::MouseEvent& event)
{
    const bool click = pressed && getLocalBounds().contains (event.getPosition());
    pressed = false;
    repaint();
    if (click && enabledLook && onClick)
        onClick();
}

void FlatButton::mouseEnter (const yup::MouseEvent&)
{
    hover = true;
    repaint();
}

void FlatButton::mouseExit (const yup::MouseEvent&)
{
    hover = false;
    pressed = false;
    repaint();
}

//==============================================================================
float LevelMeter::levelToPosition (float linear) noexcept
{
    const float db = gainToDb (linear);
    return std::clamp ((db - minDb) / (maxFaderDb - minDb), 0.0f, 1.0f);
}

void LevelMeter::update (audio::Meter& meter, double frameSeconds)
{
    float newPeaks[2] {};
    meter.readAndResetPeaks (newPeaks[0], newPeaks[1]);
    const float newRms[2] { meter.rmsLeft.load(), meter.rmsRight.load() };

    // Peaks fall at about 24 dB a second; the hold line stays a second and a half
    const float fall = std::pow (10.0f, -24.0f * static_cast<float> (frameSeconds) / 20.0f);
    bool changed = false;
    for (int i = 0; i < 2; ++i)
    {
        const float falling = std::max (newPeaks[i], peak[i] * fall);
        changed = changed || std::abs (falling - peak[i]) > 1.0e-4f || std::abs (newRms[i] - rms[i]) > 1.0e-4f;
        peak[i] = falling < 1.0e-4f ? 0.0f : falling;
        rms[i] = newRms[i];

        holdAge[i] += frameSeconds;
        if (newPeaks[i] >= hold[i] || holdAge[i] > 1.5)
        {
            changed = changed || hold[i] != newPeaks[i];
            hold[i] = newPeaks[i];
            holdAge[i] = 0.0;
        }
    }
    if (changed)
        repaint();
}

void LevelMeter::paintBar (yup::Graphics& g, yup::Rectangle<float> bar, float peakLevel, float rmsLevel, float holdLevel)
{
    g.setFillColor (theme::background);
    g.fillRect (bar);

    const auto colorAt = [] (float position)
    {
        if (position > levelToPosition (1.0f))
            return theme::meterHigh;
        if (position > levelToPosition (0.5f))
            return theme::meterMid;
        return theme::meterLow;
    };

    const float p = levelToPosition (peakLevel);
    const float r = levelToPosition (rmsLevel);
    const float h = levelToPosition (holdLevel);
    if (vertical)
    {
        g.setFillColor (colorAt (p).withAlpha (0.45f));
        g.fillRect (bar.withTrimmedTop (bar.getHeight() * (1.0f - p)));
        g.setFillColor (colorAt (r));
        g.fillRect (bar.withTrimmedTop (bar.getHeight() * (1.0f - r)));
        if (h > 0.0f)
        {
            g.setFillColor (colorAt (h));
            g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * h, bar.getWidth(), 1.5f);
        }
    }
    else
    {
        g.setFillColor (colorAt (p).withAlpha (0.45f));
        g.fillRect (bar.withWidth (bar.getWidth() * p));
        g.setFillColor (colorAt (r));
        g.fillRect (bar.withWidth (bar.getWidth() * r));
        if (h > 0.0f)
        {
            g.setFillColor (colorAt (h));
            g.fillRect (bar.getX() + bar.getWidth() * h - 1.5f, bar.getY(), 1.5f, bar.getHeight());
        }
    }
}

void LevelMeter::paint (yup::Graphics& g)
{
    auto area = getLocalBounds();
    if (vertical)
    {
        const float w = std::floor ((area.getWidth() - 1.0f) * 0.5f);
        paintBar (g, area.removeFromLeft (w), peak[0], rms[0], hold[0]);
        area.removeFromLeft (1.0f);
        paintBar (g, area, peak[1], rms[1], hold[1]);
    }
    else
    {
        const float h = std::floor ((area.getHeight() - 1.0f) * 0.5f);
        paintBar (g, area.removeFromTop (h), peak[0], rms[0], hold[0]);
        area.removeFromTop (1.0f);
        paintBar (g, area, peak[1], rms[1], hold[1]);
    }
}

//==============================================================================
LineEdit::LineEdit (yup::String placeholderText)
    : yup::Component ("LineEdit")
    , placeholder (std::move (placeholderText))
{
    using Style = yup::TextEditor::Style;
    editor.setMultiLine (false);
    editor.setFontSize (14.0f);
    editor.setColor (Style::backgroundColorId, yup::Color (0x00000000));
    editor.setColor (Style::outlineColorId, yup::Color (0x00000000));
    editor.setColor (Style::focusedOutlineColorId, yup::Color (0x00000000));
    editor.setColor (Style::textColorId, theme::text);
    editor.setColor (Style::caretColorId, theme::accent);
    editor.setColor (Style::selectionColorId, theme::accent.withAlpha (0.35f));
    editor.onReturn = [this] { if (onReturn) onReturn(); };
    editor.onFocusChange = [this] { repaint(); };
    addAndMakeVisible (editor);
}

void LineEdit::resized()
{
    const float height = 14.0f * 1.35f + 8.0f; // one line plus the editor's own 4 px margins
    editor.setBounds (getLocalBounds().reduced (6.0f, 0.0f).withSizeKeepingCenter (getWidth() - 12.0f, height));
}

void LineEdit::paint (yup::Graphics& g)
{
    const auto area = getLocalBounds().reduced (0.5f);
    g.setFillColor (theme::background);
    g.fillRoundedRect (area, 5.0f);
    g.setStrokeColor (editor.hasKeyboardFocus() ? theme::accent : theme::hairline);
    g.setStrokeWidth (editor.hasKeyboardFocus() ? 1.5f : 1.0f);
    g.strokeRoundedRect (area, 5.0f);
}

void LineEdit::paintOverChildren (yup::Graphics& g)
{
    if (editor.getText().isEmpty() && ! editor.hasKeyboardFocus())
        drawText (g, placeholder, theme::font (13.0f), getLocalBounds().reduced (10.0f, 0.0f), theme::faint);
}

void LineEdit::mouseDown (const yup::MouseEvent&)
{
    editor.takeKeyboardFocus();
}

bool LineEdit::Editor::keyDown (const yup::KeyPress& key, const yup::Point<float>& position)
{
    if (key.getKey() == yup::KeyPress::enterKey)
    {
        if (onReturn)
            onReturn();
        return true;
    }
    return yup::TextEditor::keyDown (key, position);
}

void LineEdit::Editor::focusGained()
{
    yup::TextEditor::focusGained();
    if (onFocusChange)
        onFocusChange();
}

void LineEdit::Editor::focusLost()
{
    yup::TextEditor::focusLost();
    if (onFocusChange)
        onFocusChange();
}

} // namespace jamtaba::ui
