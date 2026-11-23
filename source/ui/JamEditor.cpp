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
}

JamEditor::~JamEditor() = default;

void JamEditor::showView()
{
    const bool inRoom = processor.getModel().connection != SessionModel::Connection::disconnected;
    if (inRoom && room == nullptr)
    {
        browser.reset();
        room = std::make_unique<ui::JamRoom> (processor);
        addAndMakeVisible (*room);
        seenLayout = processor.getModel().layoutVersion;
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

void JamEditor::refreshDisplay (double)
{
    showView();

    if (room != nullptr && processor.getModel().layoutVersion != seenLayout)
    {
        seenLayout = processor.getModel().layoutVersion;
        room->layoutChanged();
        room->repaint();
    }
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
