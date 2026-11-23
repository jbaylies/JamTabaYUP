/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "ui/JamEditor.h"
#include "ui/JamRoom.h"
#include "ui/JamTheme.h"
#include "ui/ServerBrowser.h"

namespace jamtaba
{

JamEditor::JamEditor (JamProcessor& p)
    : processor (p)
{
    setOpaque (true);
    showView();
    startTimerHz (30);
}

JamEditor::~JamEditor()
{
    stopTimer();
}

void JamEditor::showView()
{
    const bool inRoom = processor.uiModel().connection != SessionModel::Connection::disconnected;
    if (inRoom && room == nullptr)
    {
        browser.reset();
        room = std::make_unique<ui::JamRoom> (processor);
        addAndMakeVisible (*room);
        seenLayout = processor.uiModel().layoutVersion;
    }
    else if (! inRoom && browser == nullptr)
    {
        room.reset();
        browser = std::make_unique<ui::ServerBrowser> (processor);
        addAndMakeVisible (*browser);
    }
    else
    {
        return;
    }
    resized();
}

// Threads: YUP calls refreshDisplay() and paint() on its render thread and
// timers and mouse events on the message thread. All per-frame work happens
// in the tick below, under the UI lock; components are only added or removed
// from the timer, under runUiLocked().

void JamEditor::timerCallback()
{
    ui::runUiLocked (*this, processor.getUiLock(), [this]
    {
        showView();

        if (room != nullptr)
        {
            if (processor.uiModel().layoutVersion != seenLayout)
            {
                seenLayout = processor.uiModel().layoutVersion;
                room->layoutChanged();
            }
            room->syncStructure();
        }
    });
}

void JamEditor::refreshDisplay (double frameSeconds)
{
    std::lock_guard guard (processor.getUiLock());
    processor.updateUiModel();

    if (browser != nullptr)
        browser->tick (frameSeconds);
    if (room != nullptr)
        room->tick (frameSeconds);
}

void JamEditor::resized()
{
    if (browser != nullptr)
        browser->setBounds (getLocalBounds());
    if (room != nullptr)
        room->setBounds (getLocalBounds());
}

void JamEditor::paint (yup::Graphics& g)
{
    g.setFillColor (theme::background);
    g.fillAll();
}

//==============================================================================
yup::AudioProcessorEditor* JamProcessor::createEditor()
{
    return new JamEditor (*this);
}

} // namespace jamtaba
