/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

/*  ninjam_cli: the headless harness for the engine.

      ninjam_cli selftest
      ninjam_cli servers
      ninjam_cli connect HOST:PORT --name NAME [options]

    connect drives the engine with a simulated audio device running in real
    time (48 kHz, stereo in and out). Options:

      --password P         log in with a password instead of anonymously
      --seconds N          stay connected for N seconds (default 30)
      --tone HZ            send a sine at HZ as the local channel (default: silence)
      --no-transmit        don't send anything
      --metronome          hear the metronome (off by default)
      --chat TEXT          send a chat message once connected
      --vote-bpm N         vote for a BPM once connected
      --vote-bpi N         vote for a BPI once connected
      --accept-licence     accept the server's licence agreement if it shows one
      --expect-tone HZ     exit with failure unless a tone near HZ is received
      --expect-bpm N       exit with failure unless the tempo ends up at N BPM
      --expect-chat TEXT   exit with failure unless a chat message with TEXT arrives
      --wav PATH           write what the device plays to a 16-bit WAV file
*/

#include "SelfTest.h"

#include "engine/JamEngine.h"
#include "engine/net/PublicServerList.h"

#include <yup_core/yup_core.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <numbers>
#include <vector>

namespace
{

using jamtaba::JamEngine;
using jamtaba::JamEvent;

void print (const std::string& text)
{
    std::printf ("%s\n", text.c_str());
    std::fflush (stdout);
}

std::string timestamp()
{
    return yup::Time::getCurrentTime().formatted ("%H:%M:%S ").toStdString();
}

//==============================================================================
/** Stands in for an audio interface: calls the engine's process() in blocks,
    paced by the clock, and keeps statistics of what comes out per interval. */
class VirtualDevice final : public yup::Thread
{
public:
    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 256;

    struct IntervalStats
    {
        std::uint32_t interval = 0;
        float peak = 0.0f;
        double frequency = 0.0;
    };

    VirtualDevice (jamtaba::audio::AudioEngine& engineToUse, double toneHz, bool keepAudio)
        : yup::Thread ("virtual audio device")
        , engine (engineToUse)
        , tone (toneHz)
        , keep (keepAudio)
    {
        engine.prepare (sampleRate, blockSize, 2);
    }

    ~VirtualDevice() override { stopThread (2000); }

    std::vector<IntervalStats> takeStats()
    {
        std::lock_guard lock (statsLock);
        return std::exchange (finished, {});
    }

    const std::vector<float>& getRecording() const { return recording; }

private:
    void run() override
    {
        std::vector<float> inL (blockSize), inR (blockSize), outL (blockSize), outR (blockSize);
        const float* inputs[2] { inL.data(), inR.data() };
        float* outputs[2] { outL.data(), outR.data() };

        const double start = yup::Time::getMillisecondCounterHiRes();
        std::int64_t rendered = 0;
        double phase = 0.0;
        std::uint32_t lastInterval = engine.getIntervalCount();

        while (! threadShouldExit())
        {
            // Keep up with the wall clock, like a sound card would demand
            const double due = (yup::Time::getMillisecondCounterHiRes() - start) / 1000.0 * sampleRate;
            if (static_cast<double> (rendered) > due)
            {
                yup::Thread::sleep (2);
                continue;
            }

            for (int i = 0; i < blockSize; ++i)
            {
                const float value = tone > 0.0 ? 0.4f * static_cast<float> (std::sin (phase)) : 0.0f;
                phase += 2.0 * std::numbers::pi * tone / sampleRate;
                if (phase > 2.0 * std::numbers::pi)
                    phase -= 2.0 * std::numbers::pi;
                inL[static_cast<size_t> (i)] = value;
                inR[static_cast<size_t> (i)] = value;
            }

            engine.process (inputs, 2, outputs, 2, blockSize);
            rendered += blockSize;

            const auto interval = engine.getIntervalCount();
            if (interval != lastInterval)
            {
                closeInterval (lastInterval);
                lastInterval = interval;
            }

            for (int i = 0; i < blockSize; ++i)
            {
                const float l = outL[static_cast<size_t> (i)];
                current.peak = std::max (current.peak, std::abs (l));
                if (previous < 0.0f && l >= 0.0f && std::abs (l - previous) < 0.5f)
                {
                    if (crossings == 0)
                        firstCrossing = sampleInInterval;
                    lastCrossing = sampleInInterval;
                    ++crossings;
                }
                previous = l;
                ++sampleInInterval;
                if (keep)
                {
                    recording.push_back (l);
                    recording.push_back (outR[static_cast<size_t> (i)]);
                }
            }
        }
    }

