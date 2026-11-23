/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "engine/audio/SpscQueue.h"

#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <vector>

namespace jamtaba::audio
{

/** One received interval, decoded and resampled to the device rate.
    The engine thread appends frames and then publishes framesReady; the audio
    thread only reads frames below framesReady. */
struct RemoteInterval
{
    explicit RemoteInterval (int capacityFrames)
        : left (static_cast<size_t> (capacityFrames)), right (static_cast<size_t> (capacityFrames))
    {
    }

    int capacity() const noexcept { return static_cast<int> (left.size()); }

    std::vector<float> left, right;
    std::atomic<int> framesReady { 0 };

    // Engine thread bookkeeping
    bool inAudio = false; // handed to the audio thread and not yet given back
    bool downloading = true;
};

/** Peak (held until read) and smoothed RMS for one stereo signal. */
struct Meter
{
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };
    std::atomic<float> rmsLeft { 0.0f }, rmsRight { 0.0f };

    /** Audio thread: folds in one block's peaks and sums of squares. */
    void update (float blockPeakLeft, float blockPeakRight, float sumSquaresLeft, float sumSquaresRight,
                 int numFrames, float smoothing) noexcept
    {
        atomicMax (peakLeft, blockPeakLeft);
        atomicMax (peakRight, blockPeakRight);
        if (numFrames <= 0)
            return;
        const float n = static_cast<float> (numFrames);
        meanSquares[0] = smoothing * meanSquares[0] + (1.0f - smoothing) * sumSquaresLeft / n;
        meanSquares[1] = smoothing * meanSquares[1] + (1.0f - smoothing) * sumSquaresRight / n;
        rmsLeft.store (std::sqrt (meanSquares[0]), std::memory_order_relaxed);
        rmsRight.store (std::sqrt (meanSquares[1]), std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        peakLeft.store (0.0f);
        peakRight.store (0.0f);
        rmsLeft.store (0.0f);
        rmsRight.store (0.0f);
        meanSquares[0] = meanSquares[1] = 0.0f;
    }

    /** Reads the peaks held since the last call. */
    void readAndResetPeaks (float& left, float& right) noexcept
    {
        left = peakLeft.exchange (0.0f);
        right = peakRight.exchange (0.0f);
    }

private:
    static void atomicMax (std::atomic<float>& target, float value) noexcept
    {
        auto current = target.load (std::memory_order_relaxed);
        while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {
        }
    }

    float meanSquares[2] {}; // audio thread only
};

/** Gain, pan, mute, solo: set from any thread, read by the audio thread. */
struct StripControls
{
    std::atomic<float> gain { 1.0f };
    std::atomic<float> pan { 0.0f }; // -1 .. 1
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };
};

/** A remote user's channel as the audio thread plays it. */
struct RemoteSlot
{
    enum State : int
    {
        free,
        active,
        releasing, // the engine thread let go; the audio thread hands back its intervals, then frees it
    };

    std::atomic<int> state { free };
    SpscQueue<RemoteInterval*> queue { 8 }; // engine thread -> audio thread; nullptr is a silent interval
    StripControls controls;
    Meter meter;

    // Audio thread only
    RemoteInterval* current = nullptr;
    int readPosition = 0;
};

/** A local input channel. */
struct LocalChannel
{
    std::atomic<int> firstInput { 0 };
    std::atomic<bool> stereo { true };
    std::atomic<bool> transmit { true }; // applied at the next interval
    std::atomic<bool> monitor { true };
    StripControls controls;
    Meter meter;
};

/** Captured local audio on its way to the encoder. */
struct InputChunk
{
    static constexpr int maxFrames = 256;
    static constexpr int maxChannels = 4;

    enum Flags : std::uint32_t
    {
        intervalStart = 1, // the first chunk of a new interval; numFrames may be 0
    };

    std::uint32_t flags = 0;
    std::uint32_t transmitMask = 0; // local channels sending this interval
    int numFrames = 0;
    float samples[maxChannels][2][maxFrames] {};
};

//==============================================================================
/** Everything that runs on the audio thread: the interval clock, the remote
    channel playback, local capture and monitoring, the metronome and the mix.
    process() takes no locks and allocates nothing. */
class AudioEngine
{
public:
    static constexpr int maxRemoteSlots = 64;
    static constexpr int maxLocalChannels = InputChunk::maxChannels;

    AudioEngine();

