/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "SelfTest.h"

#include "engine/audio/AudioEngine.h"
#include "engine/codec/VorbisCodec.h"
#include "engine/net/PublicServerList.h"
#include "engine/protocol/Messages.h"

#include <yup_core/yup_core.h>

#include <cmath>
#include <cstdio>
#include <numbers>

namespace jamtaba::cli
{

namespace
{
int failures = 0;
int checks = 0;

void expect (bool condition, const char* what)
{
    ++checks;
    if (! condition)
    {
        ++failures;
        std::printf ("  FAILED: %s\n", what);
    }
}

std::string hex (std::span<const std::uint8_t> bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (auto b : bytes)
    {
        text += digits[b >> 4];
        text += digits[b & 15];
    }
    return text;
}

protocol::ByteView view (const protocol::Message& m)
{
    return protocol::ByteView (m.payload.data(), m.payload.size());
}

/** Frequency of a sine from its rising zero crossings. */
double estimateFrequency (const std::vector<float>& samples, double sampleRate)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = 1; i < samples.size(); ++i)
    {
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
        {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    }
    if (crossings < 2)
        return 0.0;
    return (crossings - 1) * sampleRate / static_cast<double> (last - first);
}

//==============================================================================
void testMessages()
{
    using namespace protocol;
    std::printf ("protocol messages\n");

    std::vector<UserChannelInfo> channels {
        { true, 0, -30, -64, 0, "alice@1.2.3.x", "guitar" },
        { false, 3, 10, 127, channelFlags::voiceChat, "bob", "" },
    };
    const auto parsedChannels = parseUserInfoChange (view (makeUserInfoChange (channels)));
    expect (parsedChannels && parsedChannels->size() == 2, "user info: two records");
    if (parsedChannels && parsedChannels->size() == 2)
    {
        const auto& a = (*parsedChannels)[0];
        const auto& b = (*parsedChannels)[1];
        expect (a.active && a.channelIndex == 0 && a.volume == -30 && a.pan == -64 && a.userName == "alice@1.2.3.x" && a.channelName == "guitar", "user info: first record");
        expect (! b.active && b.channelIndex == 3 && b.volume == 10 && b.pan == 127 && b.flags == channelFlags::voiceChat && b.userName == "bob" && b.channelName.empty(), "user info: second record");
    }

    auto truncated = makeUserInfoChange (channels).payload;
    truncated.resize (truncated.size() - 3);
    const auto partial = parseUserInfoChange (ByteView (truncated.data(), truncated.size()));
    expect (partial && partial->size() == 1, "user info: a cut-off record is dropped");

    DownloadIntervalBegin begin;
    for (std::size_t i = 0; i < begin.guid.size(); ++i)
        begin.guid[i] = static_cast<std::uint8_t> (i * 7 + 1);
    begin.fourcc = fourccOggVorbis;
    begin.channelIndex = 5;
    begin.userName = "carol";
    const auto parsedBegin = parseDownloadIntervalBegin (view (makeDownloadIntervalBegin (begin)));
    expect (parsedBegin && parsedBegin->guid == begin.guid && parsedBegin->fourcc == fourccOggVorbis
                && parsedBegin->channelIndex == 5 && parsedBegin->userName == "carol",
            "download begin round trip");

    const auto upload = makeUploadIntervalBegin (begin.guid, fourccOggVorbis, 2);
    expect (upload.payload.size() == 25 && upload.payload[24] == 2 && upload.payload[20] == 'O' && upload.payload[23] == 'v', "upload begin layout");

    const std::vector<std::uint8_t> audio { 1, 2, 3, 4, 5 };
    const auto write = parseIntervalWrite (view (makeUploadIntervalWrite (begin.guid, ByteView (audio.data(), audio.size()), true)));
    expect (write && write->guid == begin.guid && write->last && write->data == audio, "interval write round trip");

    const auto chat = parseChatMessage (view (makeChatMessage ({ "MSG", "dave", "hello there" })));
    expect (chat && chat->params[0] == "MSG" && chat->params[1] == "dave" && chat->params[2] == "hello there" && chat->params[3].empty(), "chat round trip");

    AuthChallenge challenge;
    challenge.challenge = { 1, 2, 3, 4, 5, 6, 7, 8 };
    challenge.serverCapabilities = 30u << 8;
    challenge.protocolVersion = protocolVersionCurrent;
    challenge.licenceAgreement = "Be nice.";
    const auto parsedChallenge = parseAuthChallenge (view (makeAuthChallenge (challenge)));
    expect (parsedChallenge && parsedChallenge->keepAliveSeconds() == 30 && parsedChallenge->licenceAgreement == std::optional<std::string> ("Be nice.")
                && parsedChallenge->protocolVersion == protocolVersionCurrent,
            "auth challenge round trip");
    expect (! parseAuthChallenge (ByteView (challenge.challenge.data(), 8)), "short auth challenge is rejected");

    const std::uint8_t reply[] = { 1, 'e', 'v', 'e', 0, 4 };
    const auto parsedReply = parseAuthReply (ByteView (reply, sizeof (reply)));
    expect (parsedReply && parsedReply->succeeded && parsedReply->message == "eve" && parsedReply->maxChannels == 4, "auth reply parse");

    const auto framed = frame (makeKeepAlive());
    expect (framed.size() == 5 && framed[0] == 0xfd && framed[1] == 0, "keepalive framing");

    const auto channelInfo = makeSetChannelInfo ({ { "mic", 0, -200, 0 } });
    expect (channelInfo.payload.size() == 2 + 4 + 4 && channelInfo.payload[0] == 4 && channelInfo.payload[8] == 0x80, "channel info layout, pan clamped");
}

void testAuth()
{
    std::printf ("auth hash\n");
    const std::array<std::uint8_t, 8> challenge { 1, 2, 3, 4, 5, 6, 7, 8 };
    expect (hex (protocol::authPassHash ("anonymous:bob", "", challenge)) == "2a5133a2bc5a2afe5769a041333de901c1dcbecf", "anonymous login hash");
    expect (hex (protocol::authPassHash ("alice", "s3cret", challenge)) == "9df86c5baac1329c36b0fe3bc20e9bd3067b737f", "password login hash");
}

void testServerList()
{
    std::printf ("server list parsing\n");
    const std::string json = R"({"servers":[
        {"host":"ninbot.com","port":"2049","bpi":"16","bpm":"100","name":"ninbot.com:2049","user_count":"2","user_max":"13","users":[{"name":"ninbot"},{"name":"amy","lat":"41.3","lon":"-13.1","country":"Italy"},{"name":"bo","lat":"NaN","lon":"200"}]},
        {"name":"old.example.org:2051","bpm":"90","bpi":"8","user_max":"NaN"},
        {"name":"broken"}]})";
    const auto list = net::parsePublicServerList (json);
    expect (list && list->size() == 2, "two usable servers");
    if (list && list->size() == 2)
    {
        const auto& a = (*list)[0];
        const auto& b = (*list)[1];
        expect (a.host == "ninbot.com" && a.port == 2049 && a.bpm == 100 && a.bpi == 16 && a.userCount == 2 && a.maxUsers == 13 && a.users.size() == 3 && a.users[1].name == "amy", "full entry");
        if (a.users.size() == 3)
        {
            const auto& amy = a.users[1];
            expect (amy.hasLocation() && std::abs (*amy.latitude - 41.3f) < 1.0e-4f && std::abs (*amy.longitude + 13.1f) < 1.0e-4f && amy.country == "Italy", "user location parsed");
            expect (! a.users[0].hasLocation() && ! a.users[2].hasLocation(), "missing or invalid locations are dropped");
        }
        expect (b.host == "old.example.org" && b.port == 2051 && b.maxUsers == 0, "name-only entry with NaN max users");
    }
    expect (! net::parsePublicServerList ("not json"), "garbage is rejected");
}

