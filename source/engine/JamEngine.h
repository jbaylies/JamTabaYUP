/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "engine/audio/AudioEngine.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace jamtaba
{

/** What happened, for whoever shows it (the UI, the console harness). */
struct JamEvent
{
    enum class Type
    {
        connecting,       // text: host:port
        licence,          // text: the server's licence; answer with acceptLicence()
        connected,        // user: our name as the server knows it; number: max channels
        disconnected,     // text: why, empty when we asked for it
        tempo,            // number: bpm, number2: bpi (takes effect at the next interval)
        channelAdded,     // user, channel (name), channelIndex, slot
        channelChanged,   // user, channel, channelIndex, slot
        channelRemoved,   // user, channelIndex, slot
        userLeft,         // user: their last channel went away
        chat,             // user (empty for the server), text
        privateChat,      // user, text
        topic,            // user, text
        userJoined,       // user (chat JOIN)
        userParted,       // user (chat PART)
        userCount,        // number: users, number2: max users
        intervalReceived, // user, channelIndex, slot, number: bytes
        intervalSent,     // channelIndex, number: bytes
    };

    Type type = Type::connecting;
    std::string user;
    std::string channel;
    std::string text;
    int channelIndex = -1;
    int slot = -1;
    int number = 0;
    int number2 = 0;
};

struct LocalChannelSetup
{
    std::string name = "channel";
    int firstInput = 0;
    bool stereo = true;
    bool transmit = true;
};

struct ConnectionSettings
{
    std::string host;
    int port = 2049;
    std::string userName;
    std::string password; // empty: log in anonymously
    std::vector<LocalChannelSetup> channels { LocalChannelSetup {} };

    /** Vorbis VBR quality for uploads, -0.1 .. 1. */
    float encoderQuality = 0.3f;
};

//==============================================================================
/** The NINJAM client: one engine thread runs the connection, the session
    state, the Vorbis decoding of received intervals and the encoding of ours.
    The audio thread side is getAudio(), driven by the owner's audio callback.

    Call commands from any thread except the audio thread. Events queue up
    until pollEvents() is called, normally from the message thread. */
class JamEngine
{
public:
    JamEngine();

    /** The audio callback must have stopped before the engine is destroyed. */
    ~JamEngine();

    audio::AudioEngine& getAudio() noexcept { return audioEngine; }

    void connect (const ConnectionSettings& settings);
    void disconnect();
    bool isConnected() const noexcept { return connected.load(); }
    bool isRunning() const noexcept;

    void acceptLicence (bool accept);
    void sendChat (const std::string& text);
    void sendPrivateChat (const std::string& user, const std::string& text);
    void voteBpm (int bpm);
    void voteBpi (int bpi);
    void setReceiving (const std::string& user, int channelIndex, bool receive);
    void setTransmitting (int localChannel, bool transmit);

    void pollEvents (const std::function<void (const JamEvent&)>& handler);

private:
    class Worker;
    friend class Worker;

    void post (std::function<void (Worker&)> command);

    audio::AudioEngine audioEngine;
    std::unique_ptr<Worker> worker;
    std::atomic<bool> connected { false };

    std::mutex eventsLock;
    std::deque<JamEvent> events;

    /** Intervals outlive connections: the audio thread may still hold some. */
    std::vector<std::unique_ptr<audio::RemoteInterval>> intervals;

    void pushEvent (JamEvent event);
    void collectRetiredIntervals();
};

} // namespace jamtaba
