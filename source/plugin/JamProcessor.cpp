/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "plugin/JamProcessor.h"

namespace jamtaba
{

namespace
{
yup::AudioBusLayout makeBusLayout()
{
    using Bus = yup::AudioBus;
    return yup::AudioBusLayout ({ Bus ("input", Bus::Audio, Bus::Input, 2) },
                                { Bus ("output", Bus::Audio, Bus::Output, 2) });
}
} // namespace

JamProcessor::JamProcessor()
    : yup::AudioProcessor ("JamTabaYUP", makeBusLayout())
{
}

void JamProcessor::prepareToPlay (const yup::AudioSpec&)
{
}

void JamProcessor::processBlock (yup::AudioProcessContext<float>& context)
{
    context.audio.clear();
}

yup::Result JamProcessor::saveStateIntoDataTree (yup::DataTree& state)
{
    state = yup::DataTree ("JamTabaState");
    auto transaction = state.beginTransaction();
    transaction.setProperty ("version", 1);
    return yup::Result::ok();
}

yup::Result JamProcessor::loadStateFromDataTree (const yup::DataTree& state)
{
    if (state.getType() != yup::Identifier ("JamTabaState"))
        return yup::Result::fail ("Not a JamTabaYUP state");

    return yup::Result::ok();
}

} // namespace jamtaba

//==============================================================================
extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new jamtaba::JamProcessor();
}
