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

namespace jamtaba
{

/** The main window. Phase 3 fills it with the server browser and the jam room. */
class JamEditor final : public yup::AudioProcessorEditor
{
public:
    explicit JamEditor (JamProcessor& processor);

    bool isResizable() const override { return true; }
    yup::Size<int> getPreferredSize() const override { return { 1100, 700 }; }

    void paint (yup::Graphics& g) override;

private:
    [[maybe_unused]] JamProcessor& processor;

    YUP_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JamEditor)
};

} // namespace jamtaba