void testCodec()
{
    std::printf ("vorbis streaming + resampling\n");
    constexpr int sourceRate = 44100;
    constexpr double targetRate = 48000.0;
    constexpr int frames = sourceRate * 3;

    std::vector<float> left (frames), right (frames);
    for (int i = 0; i < frames; ++i)
    {
        left[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 440.0 * i / sourceRate));
        right[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 660.0 * i / sourceRate));
    }

    codec::VorbisEncoder encoder (2, sourceRate, 0.3f);
    expect (encoder.isValid(), "encoder initialises");
    std::vector<std::uint8_t> encoded;
    for (int i = 0; i < frames; i += 1000)
    {
        const float* in[2] { left.data() + i, right.data() + i };
        encoder.encode (in, std::min (1000, frames - i), encoded);
    }
    encoder.finish (encoded);

    // Feed the decoder in uneven pieces, as the network would
    codec::VorbisDecoder decoder;
    std::unique_ptr<codec::StreamResampler> resampler;
    std::vector<float> decodedLeft, decodedRight, outLeft, outRight;
    yup::Random random (42);
    for (std::size_t offset = 0; offset < encoded.size();)
    {
        const auto size = std::min (encoded.size() - offset, static_cast<std::size_t> (1 + random.nextInt (5000)));
        decoder.push (encoded.data() + offset, size);
        offset += size;

        decodedLeft.clear();
        decodedRight.clear();
        decoder.pull (decodedLeft, decodedRight);
        if (resampler == nullptr && decoder.getSampleRate() > 0)
            resampler = std::make_unique<codec::StreamResampler> (decoder.getSampleRate(), targetRate);
        if (resampler != nullptr)
            resampler->process (decodedLeft.data(), decodedRight.data(), static_cast<int> (decodedLeft.size()), outLeft, outRight);
    }
    if (resampler != nullptr)
        resampler->flush (outLeft, outRight);

    expect (! decoder.hasFailed() && decoder.getSampleRate() == sourceRate && decoder.getNumChannels() == 2, "decoder reads the headers");
    const double expectedFrames = frames * targetRate / sourceRate;
    expect (std::abs (static_cast<double> (outLeft.size()) - expectedFrames) < 64.0, "resampled length matches");
    const auto f1 = estimateFrequency (outLeft, targetRate);
    const auto f2 = estimateFrequency (outRight, targetRate);
    std::printf ("  %zu bytes, %zu frames at 48 kHz, %.1f Hz / %.1f Hz\n", encoded.size(), outLeft.size(), f1, f2);
    expect (std::abs (f1 - 440.0) < 2.0 && std::abs (f2 - 660.0) < 2.0, "pitch survives decoding and resampling");
}

