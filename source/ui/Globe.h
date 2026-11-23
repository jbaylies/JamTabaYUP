/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "ui/Widgets.h"

#include <atomic>
#include <string>
#include <vector>

namespace jamtaba::ui
{

/** A slowly turning globe with a pin for each musician in a room: a dot at
    their (coarse) location and a line out past the rim to their name.

    When pins are set it eases round to face them and sways gently; drag to
    spin it by hand. Land outlines are Natural Earth's 1:110m land. */
class Globe final : public yup::Component
{
public:
    struct Pin
    {
        std::string name;
        std::string country;
        float latitude = 0.0f, longitude = 0.0f;
        yup::Color color = theme::accent;
    };

    Globe();

    /** Render thread, UI lock held: the room shown, its pins, a title, and a
        note for people with no location. */
    void setRoom (yup::String title, std::vector<Pin> pins, int peopleWithoutLocation);

    /** Render thread, UI lock held: turns the globe. */
    void tick (double frameSeconds);

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    void mouseDrag (const yup::MouseEvent& event) override;
    void mouseUp (const yup::MouseEvent& event) override;

private:
    struct Projected
    {
        float x = 0.0f, y = 0.0f; // on the unit disc, y up
        float z = 0.0f;           // > 0 faces the viewer
    };

    Projected project (float latitude, float longitude) const noexcept;
    yup::Rectangle<float> globeBounds() const;
    void paintLand (yup::Graphics& g, yup::Point<float> centre, float radius);
    void paintGraticule (yup::Graphics& g, yup::Point<float> centre, float radius);
    void paintPins (yup::Graphics& g, yup::Point<float> centre, float radius);

    yup::String title;
    std::vector<Pin> pins;
    int withoutLocation = 0;

    // Written by mouse drags on the message thread, read by the render thread
    std::atomic<float> centreLongitude { 10.0f }; // degrees, the meridian facing the viewer
    std::atomic<float> tilt { 18.0f };            // degrees the north pole leans toward the viewer
    std::atomic<bool> dragging { false };
    std::atomic<double> idleAfterDrag { 0.0 };    // seconds until the globe moves on its own again

    float viewLongitude = 10.0f, viewTilt = 18.0f; // render thread: the view this frame paints

    float targetLongitude = 10.0f;
    bool hasTarget = false;
    double time = 0.0;

    yup::Point<float> dragStart;
    float dragStartLongitude = 0.0f, dragStartTilt = 0.0f;
};

} // namespace jamtaba::ui
