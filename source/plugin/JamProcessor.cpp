/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "plugin/JamProcessor.h"

#include <algorithm>
#include <sstream>

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

yup::String toYup (const std::string& text)
{
    return yup::String::fromUTF8 (text.c_str(), static_cast<int> (text.size()));
}
} // namespace

JamProcessor::JamProcessor()
    : yup::AudioProcessor ("JamTabaYUP", makeBusLayout())
{
    // In a DAW the host already monitors the track, so ours starts off
    settings.monitor = isStandalone();
    publishModel();
    updateUiModel();
    applySettings();
    startTimerHz (30);

    // For scripted testing: JAMTABA_CONNECT=host:port [JAMTABA_NAME=name] joins at launch
    const auto autoConnect = yup::SystemStats::getEnvironmentVariable ("JAMTABA_CONNECT", {});
    if (isStandalone() && autoConnect.containsChar (':'))
    {
        settings.userName = yup::SystemStats::getEnvironmentVariable ("JAMTABA_NAME", "tester").toStdString();
        connectTo (autoConnect.upToLastOccurrenceOf (":", false, false).toStdString(),
                   autoConnect.fromLastOccurrenceOf (":", false, false).getIntValue(), {});
    }
}

JamProcessor::~JamProcessor()
{
    stopTimer();
    engine.disconnect();
}

bool JamProcessor::isStandalone() const noexcept
{
    return getWrapperType() == yup::AudioPluginWrapperType::standalone;
}

void JamProcessor::connectTo (const std::string& host, int port, const std::string& password)
{
    ConnectionSettings connection;
    connection.host = host;
    connection.port = port;
    connection.userName = settings.userName.empty() ? "jamtabayup" : settings.userName;
    connection.password = password;

    LocalChannelSetup channel;
    channel.name = settings.channelName;
    channel.stereo = settings.inputMode == JamSettings::InputMode::stereo;
    channel.firstInput = settings.inputMode == JamSettings::InputMode::monoRight ? 1 : 0;
    channel.transmit = settings.transmit;
    connection.channels = { channel };

    settings.lastServer = host + ":" + std::to_string (port);
    engine.connect (connection);
    applySettings();
}

void JamProcessor::disconnect()
{
    engine.disconnect();
    timerCallback(); // deliver the disconnected event now
}

void JamProcessor::applySettings()
{
    auto& audio = engine.getAudio();
    auto& local = audio.getLocalChannel (0);
    local.stereo.store (settings.inputMode == JamSettings::InputMode::stereo);
    local.firstInput.store (settings.inputMode == JamSettings::InputMode::monoRight ? 1 : 0);
    local.transmit.store (settings.transmit);
    local.monitor.store (settings.monitor);
    local.controls.gain.store (settings.localGain);
    local.controls.pan.store (settings.localPan);
    local.controls.mute.store (settings.localMute);

    audio.getMetronome().gain.store (settings.metronomeGain);
    audio.getMetronome().mute.store (settings.metronomeMute);
    audio.getMaster().gain.store (settings.masterGain);
}

//==============================================================================
std::string JamProcessor::mixKey (const std::string& fullName, int channelIndex)
{
    return SessionModel::displayNameOf (fullName) + "#" + std::to_string (channelIndex);
}

void JamProcessor::publishModel()
{
    auto copy = std::make_shared<const SessionModel> (model);
    std::lock_guard guard (publishLock);
    published = std::move (copy);
}

void JamProcessor::updateUiModel()
{
    std::lock_guard guard (publishLock);
    uiSnapshot = published;
}

void JamProcessor::setReceiving (const std::string& fullName, int channelIndex, bool receive)
{
    engine.setReceiving (fullName, channelIndex, receive);
    if (auto* channel = model.findChannel (fullName, channelIndex))
    {
        channel->receiving = receive;
        publishModel();
    }
}

void JamProcessor::rememberRemoteMix (const std::string& fullName, int channelIndex)
{
    const auto* channel = model.findChannel (fullName, channelIndex);
    if (channel == nullptr || channel->slot < 0)
        return;

    const auto& controls = engine.getAudio().getSlot (channel->slot).controls;
    remoteMix[mixKey (fullName, channelIndex)] = { controls.gain.load(), controls.pan.load(), controls.mute.load() };
}

void JamProcessor::restoreRemoteMix (const JamEvent& event)
{
    if (event.slot < 0)
        return;

    const auto found = remoteMix.find (mixKey (event.user, event.channelIndex));
    if (found == remoteMix.end())
        return;

    auto& controls = engine.getAudio().getSlot (event.slot).controls;
    controls.gain.store (found->second.gain);
    controls.pan.store (found->second.pan);
    controls.mute.store (found->second.mute);
}