void testAudioEngine()
{
    std::printf ("audio engine clock, capture and playback\n");
    constexpr double rate = 48000.0;
    constexpr int block = 480;

    audio::AudioEngine engine;
    engine.prepare (rate, block, 2);
    engine.setNumLocalChannels (1);
    engine.getMetronome().mute.store (true);
    engine.getLocalChannel (0).monitor.store (false);

    std::vector<float> inL (block, 0.25f), inR (block, -0.25f), outL (block), outR (block);
    const float* inputs[2] { inL.data(), inR.data() };
    float* outputs[2] { outL.data(), outR.data() };

    engine.process (inputs, 2, outputs, 2, block);
    expect (! engine.isClockRunning(), "clock waits for the server's tempo");

    engine.setTempo (120, 4); // 2 s intervals
    auto& slot = engine.getSlot (0);
    slot.state.store (audio::RemoteSlot::active);

    // A full interval of 0.5 waiting to play
    auto interval = std::make_unique<audio::RemoteInterval> (96000);
    std::fill (interval->left.begin(), interval->left.end(), 0.5f);
    std::fill (interval->right.begin(), interval->right.end(), 0.5f);
    interval->framesReady.store (96000);

    engine.process (inputs, 2, outputs, 2, block); // first interval starts here
    expect (engine.isClockRunning() && engine.getSamplesPerInterval() == 96000, "tempo applied: 96000 samples per interval");
    expect (outL[0] == 0.0f, "nothing plays in the first interval");

    slot.queue.push (interval.get());

    int capturedFrames = 0, starts = 0;
    float lastOut = 0.0f;
    audio::InputChunk chunk;
    for (int i = 1; i < 96000 / block + 10; ++i)
    {
        engine.process (inputs, 2, outputs, 2, block);
        lastOut = outL[block - 1];
        while (engine.popInputChunk (chunk))
        {
            if (chunk.flags & audio::InputChunk::intervalStart)
                ++starts;
            capturedFrames += chunk.numFrames;
            expect (chunk.numFrames == 0 || chunk.samples[0][0][0] == 0.25f, "captured audio is the input");
        }
    }
    expect (engine.getIntervalCount() == 2, "second interval started");
    expect (std::abs (lastOut - 0.5f) < 1.0e-6f, "queued interval plays in the next interval");
    expect (starts == 2 && capturedFrames >= 96000, "capture marks interval starts and carries every frame");

    audio::RemoteInterval* retired = nullptr;
    slot.state.store (audio::RemoteSlot::releasing);
    engine.process (inputs, 2, outputs, 2, block);
    expect (engine.popRetired (retired) && retired == interval.get(), "released slot hands its interval back");
    expect (slot.state.load() == audio::RemoteSlot::free, "released slot becomes free");

    float peakL = 0.0f, peakR = 0.0f;
    slot.meter.readAndResetPeaks (peakL, peakR);
    engine.getMasterMeter().readAndResetPeaks (peakL, peakR);
    expect (peakL > 0.49f, "master meter saw the remote audio");
}
} // namespace

int runSelfTest()
{
    testMessages();
    testAuth();
    testServerList();
    testCodec();
    testAudioEngine();

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

} // namespace jamtaba::cli
