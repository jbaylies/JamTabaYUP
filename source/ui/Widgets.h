/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "engine/audio/AudioEngine.h"
#include "ui/JamTheme.h"

#include <functional>

namespace jamtaba::ui
{

/** Gain fader, vertical or horizontal. The value is linear gain, 0 .. 2
    (+6 dB), shown on a dB scale. Double-click resets to 0 dB; shift drags finely. */
class Fader final : public yup::Component
{
public:
    explicit Fader (bool vertical = true);

    void setGain (float gain);
    float getGain() const noexcept { return gain; }
    void setAccent (yup::Color color) { accentColor = color; repaint(); }

    std::function<void (float)> onChange;

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    void mouseDrag (const yup::MouseEvent& event) override;
    void mouseDoubleClick (const yup::MouseEvent& event) override;
    bool mouseWheel (const yup::MouseEvent& event, const yup::MouseWheelData& wheel) override;

    static float gainToPosition (float gain) noexcept;
    static float positionToGain (float position) noexcept;

private:
    yup::Rectangle<float> trackBounds() const;
    void setFromPosition (float position);

    bool vertical;
    float gain = 1.0f;
    float dragStartPosition = 0.0f;
    yup::Point<float> dragStart;
    yup::Color accentColor = theme::accent;
};

/** Pan, -1 .. 1, as a small horizontal bar from the centre. Double-click centres it. */
class PanSlider final : public yup::Component
{
public:
    void setPan (float pan);
    float getPan() const noexcept { return pan; }

    std::function<void (float)> onChange;

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    void mouseDrag (const yup::MouseEvent& event) override;
    void mouseDoubleClick (const yup::MouseEvent& event) override;

private:
    float pan = 0.0f, dragStartPan = 0.0f;
    float dragStartX = 0.0f;
};

/** A small rounded toggle: M, S, XMIT, RX, MON... */
class ToggleChip final : public yup::Component
{
public:
    ToggleChip (yup::String label, yup::Color onColor);

    void setOn (bool shouldBeOn);
    bool isOn() const noexcept { return on; }
    void setLabel (yup::String newLabel) { label = std::move (newLabel); repaint(); }

    std::function<void (bool)> onToggle;

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    void mouseEnter (const yup::MouseEvent& event) override;
    void mouseExit (const yup::MouseEvent& event) override;

private:
    yup::String label;
    yup::Color onColor;
    bool on = false, hover = false;
};

/** A flat push button. */
class FlatButton final : public yup::Component
{
public:
    explicit FlatButton (yup::String text, bool primary = false);

    void setText (yup::String newText) { text = std::move (newText); repaint(); }
    void setEnabledLook (bool enabled) { enabledLook = enabled; repaint(); }

    std::function<void()> onClick;

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    void mouseUp (const yup::MouseEvent& event) override;
    void mouseEnter (const yup::MouseEvent& event) override;
    void mouseExit (const yup::MouseEvent& event) override;

private:
    yup::String text;
    bool primary;
    bool hover = false, pressed = false, enabledLook = true;
};

/** Stereo peak + RMS meter with falling peaks and a peak-hold line.
    update() reads the engine meter; call it every frame. */
class LevelMeter final : public yup::Component
{
public:
    explicit LevelMeter (bool vertical = true) : vertical (vertical) {}

    void update (audio::Meter& meter, double frameSeconds);

    void paint (yup::Graphics& g) override;

    static float levelToPosition (float linear) noexcept; // 0..1 over -60 .. +6 dB

private:
    void paintBar (yup::Graphics& g, yup::Rectangle<float> bar, float peak, float rms, float hold);

    bool vertical;
    float peak[2] {}, rms[2] {}, hold[2] {};
    double holdAge[2] {};
};

/** A single-line text field that reports Enter, with the text centred
    vertically in its box. */
class LineEdit final : public yup::Component
{
public:
    explicit LineEdit (yup::String placeholder = {});

    yup::String getText() const { return editor.getText(); }
    void setText (const yup::String& text, yup::NotificationType notification = yup::sendNotification) { editor.setText (text, notification); }
    void focus() { editor.takeKeyboardFocus(); }

    std::function<void()> onReturn;

    void resized() override;
    void paint (yup::Graphics& g) override;
    void paintOverChildren (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;

private:
    class Editor final : public yup::TextEditor
    {
    public:
        Editor() : yup::TextEditor ("LineEditEditor") {}
        std::function<void()> onReturn, onFocusChange;
        bool keyDown (const yup::KeyPress& key, const yup::Point<float>& position) override;
        void focusGained() override;
        void focusLost() override;
    };

    Editor editor;
    yup::String placeholder;
};

/** Draws text in a box, left or centred, clipped to one line. */
void drawText (yup::Graphics& g, const yup::String& text, const yup::Font& font,
               yup::Rectangle<float> area, yup::Color color,
               yup::Justification justification = yup::Justification::centerLeft);

} // namespace jamtaba::ui