    /** Not real-time: call before processing starts or while it is stopped. */
    void prepare (double sampleRate, int maxBlockSize, int numInputs);

    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs, int numFrames) noexcept;

    //==============================================================================
    // Engine thread

    /** The server's tempo. The first call starts the clock at an interval
        start; later calls take effect at the next interval. */
    void setTempo (int bpm, int bpi) noexcept;

    /** Stops the clock; process() then only monitors the local inputs. */
    void stopClock() noexcept;

    RemoteSlot& getSlot (int index) noexcept { return slots[static_cast<size_t> (index)]; }
    bool popInputChunk (InputChunk& chunk) noexcept { return inputChunks.pop (chunk); }
    bool popRetired (RemoteInterval*& interval) noexcept { return retired.pop (interval); }
    int getInputChunkOverflows() const noexcept { return inputOverflows.load(); }

    //==============================================================================
    // Any thread

    double getSampleRate() const noexcept { return sampleRateAtomic.load(); }
    bool isClockRunning() const noexcept { return clockRunning.load(); }
    int getBpm() const noexcept { return bpmAtomic.load(); }
    int getBpi() const noexcept { return bpiAtomic.load(); }
    int getSamplesPerInterval() const noexcept { return samplesPerIntervalAtomic.load(); }
    int getIntervalPosition() const noexcept { return positionAtomic.load(); }
    int getCurrentBeat() const noexcept { return beatAtomic.load(); }
    std::uint32_t getIntervalCount() const noexcept { return intervalCount.load(); }

    LocalChannel& getLocalChannel (int index) noexcept { return locals[static_cast<size_t> (index)]; }
    void setNumLocalChannels (int count) noexcept;
    int getNumLocalChannels() const noexcept { return numLocals.load(); }

    StripControls& getMetronome() noexcept { return metronome; }
    Meter& getMetronomeMeter() noexcept { return metronomeMeter; }
    std::atomic<int>& getMetronomeAccentEvery() noexcept { return accentEvery; } // 0: accent only the first beat
    StripControls& getMaster() noexcept { return master; }
    Meter& getMasterMeter() noexcept { return masterMeter; }

private:
    void processChunk (int numFrames) noexcept;
    void startInterval() noexcept;
    void applyTempo (int bpm, int bpi) noexcept;
    void mixRemote (int offset, int numFrames, bool anySolo) noexcept;
    void handleLocals (int offset, int numFrames, bool anySolo) noexcept;
    void mixMetronome (int offset, int numFrames) noexcept;
    void pushCapture (int numFrames, int offset) noexcept;
    void flushCapture() noexcept;
    void retire (RemoteInterval* interval) noexcept;
    void serviceReleasingSlots() noexcept;

    std::array<RemoteSlot, maxRemoteSlots> slots;
    std::array<LocalChannel, maxLocalChannels> locals;
    std::atomic<int> numLocals { 1 };

    SpscQueue<InputChunk> inputChunks;
    SpscQueue<RemoteInterval*> retired;
    std::atomic<int> inputOverflows { 0 };

    StripControls metronome, master;
    Meter metronomeMeter, masterMeter;
    std::atomic<int> accentEvery { 0 };

    // Engine thread -> audio thread
    std::atomic<std::uint32_t> pendingTempo { 0 }; // bpm << 16 | bpi, 0 when none
    std::atomic<bool> stopRequested { false };

    // Published
    std::atomic<double> sampleRateAtomic { 44100.0 };
    std::atomic<bool> clockRunning { false };
    std::atomic<int> bpmAtomic { 0 }, bpiAtomic { 0 };
    std::atomic<int> samplesPerIntervalAtomic { 0 };
    std::atomic<int> positionAtomic { 0 };
    std::atomic<int> beatAtomic { 0 };
    std::atomic<std::uint32_t> intervalCount { 0 };

    // Audio thread state
    double sampleRate = 44100.0;
    int maxBlock = 512;
    int numDeviceInputs = 0;
    bool running = false;
    int bpm = 120, bpi = 16;
    int samplesPerInterval = 0;
    double samplesPerBeat = 0.0;
    int position = 0;
    int beat = 0;
    std::int64_t nextBeatSample = 0;
    int clickPosition = -1;
    bool clickAccent = false;
    std::uint32_t transmitMask = 0;

    std::vector<std::vector<float>> inputScratch; // device inputs, copied before the outputs are written
    std::vector<float> mixLeft, mixRight;
    std::vector<float> clickAccentSound, clickBeatSound;
    InputChunk capture;

};

} // namespace jamtaba::audio