void JamProcessor::timerCallback()
{
    bool any = false;
    engine.pollEvents ([&] (const JamEvent& event)
    {
        any = true;
        model.handle (event);
        if (event.type == JamEvent::Type::channelAdded)
            restoreRemoteMix (event);
        if (event.type == JamEvent::Type::connected)
            applySettings();
    });

    if (any)
        publishModel();
}

//==============================================================================
void JamProcessor::prepareToPlay (const yup::AudioSpec& spec)
{
    engine.getAudio().prepare (static_cast<double> (spec.sampleRate), spec.maxBlockSize, 2);
}

void JamProcessor::processBlock (yup::AudioProcessContext<float>& context)
{
    auto& buffer = context.audio;
    const int numChannels = std::min (buffer.getNumChannels(), static_cast<int> (inputPointers.size()));
    float* const* channels = buffer.getArrayOfWritePointers();
    for (int i = 0; i < numChannels; ++i)
        inputPointers[static_cast<size_t> (i)] = channels[i];

    engine.getAudio().process (inputPointers.data(), std::min (numChannels, 2), channels, numChannels, buffer.getNumSamples());
}

//==============================================================================
yup::Result JamProcessor::saveStateIntoDataTree (yup::DataTree& state)
{
    std::ostringstream mix;
    for (const auto& [key, memory] : remoteMix)
        mix << key << '\t' << memory.gain << '\t' << memory.pan << '\t' << (memory.mute ? 1 : 0) << '\n';

    state = yup::DataTree ("JamTabaState");
    auto transaction = state.beginTransaction();
    transaction.setProperty ("version", 1);
    transaction.setProperty ("userName", toYup (settings.userName));
    transaction.setProperty ("lastServer", toYup (settings.lastServer));
    transaction.setProperty ("channelName", toYup (settings.channelName));
    transaction.setProperty ("inputMode", static_cast<int> (settings.inputMode));
    transaction.setProperty ("transmit", settings.transmit);
    transaction.setProperty ("monitor", settings.monitor);
    transaction.setProperty ("localGain", static_cast<double> (settings.localGain));
    transaction.setProperty ("localPan", static_cast<double> (settings.localPan));
    transaction.setProperty ("localMute", settings.localMute);
    transaction.setProperty ("metronomeGain", static_cast<double> (settings.metronomeGain));
    transaction.setProperty ("metronomeMute", settings.metronomeMute);
    transaction.setProperty ("masterGain", static_cast<double> (settings.masterGain));
    transaction.setProperty ("chatVisible", settings.chatVisible);
    transaction.setProperty ("remoteMix", toYup (mix.str()));
    return yup::Result::ok();
}

yup::Result JamProcessor::loadStateFromDataTree (const yup::DataTree& state)
{
    if (state.getType() != yup::Identifier ("JamTabaState"))
        return yup::Result::fail ("Not a JamTabaYUP state");

    const auto text = [&] (const char* id, const std::string& fallback)
    {
        return state.getProperty (id, toYup (fallback)).toString().toStdString();
    };
    const auto number = [&] (const char* id, float fallback)
    {
        return static_cast<float> (static_cast<double> (state.getProperty (id, static_cast<double> (fallback))));
    };
    const auto flag = [&] (const char* id, bool fallback)
    {
        return static_cast<bool> (state.getProperty (id, fallback));
    };

    settings.userName = text ("userName", settings.userName);
    settings.lastServer = text ("lastServer", settings.lastServer);
    settings.channelName = text ("channelName", settings.channelName);
    settings.inputMode = static_cast<JamSettings::InputMode> (std::clamp (static_cast<int> (state.getProperty ("inputMode", 0)), 0, 2));
    settings.transmit = flag ("transmit", settings.transmit);
    settings.monitor = flag ("monitor", settings.monitor);
    settings.localGain = number ("localGain", settings.localGain);
    settings.localPan = number ("localPan", settings.localPan);
    settings.localMute = flag ("localMute", settings.localMute);
    settings.metronomeGain = number ("metronomeGain", settings.metronomeGain);
    settings.metronomeMute = flag ("metronomeMute", settings.metronomeMute);
    settings.masterGain = number ("masterGain", settings.masterGain);
    settings.chatVisible = flag ("chatVisible", settings.chatVisible);

    remoteMix.clear();
    std::istringstream mix (text ("remoteMix", {}));
    std::string line;
    while (std::getline (mix, line))
    {
        std::istringstream fields (line);
        std::string key;
        StripMemory memory;
        int mute = 0;
        if (std::getline (fields, key, '\t') && (fields >> memory.gain >> memory.pan >> mute))
        {
            memory.mute = mute != 0;
            remoteMix[key] = memory;
        }
    }

    applySettings();
    return yup::Result::ok();
}

} // namespace jamtaba

//==============================================================================
extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new jamtaba::JamProcessor();
}
