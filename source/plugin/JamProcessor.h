/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include <yup_audio_processors/yup_audio_processors.h>

namespace jamtaba
{

/** Stereo in (the local channel), stereo out (the jam mix).
    The NINJAM engine is wired in during Phase 2; until then the output is silent. */
class JamProcessor final : public yup::AudioProcessor
{
public:
    JamProcessor();

    void prepareToPlay (const yup::AudioSpec& spec) override;
    void releaseResources() override {}
    void processBlock (yup::AudioProcessContext<float>& context) override;
    void flush() override {}

    bool hasEditor() const override { return true; }
    yup::AudioProcessorEditor* createEditor() override;

    int getCurrentPreset() const noexcept override { return 0; }
    void setCurrentPreset (int) noexcept override {}
    int getNumPresets() const override { return 0; }
    yup::String getPresetName (int) const override { return {}; }
    void setPresetName (int, yup::StringRef) override {}

    bool supportsDataTreeState() const noexcept override { return true; }
    yup::Result saveStateIntoDataTree (yup::DataTree& state) override;
    yup::Result loadStateFromDataTree (const yup::DataTree& state) override;

private:
    YUP_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JamProcessor)
};

} // namespace jamtaba
