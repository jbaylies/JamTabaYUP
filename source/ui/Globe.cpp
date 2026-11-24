/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "ui/Globe.h"
#include "ui/WorldData.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace jamtaba::ui
{

namespace
{
constexpr float degrees = std::numbers::pi_v<float> / 180.0f;
constexpr float spinDegreesPerSecond = 6.0f;
constexpr float swayDegrees = 22.0f;

const yup::Color ocean { 0xff17202c };
const yup::Color oceanRim { 0xff22334a };
const yup::Color land { 0xff2f5e5a };
const yup::Color landEdge { 0xff4f9a90 };
const yup::Color gridLine { 0x1fffffff };

/** Wraps an angle in degrees to -180 .. 180. */
float wrap (float angle) noexcept
{
    angle = std::fmod (angle + 180.0f, 360.0f);
    if (angle < 0.0f)
        angle += 360.0f;
    return angle - 180.0f;
}

/** Whether a point (in degrees) is inside a ring of the land data, tested on
    the flat longitude/latitude map, where the rings are simple polygons. */
bool containsPoint (const world::Ring& ring, float longitude, float latitude) noexcept
{
    const float px = longitude * 100.0f, py = latitude * 100.0f;
    bool inside = false;
    for (int i = 0, j = ring.count - 1; i < ring.count; j = i++)
    {
        const auto* a = world::points + 2 * (ring.start + i);
        const auto* b = world::points + 2 * (ring.start + j);
        const float ax = a[0], ay = a[1], bx = b[0], by = b[1];
        if ((ay > py) != (by > py) && px < (bx - ax) * (py - ay) / (by - ay) + ax)
            inside = ! inside;
    }
    return inside;
}
} // namespace

Globe::Globe()
    : yup::Component ("Globe")
{
}

void Globe::setRoom (yup::String newTitle, std::vector<Pin> newPins, int peopleWithoutLocation)
{
    title = std::move (newTitle);
    pins = std::move (newPins);
    withoutLocation = peopleWithoutLocation;

    // Face the pins: the circular mean of their longitudes
    hasTarget = ! pins.empty();
    if (hasTarget)
    {
        float sx = 0.0f, sy = 0.0f;
        for (const auto& pin : pins)
        {
            sx += std::cos (pin.longitude * degrees);
            sy += std::sin (pin.longitude * degrees);
        }
        targetLongitude = std::atan2 (sy, sx) / degrees;
    }
    repaint();
}

Globe::Projected Globe::project (float latitude, float longitude) const noexcept
{
    const float phi = latitude * degrees;
    const float lambda = wrap (longitude - viewLongitude) * degrees;
    const float t = viewTilt * degrees;

    const float x = std::cos (phi) * std::sin (lambda);
    const float y0 = std::sin (phi);
    const float z0 = std::cos (phi) * std::cos (lambda);
    return { x, y0 * std::cos (t) - z0 * std::sin (t), y0 * std::sin (t) + z0 * std::cos (t) };
}

yup::Rectangle<float> Globe::globeBounds() const
{
    auto area = getLocalBounds().withTrimmedTop (44.0f).reduced (8.0f);
    // Room around the globe for the name labels
    const float size = std::max (40.0f, std::min (area.getWidth() - 230.0f, area.getHeight() * 0.86f));
    return area.withSizeKeepingCenter (size, size);
}

void Globe::tick (double frameSeconds)
{
    time += frameSeconds;
    if (dragging)
        return;

    if (idleAfterDrag.load() > 0.0)
    {
        idleAfterDrag.store (idleAfterDrag.load() - frameSeconds);
        return;
    }

    if (hasTarget)
    {
        // Ease toward the pins, swaying a little either side so it stays alive
        const float goal = targetLongitude + swayDegrees * static_cast<float> (std::sin (time * 0.35));
        const float step = 1.0f - std::exp (-static_cast<float> (frameSeconds) * 1.6f);
        const float current = centreLongitude.load();
        centreLongitude.store (wrap (current + wrap (goal - current) * step));
    }
    else
    {
        centreLongitude.store (wrap (centreLongitude.load() + spinDegreesPerSecond * static_cast<float> (frameSeconds)));
    }
    repaint();
}

void Globe::mouseDown (const yup::MouseEvent& event)
{
    dragging.store (true);
    dragStart = event.getPosition();
    dragStartLongitude = centreLongitude.load();
    dragStartTilt = tilt.load();
}

void Globe::mouseDrag (const yup::MouseEvent& event)
{
    const float radius = std::max (1.0f, globeBounds().getWidth() * 0.5f);
    const auto delta = event.getPosition() - dragStart;
    centreLongitude.store (wrap (dragStartLongitude - delta.getX() / radius * 60.0f));
    tilt.store (std::clamp (dragStartTilt + delta.getY() / radius * 60.0f, -60.0f, 60.0f));
    repaint();
}

void Globe::mouseUp (const yup::MouseEvent&)
{
    idleAfterDrag.store (4.0); // let the user look before it turns again
    dragging.store (false);
}

//==============================================================================
void Globe::paint (yup::Graphics& g)
{
    // One view for the whole frame, however the drag moves it meanwhile
    viewLongitude = centreLongitude.load();
    viewTilt = tilt.load();

    auto header = getLocalBounds().removeFromTop (44.0f);
    drawText (g, title, theme::font (15.0f), header.removeFromTop (22.0f), theme::text, yup::Justification::center);
    yup::String note;
    if (pins.empty() && withoutLocation == 0)
        note = "Nobody here to show on the map";
    else if (pins.empty())
        note = "The server list has no locations for this room";
    else
        note = yup::String (static_cast<int> (pins.size())) + (pins.size() == 1 ? " musician" : " musicians") + " on the map";
    drawText (g, note, theme::font (12.0f), header, theme::dim, yup::Justification::center);

    const auto bounds = globeBounds();
    const auto centre = bounds.getCenter();
    const float radius = bounds.getWidth() * 0.5f;

    // A faint halo, the ocean with light from the upper left, and the rim
    g.setFillColor (theme::accent.withAlpha (0.06f));
    g.fillEllipse (bounds.enlarged (radius * 0.08f));
    g.setFillColorGradient (yup::ColorGradient (oceanRim, centre.getX() - radius * 0.35f, centre.getY() - radius * 0.4f,
                                                ocean, centre.getX() + radius * 0.6f, centre.getY() + radius * 0.75f,
                                                yup::ColorGradient::Radial));
    g.fillEllipse (bounds);

    paintGraticule (g, centre, radius);
    paintLand (g, centre, radius);

    g.setStrokeColor (theme::accent.withAlpha (0.35f));
    g.setStrokeWidth (1.2f);
    g.strokeEllipse (bounds);

    paintPins (g, centre, radius);
}

void Globe::paintGraticule (yup::Graphics& g, yup::Point<float> centre, float radius)
{
    yup::Path path;
    const auto addLine = [&] (auto pointAt, int steps)
    {
        bool drawing = false;
        for (int i = 0; i <= steps; ++i)
        {
            const auto p = pointAt (i);
            if (p.z <= 0.0f)
            {
                drawing = false;
                continue;
            }
            const float x = centre.getX() + p.x * radius, y = centre.getY() - p.y * radius;
            if (drawing)
                path.lineTo (x, y);
            else
                path.moveTo (x, y);
            drawing = true;
        }
    };

    for (int lon = -180; lon < 180; lon += 30)
        addLine ([&] (int i) { return project (-90.0f + static_cast<float> (i) * 3.0f, static_cast<float> (lon)); }, 60);
    for (int lat = -60; lat <= 60; lat += 30)
        addLine ([&] (int i) { return project (static_cast<float> (lat), -180.0f + static_cast<float> (i) * 4.0f); }, 90);

    g.setStrokeColor (gridLine);
    g.setStrokeWidth (1.0f);
    g.strokePath (path);
}

void Globe::paintLand (yup::Graphics& g, yup::Point<float> centre, float radius)
{
    // Each ring is clipped to the visible half: where it goes behind the globe
    // it's cut at the horizon and followed round the rim, the way the hidden
    // part winds, to where it comes back. If the point straight behind the
    // globe is inside the ring, that closes the ring around the sea instead of
    // the land, so a rim circle the other way round flips it back.
    //
    // Each ring gets new Path objects: drawing is recorded and rendered at the
    // end of the frame, so a path must not change after it has been drawn.
    constexpr float maxStep = 2.0f * degrees;    // longest straight piece of a ring
    constexpr float rimStep = 3.0f * degrees;    // of an arc along the rim
    const float backLatitude = -viewTilt, backLongitude = wrap (viewLongitude + 180.0f);

    std::vector<yup::Point<float>> outline;
    for (const auto& ring : world::rings)
    {
        const auto at = [&] (int i)
        {
            const auto* point = world::points + 2 * (ring.start + i);
            return project (point[1] * 0.01f, point[0] * 0.01f);
        };

        int first = -1;
        for (int i = 0; i < ring.count && first < 0; ++i)
            if (at (i).z > 0.0f)
                first = i;
        if (first < 0)
            continue; // wholly behind the globe

        yup::Path edges;
        outline.clear();
        const auto toScreen = [&] (float x, float y) { return yup::Point<float> (centre.getX() + x * radius, centre.getY() - y * radius); };

        bool hidden = false;
        float exitAngle = 0.0f, lastAngle = 0.0f, swept = 0.0f;
        Projected previous = at (first);
        outline.push_back (toScreen (previous.x, previous.y));
        edges.moveTo (outline.back());

        for (int k = 1; k <= ring.count; ++k)
        {
            const Projected next = at ((first + k) % ring.count);
            // Antarctica is closed along the 180th meridian and the pole: fill those, but don't outline them
            const auto* from = world::points + 2 * (ring.start + (first + k - 1) % ring.count);
            const auto* to = world::points + 2 * (ring.start + (first + k) % ring.count);
            const bool seam = (std::abs (from[0]) == 18000 && std::abs (to[0]) == 18000) || (from[1] <= -8990 && to[1] <= -8990);
            const float dot = std::clamp (previous.x * next.x + previous.y * next.y + previous.z * next.z, -1.0f, 1.0f);
            const int pieces = std::max (1, static_cast<int> (std::ceil (std::acos (dot) / maxStep)));

            Projected p = previous;
            for (int j = 1; j <= pieces; ++j)
            {
                const float f = static_cast<float> (j) / static_cast<float> (pieces);
                const Projected q { previous.x + (next.x - previous.x) * f,
                                    previous.y + (next.y - previous.y) * f,
                                    previous.z + (next.z - previous.z) * f };

                const auto horizon = [&]
                {
                    const float t = p.z / (p.z - q.z);
                    return std::atan2 (p.y + (q.y - p.y) * t, p.x + (q.x - p.x) * t);
                };

                if (! hidden && q.z > 0.0f)
                {
                    outline.push_back (toScreen (q.x, q.y));
                    if (seam)
                        edges.moveTo (outline.back());
                    else
                        edges.lineTo (outline.back());
                }
                else if (! hidden)
                {
                    exitAngle = lastAngle = horizon();
                    swept = 0.0f;
                    hidden = true;
                    outline.push_back (toScreen (std::cos (exitAngle), std::sin (exitAngle)));
                    edges.lineTo (outline.back());
                }
                else if (q.z <= 0.0f)
                {
                    const float angle = std::atan2 (q.y, q.x);
                    swept += std::remainder (angle - lastAngle, 2.0f * std::numbers::pi_v<float>);
                    lastAngle = angle;
                }
                else
                {
                    const float angle = horizon();
                    swept += std::remainder (angle - lastAngle, 2.0f * std::numbers::pi_v<float>);
                    const int steps = std::max (1, static_cast<int> (std::ceil (std::abs (swept) / rimStep)));
                    for (int s = 1; s <= steps; ++s)
                    {
                        const float a = exitAngle + swept * static_cast<float> (s) / static_cast<float> (steps);
                        outline.push_back (toScreen (std::cos (a), std::sin (a)));
                    }
                    edges.moveTo (outline.back());
                    outline.push_back (toScreen (q.x, q.y));
                    edges.lineTo (outline.back());
                    hidden = false;
                }
                p = q;
            }
            previous = next;
        }

        yup::Path fill;
        fill.moveTo (outline.front());
        for (size_t i = 1; i < outline.size(); ++i)
            fill.lineTo (outline[i]);
        fill.close();

        if (containsPoint (ring, backLongitude, backLatitude))
        {
            float area = 0.0f;
            for (size_t i = 0; i < outline.size(); ++i)
            {
                const auto& a = outline[i];
                const auto& b = outline[(i + 1) % outline.size()];
                area += a.getX() * b.getY() - b.getX() * a.getY();
            }
            const float direction = area > 0.0f ? -1.0f : 1.0f;
            constexpr int rimPoints = 120;
            for (int s = 0; s <= rimPoints; ++s)
            {
                const float a = direction * 2.0f * std::numbers::pi_v<float> * static_cast<float> (s) / rimPoints;
                const yup::Point<float> point (centre.getX() + std::cos (a) * radius, centre.getY() + std::sin (a) * radius);
                if (s == 0)
                    fill.moveTo (point);
                else
                    fill.lineTo (point);
            }
            fill.close();
        }

        g.setFillColor (land);
        g.fillPath (fill);
        g.setStrokeColor (landEdge.withAlpha (0.7f));
        g.setStrokeWidth (0.8f);
        g.strokePath (edges);
    }
}

void Globe::paintPins (yup::Graphics& g, yup::Point<float> centre, float radius)
{
    struct Label
    {
        const Pin* pin;
        yup::Point<float> dot;
        float angle; // of the dot around the centre
        float z;
    };

    std::vector<Label> labels;
    for (const auto& pin : pins)
    {
        const auto p = project (pin.latitude, pin.longitude);
        if (p.z <= 0.05f)
            continue;
        const yup::Point<float> dot (centre.getX() + p.x * radius, centre.getY() - p.y * radius);
        labels.push_back ({ &pin, dot, std::atan2 (-p.y, p.x), p.z });
    }

    // Labels sit outside the rim, on the side of their dot, stacked so they don't overlap
    const auto font = theme::font (12.5f);
    const float lineHeight = 30.0f;
    std::vector<Label*> left, right;
    for (auto& label : labels)
        (label.dot.getX() < centre.getX() ? left : right).push_back (&label);

    for (auto* side : { &left, &right })
    {
        auto& list = *side;
        std::sort (list.begin(), list.end(), [] (const Label* a, const Label* b) { return a->dot.getY() < b->dot.getY(); });
        const bool isLeft = side == &left;

        // Ideal heights follow the dots; then push apart top-down and clamp
        std::vector<float> ys;
        for (const auto* label : list)
            ys.push_back (label->dot.getY());
        for (size_t i = 1; i < ys.size(); ++i)
            ys[i] = std::max (ys[i], ys[i - 1] + lineHeight);
        const float bottom = centre.getY() + radius + 10.0f;
        for (size_t i = ys.size(); i-- > 0;)
        {
            const float limit = bottom - static_cast<float> (ys.size() - 1 - i) * lineHeight;
            ys[i] = std::min (ys[i], limit);
            if (i + 1 < ys.size())
                ys[i] = std::min (ys[i], ys[i + 1] - lineHeight);
        }

        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto& label = *list[i];
            const float x = centre.getX() + (isLeft ? -1.0f : 1.0f) * (radius + 24.0f);
            const float y = ys[i];

            // The line: from the dot out past the rim, then level to the name
            const float elbowX = centre.getX() + (isLeft ? -1.0f : 1.0f) * (radius + 10.0f);
            g.setStrokeColor (label.pin->color.withAlpha (0.75f));
            g.setStrokeWidth (1.2f);
            yup::Path line;
            line.moveTo (label.dot.getX(), label.dot.getY());
            line.lineTo (elbowX, y);
            line.lineTo (x, y);
            g.strokePath (line);

            // The pin: a glow and a bright dot, smaller near the rim
            const float size = 4.0f + 2.5f * label.z;
            g.setFillColor (label.pin->color.withAlpha (0.25f));
            g.fillEllipse (label.dot.getX() - size * 1.8f, label.dot.getY() - size * 1.8f, size * 3.6f, size * 3.6f);
            g.setFillColor (label.pin->color);
            g.fillEllipse (label.dot.getX() - size * 0.5f, label.dot.getY() - size * 0.5f, size, size);

            const float width = std::max (60.0f, (isLeft ? x - 4.0f : getWidth() - x - 4.0f));
            const auto nameArea = isLeft ? yup::Rectangle<float> (x - width - 4.0f, y - 15.0f, width, 16.0f)
                                         : yup::Rectangle<float> (x + 4.0f, y - 15.0f, width, 16.0f);
            const auto just = isLeft ? yup::Justification::centerRight : yup::Justification::centerLeft;
            drawText (g, theme::str (label.pin->name), font, nameArea, theme::text, just);
            if (! label.pin->country.empty())
                drawText (g, theme::str (label.pin->country), theme::font (11.0f), nameArea.translated (0.0f, 14.0f), theme::dim, just);
        }
    }
}

} // namespace jamtaba::ui
