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

#include <memory>

namespace jamtaba
{

namespace ui
{
class ServerBrowser;
class JamRoom;
} // namespace ui

/** The main window: the server browser while disconnected, the jam room
    while connecting or connected. */
class JamEditor final : public yup::AudioProcessorEditor
{
public:
    explicit JamEditor (JamProcessor& processor);
    ~JamEditor() override;

    bool isResizable() const override { return true; }
    yup::Size<int> getPreferredSize() const override { return { 1180, 720 }; }

    void resized() override;
    void paint (yup::Graphics& g) override;
    void refreshDisplay (double lastFrameTimeSeconds) override;

private:
    void showView();

    JamProcessor& processor;
    std::unique_ptr<ui::ServerBrowser> browser;
    std::unique_ptr<ui::JamRoom> room;
    std::uint32_t seenLayout = 0;

    YUP_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JamEditor)
};

} // namespace jamtaba