    void closeInterval (std::uint32_t interval)
    {
        current.interval = interval;
        if (crossings > 2)
            current.frequency = (crossings - 1) * sampleRate / static_cast<double> (lastCrossing - firstCrossing);
        {
            std::lock_guard lock (statsLock);
            finished.push_back (current);
        }
        current = {};
        crossings = 0;
        sampleInInterval = 0;
    }

    jamtaba::audio::AudioEngine& engine;
    const double tone;
    const bool keep;

    IntervalStats current;
    float previous = 0.0f;
    int crossings = 0;
    std::int64_t firstCrossing = 0, lastCrossing = 0, sampleInInterval = 0;
    std::vector<float> recording;

    std::mutex statsLock;
    std::vector<IntervalStats> finished;
};

bool writeWav (const std::string& path, const std::vector<float>& interleaved, double sampleRate)
{
    yup::File file { yup::String (path) };
    file.deleteFile();
    yup::FileOutputStream out (file);
    if (! out.openedOk())
        return false;

    const auto dataBytes = static_cast<yup::uint32> (interleaved.size() * 2);
    out.write ("RIFF", 4);
    out.writeInt (static_cast<int> (36 + dataBytes));
    out.write ("WAVEfmt ", 8);
    out.writeInt (16);
    out.writeShort (1);
    out.writeShort (2);
    out.writeInt (static_cast<int> (sampleRate));
    out.writeInt (static_cast<int> (sampleRate * 4));
    out.writeShort (4);
    out.writeShort (16);
    out.write ("data", 4);
    out.writeInt (static_cast<int> (dataBytes));
    for (const float sample : interleaved)
        out.writeShort (static_cast<short> (std::lround (std::clamp (sample, -1.0f, 1.0f) * 32767.0f)));
    return true;
}

//==============================================================================
std::string describe (const JamEvent& e)
{
    using T = JamEvent::Type;
    switch (e.type)
    {
        case T::connecting: return "connecting to " + e.text;
        case T::licence: return "licence agreement:\n" + e.text;
        case T::connected: return "connected as " + e.user + ", up to " + std::to_string (e.number) + " channels";
        case T::disconnected: return "disconnected" + (e.text.empty() ? std::string {} : ": " + e.text);
        case T::tempo: return "tempo " + std::to_string (e.number) + " BPM, " + std::to_string (e.number2) + " BPI";
        case T::channelAdded: return "channel added: " + e.user + " #" + std::to_string (e.channelIndex) + " \"" + e.channel + "\" (slot " + std::to_string (e.slot) + ")";
        case T::channelChanged: return "channel changed: " + e.user + " #" + std::to_string (e.channelIndex) + " \"" + e.channel + "\"";
        case T::channelRemoved: return "channel removed: " + e.user + " #" + std::to_string (e.channelIndex);
        case T::userLeft: return "user left: " + e.user;
        case T::chat: return "<" + (e.user.empty() ? std::string ("server") : e.user) + "> " + e.text;
        case T::privateChat: return "*" + e.user + "* " + e.text;
        case T::topic: return "topic: " + e.text;
        case T::userJoined: return "joined: " + e.user;
        case T::userParted: return "parted: " + e.user;
        case T::userCount: return "users " + std::to_string (e.number) + "/" + std::to_string (e.number2);
        case T::intervalReceived: return "received interval from " + e.user + " #" + std::to_string (e.channelIndex) + ", " + std::to_string (e.number) + " bytes";
        case T::intervalSent: return "sent interval on channel " + std::to_string (e.channelIndex) + ", " + std::to_string (e.number) + " bytes";
    }
    return {};
}

struct Options
{
    std::string host;
    int port = 2049;
    std::string name = "jamtabayup";
    std::string password;
    double seconds = 30.0;
    double tone = 0.0;
    bool transmit = true;
    bool metronome = false;
    bool acceptLicence = false;
    std::string chat;
    int voteBpm = 0, voteBpi = 0;
    double expectTone = 0.0;
    int expectBpm = 0;
    std::string expectChat;
    std::string wav;
};

int connectAndRun (const Options& options)
{
    JamEngine engine;
    auto& audio = engine.getAudio();
    audio.getMetronome().mute.store (! options.metronome);

    VirtualDevice device (audio, options.tone, ! options.wav.empty());

    jamtaba::ConnectionSettings settings;
    settings.host = options.host;
    settings.port = options.port;
    settings.userName = options.name;
    settings.password = options.password;
    settings.channels = { { options.tone > 0.0 ? "tone" : "silence", 0, true, options.transmit } };
    engine.connect (settings);
    audio.getLocalChannel (0).monitor.store (false);
    device.startThread();

    const double start = yup::Time::getMillisecondCounterHiRes() / 1000.0;
    bool sentRequests = false;
    bool disconnected = false;
    int lastBpm = 0, toneIntervals = 0, receivedIntervals = 0;
    bool chatSeen = false;

    while (yup::Time::getMillisecondCounterHiRes() / 1000.0 - start < options.seconds && ! disconnected)
    {
        engine.pollEvents ([&] (const JamEvent& e)
        {
            print (timestamp() + describe (e));
            switch (e.type)
            {
                case JamEvent::Type::licence:
                    if (! options.acceptLicence)
                        print ("declining: pass --accept-licence to accept it");
                    engine.acceptLicence (options.acceptLicence);
                    break;
                case JamEvent::Type::disconnected:
                    disconnected = true;
                    break;
                case JamEvent::Type::chat:
                    chatSeen = chatSeen || (! options.expectChat.empty() && e.text.find (options.expectChat) != std::string::npos);
                    break;
                case JamEvent::Type::intervalReceived:
                    ++receivedIntervals;
                    break;
                default:
                    break;
            }
        });

        if (engine.isConnected() && ! sentRequests && audio.isClockRunning())
        {
            sentRequests = true;
            if (! options.chat.empty())
                engine.sendChat (options.chat);
            if (options.voteBpm > 0)
                engine.voteBpm (options.voteBpm);
            if (options.voteBpi > 0)
                engine.voteBpi (options.voteBpi);
        }

        for (const auto& stats : device.takeStats())
        {
            if (stats.interval == 0)
                continue;
            print (timestamp() + yup::String::formatted ("interval %u played: peak %.3f, %.1f Hz", stats.interval, stats.peak, stats.frequency).toStdString());
            if (options.expectTone > 0.0 && stats.peak > 0.1f && std::abs (stats.frequency - options.expectTone) < options.expectTone * 0.03)
                ++toneIntervals;
        }

        lastBpm = audio.getBpm();
        yup::Thread::sleep (20);
    }

    engine.disconnect();
    engine.pollEvents ([] (const JamEvent& e) { print (timestamp() + describe (e)); });
    device.stopThread (2000);

    if (! options.wav.empty())
        print (writeWav (options.wav, device.getRecording(), VirtualDevice::sampleRate) ? "wrote " + options.wav : "could not write " + options.wav);

    bool ok = true;
    if (audio.getInputChunkOverflows() > 0)
        print ("capture overflowed " + std::to_string (audio.getInputChunkOverflows()) + " times");
    if (options.expectTone > 0.0)
    {
        print ("intervals with the expected tone: " + std::to_string (toneIntervals) + ", intervals received: " + std::to_string (receivedIntervals));
        ok = ok && toneIntervals >= 2;
    }
    if (options.expectBpm > 0)
    {
        print ("final tempo: " + std::to_string (lastBpm) + " BPM");
        ok = ok && lastBpm == options.expectBpm;
    }
    if (! options.expectChat.empty())
    {
        print (std::string ("expected chat ") + (chatSeen ? "seen" : "NOT seen"));
        ok = ok && chatSeen;
    }

    print (ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int listServers()
{
    const auto servers = jamtaba::net::fetchPublicServerList();
    if (! servers)
    {
        print ("could not fetch " + std::string (jamtaba::net::publicServerListUrl));
        return 1;
    }
    for (const auto& s : *servers)
        print (yup::String::formatted ("%-30s users %2d/%-2d  %3d BPM  %2d BPI", s.address().c_str(), s.userCount, s.maxUsers, s.bpm, s.bpi).toStdString());
    print (std::to_string (servers->size()) + " servers");
    return servers->empty() ? 1 : 0;
}

int usage()
{
    print (R"(usage:
  ninjam_cli selftest
  ninjam_cli servers
  ninjam_cli connect HOST:PORT --name NAME [options]

connect options:
  --password P         log in with a password instead of anonymously
  --seconds N          stay connected for N seconds (default 30)
  --tone HZ            send a sine at HZ as the local channel (default: silence)
  --no-transmit        don't send anything
  --metronome          hear the metronome (off by default)
  --chat TEXT          send a chat message once connected
  --vote-bpm N         vote for a BPM once connected
  --vote-bpi N         vote for a BPI once connected
  --accept-licence     accept the server's licence agreement if it shows one
  --expect-tone HZ     fail unless a tone near HZ is received
  --expect-bpm N       fail unless the tempo ends up at N BPM
  --expect-chat TEXT   fail unless a chat message containing TEXT arrives
  --wav PATH           write what the device plays to a 16-bit WAV file)");
    return 2;
}

} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    std::vector<std::string> args (argv + 1, argv + argc);
    if (args.empty())
        return usage();

    if (args[0] == "selftest")
        return jamtaba::cli::runSelfTest();
    if (args[0] == "servers")
        return listServers();
    if (args[0] != "connect" || args.size() < 2)
        return usage();

    Options options;
    const auto address = yup::String (args[1]);
    options.host = address.upToLastOccurrenceOf (":", false, false).toStdString();
    options.port = address.containsChar (':') ? address.fromLastOccurrenceOf (":", false, false).getIntValue() : 2049;

    for (size_t i = 2; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string {}; };
        if (a == "--name") options.name = next();
        else if (a == "--password") options.password = next();
        else if (a == "--seconds") options.seconds = std::stod (next());
        else if (a == "--tone") options.tone = std::stod (next());
        else if (a == "--no-transmit") options.transmit = false;
        else if (a == "--metronome") options.metronome = true;
        else if (a == "--chat") options.chat = next();
        else if (a == "--vote-bpm") options.voteBpm = std::stoi (next());
        else if (a == "--vote-bpi") options.voteBpi = std::stoi (next());
        else if (a == "--accept-licence") options.acceptLicence = true;
        else if (a == "--expect-tone") options.expectTone = std::stod (next());
        else if (a == "--expect-bpm") options.expectBpm = std::stoi (next());
        else if (a == "--expect-chat") options.expectChat = next();
        else if (a == "--wav") options.wav = next();
        else
        {
            print ("unknown option " + a);
            return usage();
        }
    }

    if (options.host.empty() || options.port <= 0)
        return usage();

    return connectAndRun (options);
}
