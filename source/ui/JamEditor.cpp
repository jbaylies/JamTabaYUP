/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "ui/JamEditor.h"
#include "ui/JamTheme.h"

namespace jamtaba
{

JamEditor::JamEditor (JamProcessor& p)
    : processor (p)
{
    setOpaque (true);
}

void JamEditor::paint (yup::Graphics& g)
{
    g.setFillColor (theme::background);
    g.fillAll();

    g.setFillColor (theme::text);
    g.fillFittedText ("JamTabaYUP", theme::labelFont (32.0f), getLocalBounds());
}

//==============================================================================
yup::AudioProcessorEditor* JamProcessor::createEditor()
{
    return new JamEditor (*this);
}

} // namespace jamtaba
