/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "engine/JamEngine.h"
#include "plugin/ServerListService.h"
#include "plugin/SessionModel.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <array>
#include <functional>
#include <map>

namespace jamtaba
{

/** What JamTabaYUP remembers between sessions (saved with the host's project,
    or by the standalone app). */
struct JamSettings
{
    enum class InputMode
    {
        stereo,  // inputs 1 + 2
        monoLeft,
        monoRight,
    };

    std::string userName;
    std::string lastServer; // host:port
    std::string channelName = "my channel";
    InputMode inputMode = InputMode::stereo;
    bool transmit = true;
    bool monitor = true;
    float localGain = 1.0f, localPan = 0.0f;
    bool localMute = false;
    float metronomeGain = 0.5f;
    bool metronomeMute = false;
    float masterGain = 1.0f;
    bool chatVisible = true;
};

/** Stereo in (the local channel), stereo out (the jam mix). Owns the engine,
    and keeps the session model current from a timer on the message thread. */
class JamProcessor final : public yup::AudioProcessor
                         , private yup::Timer
{
public:
    JamProcessor();
    ~JamProcessor() override;

    JamEngine& getEngine() noexcept { return engine; }
    SessionModel& getModel() noexcept { return model; }
    JamSettings& getSettings() noexcept { return settings; }
    ServerListService& getServerList() noexcept { return serverList; }
    bool isStandalone() const noexcept;

    void connectTo (const std::string& host, int port, const std::string& password);
    void disconnect();

    /** Pushes the local channel, metronome and master settings to the engine. */
    void applySettings();

    /** The remote mix is remembered per user name and channel. */
    void rememberRemoteMix (const std::string& fullName, int channelIndex);

    //==============================================================================
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
    struct StripMemory
    {
        float gain = 1.0f, pan = 0.0f;
        bool mute = false;
    };

    void timerCallback() override;
    void restoreRemoteMix (const JamEvent& event);
    static std::string mixKey (const std::string& fullName, int channelIndex);

    JamEngine engine;
    SessionModel model;
    JamSettings settings;
    ServerListService serverList;
    std::map<std::string, StripMemory> remoteMix;

    std::array<const float*, 8> inputPointers {};

    YUP_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JamProcessor)
};

} // namespace jamtaba
