/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "engine/audio/AudioEngine.h"

#include <yup_audio_basics/yup_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace jamtaba::audio
{

namespace
{
constexpr double meterTimeSeconds = 0.3;

/** Balance pan: the far side fades out, the near side stays at full gain. */
void panGains (float gain, float pan, float& left, float& right) noexcept
{
    left = gain * std::min (1.0f, 1.0f - pan);
    right = gain * std::min (1.0f, 1.0f + pan);
}

std::vector<float> makeClick (double sampleRate, double frequency, double seconds, float level)
{
    const auto length = static_cast<size_t> (sampleRate * seconds);
    std::vector<float> sound (length);
    for (size_t i = 0; i < length; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const double envelope = std::exp (-t / (seconds * 0.22)) * std::min (1.0, t * 2000.0);
        sound[i] = level * static_cast<float> (envelope * std::sin (2.0 * std::numbers::pi * frequency * t));
    }
    return sound;
}
} // namespace

AudioEngine::AudioEngine()
    : inputChunks (512)
    , retired (1024)
{
    metronome.gain.store (0.5f);
}

void AudioEngine::prepare (double newSampleRate, int maxBlockSize, int numInputs)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxBlock = std::max (32, maxBlockSize);
    numDeviceInputs = std::max (0, numInputs);

    inputScratch.assign (static_cast<size_t> (numDeviceInputs), std::vector<float> (static_cast<size_t> (maxBlock)));
    mixLeft.assign (static_cast<size_t> (maxBlock), 0.0f);
    mixRight.assign (static_cast<size_t> (maxBlock), 0.0f);

    clickAccentSound = makeClick (sampleRate, 1760.0, 0.05, 0.9f);
    clickBeatSound = makeClick (sampleRate, 1320.0, 0.04, 0.6f);
    clickPosition = -1;

    capture.numFrames = 0;
    sampleRateAtomic.store (sampleRate);

    if (running)
        applyTempo (bpm, bpi);
}

void AudioEngine::setNumLocalChannels (int count) noexcept
{
    numLocals.store (std::clamp (count, 0, maxLocalChannels));
}

void AudioEngine::setTempo (int newBpm, int newBpi) noexcept
{
    newBpm = std::clamp (newBpm, 1, 0xffff);
    newBpi = std::clamp (newBpi, 1, 0xffff);
    pendingTempo.store ((static_cast<std::uint32_t> (newBpm) << 16) | static_cast<std::uint32_t> (newBpi));
}

void AudioEngine::stopClock() noexcept
{
    pendingTempo.store (0);
    stopRequested.store (true);
}

void AudioEngine::applyTempo (int newBpm, int newBpi) noexcept
{
    bpm = newBpm;
    bpi = newBpi;
    samplesPerBeat = sampleRate * 60.0 / static_cast<double> (bpm);
    samplesPerInterval = std::max (1, static_cast<int> (std::lround (samplesPerBeat * bpi)));
    bpmAtomic.store (bpm);
    bpiAtomic.store (bpi);
    samplesPerIntervalAtomic.store (samplesPerInterval);
}

//==============================================================================
void AudioEngine::process (const float* const* inputs, int numInputs,
                           float* const* outputs, int numOutputs, int numFrames) noexcept
{
    // Decaying meters, clicks and quiet remote audio reach denormal range,
    // which is slow on x86. Flush them to zero for the whole callback.
    const yup::ScopedNoDenormals noDenormals;

    int done = 0;
    while (done < numFrames)
    {
        const int frames = std::min (maxBlock, numFrames - done);

        // Inputs and outputs may share memory, so the inputs are copied first
        const int inputsToCopy = std::min (numInputs, numDeviceInputs);
        for (int channel = 0; channel < numDeviceInputs; ++channel)
        {
            auto* dest = inputScratch[static_cast<size_t> (channel)].data();
            if (channel < inputsToCopy && inputs[channel] != nullptr)
                std::copy_n (inputs[channel] + done, frames, dest);
            else
                std::fill_n (dest, frames, 0.0f);
        }

        processChunk (frames);

        for (int channel = 0; channel < numOutputs; ++channel)
        {
            if (outputs[channel] == nullptr)
                continue;
            const auto& source = (channel % 2 == 0) ? mixLeft : mixRight;
            if (channel < 2)
                std::copy_n (source.data(), frames, outputs[channel] + done);
            else
                std::fill_n (outputs[channel] + done, frames, 0.0f);
        }

        done += frames;
    }
}

void AudioEngine::processChunk (int numFrames) noexcept
{
    std::fill_n (mixLeft.data(), numFrames, 0.0f);
    std::fill_n (mixRight.data(), numFrames, 0.0f);

    serviceReleasingSlots();

    if (stopRequested.exchange (false))
    {
        running = false;
        clockRunning.store (false);
        capture.numFrames = 0;
        for (auto& slot : slots)
        {
            if (slot.state.load() == RemoteSlot::active)
            {
                retire (slot.current);
                slot.current = nullptr;
            }
        }
    }

    if (! running)
    {
        if (const auto tempo = pendingTempo.exchange (0); tempo != 0)
        {
            applyTempo (static_cast<int> (tempo >> 16), static_cast<int> (tempo & 0xffff));
            running = true;
            position = 0;
            clockRunning.store (true);
        }
    }

    bool anySolo = false;
    for (const auto& slot : slots)
        anySolo = anySolo || (slot.state.load (std::memory_order_relaxed) == RemoteSlot::active && slot.controls.solo.load (std::memory_order_relaxed));
    for (int i = 0; i < numLocals.load (std::memory_order_relaxed); ++i)
        anySolo = anySolo || locals[static_cast<size_t> (i)].controls.solo.load (std::memory_order_relaxed);

    if (! running)
    {
        handleLocals (0, numFrames, anySolo);
    }
    else
    {
        int offset = 0;
        while (offset < numFrames)
        {
            if (position == 0)
                startInterval();

            const int frames = std::min (numFrames - offset, samplesPerInterval - position);
            mixRemote (offset, frames, anySolo);
            handleLocals (offset, frames, anySolo);
            mixMetronome (offset, frames);

            offset += frames;
            position += frames;
            if (position >= samplesPerInterval)
                position = 0;
        }

        positionAtomic.store (position, std::memory_order_relaxed);
        beatAtomic.store (std::min (bpi - 1, static_cast<int> (static_cast<double> (position) / samplesPerBeat)), std::memory_order_relaxed);
    }

    // Master
    const float smoothing = static_cast<float> (std::exp (-numFrames / (meterTimeSeconds * sampleRate)));
    float gainLeft = 0.0f, gainRight = 0.0f;
    panGains (master.mute.load (std::memory_order_relaxed) ? 0.0f : master.gain.load (std::memory_order_relaxed),
              master.pan.load (std::memory_order_relaxed), gainLeft, gainRight);
    float peaks[2] {}, sums[2] {};
    for (int i = 0; i < numFrames; ++i)
    {
        const float l = mixLeft[static_cast<size_t> (i)] * gainLeft;
        const float r = mixRight[static_cast<size_t> (i)] * gainRight;
        mixLeft[static_cast<size_t> (i)] = l;
        mixRight[static_cast<size_t> (i)] = r;
        peaks[0] = std::max (peaks[0], std::abs (l));
        peaks[1] = std::max (peaks[1], std::abs (r));
        sums[0] += l * l;
        sums[1] += r * r;
    }
    masterMeter.update (peaks[0], peaks[1], sums[0], sums[1], numFrames, smoothing);
}

void AudioEngine::startInterval() noexcept
{
    if (const auto tempo = pendingTempo.exchange (0); tempo != 0)
        applyTempo (static_cast<int> (tempo >> 16), static_cast<int> (tempo & 0xffff));

    // Each remote channel moves on to the interval queued for it, or to silence
    for (auto& slot : slots)
    {
        if (slot.state.load (std::memory_order_acquire) != RemoteSlot::active)
            continue;

        RemoteInterval* next = nullptr;
        slot.queue.pop (next);
        retire (slot.current);
        slot.current = next;
        slot.readPosition = 0;
    }

    // Close the interval being captured and announce the new one
    flushCapture();

    std::uint32_t mask = 0;
    const int count = numLocals.load (std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
        if (locals[static_cast<size_t> (i)].transmit.load (std::memory_order_relaxed))
            mask |= 1u << i;
    transmitMask = mask;

    capture.flags = InputChunk::intervalStart;
    capture.transmitMask = transmitMask;
    capture.numFrames = 0;
    if (! inputChunks.push (capture))
        inputOverflows.fetch_add (1, std::memory_order_relaxed);
    capture.flags = 0;

    beat = 0;
    nextBeatSample = 0;
    intervalCount.fetch_add (1, std::memory_order_relaxed);
}

void AudioEngine::mixRemote (int offset, int numFrames, bool anySolo) noexcept
{
    const float smoothing = static_cast<float> (std::exp (-numFrames / (meterTimeSeconds * sampleRate)));

    for (auto& slot : slots)
    {
        if (slot.state.load (std::memory_order_acquire) != RemoteSlot::active)
            continue;

        const int start = slot.readPosition;
        slot.readPosition += numFrames; // the interval keeps time even while data is missing

        auto* interval = slot.current;
        if (interval == nullptr)
        {
            slot.meter.update (0.0f, 0.0f, 0.0f, 0.0f, numFrames, smoothing);
            continue;
        }

        const int ready = std::min (interval->framesReady.load (std::memory_order_acquire), interval->capacity());
        const int frames = std::clamp (ready - start, 0, numFrames);

        const auto& controls = slot.controls;
        const bool audible = ! controls.mute.load (std::memory_order_relaxed)
                          && (! anySolo || controls.solo.load (std::memory_order_relaxed));
        float gainLeft = 0.0f, gainRight = 0.0f;
        panGains (controls.gain.load (std::memory_order_relaxed), controls.pan.load (std::memory_order_relaxed), gainLeft, gainRight);

        float peaks[2] {}, sums[2] {};
        const float* srcLeft = interval->left.data() + start;
        const float* srcRight = interval->right.data() + start;
        for (int i = 0; i < frames; ++i)
        {
            const float l = srcLeft[i] * gainLeft;
            const float r = srcRight[i] * gainRight;
            peaks[0] = std::max (peaks[0], std::abs (l));
            peaks[1] = std::max (peaks[1], std::abs (r));
            sums[0] += l * l;
            sums[1] += r * r;
            if (audible)
            {
                mixLeft[static_cast<size_t> (offset + i)] += l;
                mixRight[static_cast<size_t> (offset + i)] += r;
            }
        }
        slot.meter.update (peaks[0], peaks[1], sums[0], sums[1], numFrames, smoothing);
    }
}

void AudioEngine::handleLocals (int offset, int numFrames, bool anySolo) noexcept
{
    const float smoothing = static_cast<float> (std::exp (-numFrames / (meterTimeSeconds * sampleRate)));
    const int count = numLocals.load (std::memory_order_relaxed);

    for (int c = 0; c < count; ++c)
    {
        auto& local = locals[static_cast<size_t> (c)];
        const int first = local.firstInput.load (std::memory_order_relaxed);
        const bool stereo = local.stereo.load (std::memory_order_relaxed);
        const int second = stereo ? first + 1 : first;

        const float* inLeft = (first >= 0 && first < numDeviceInputs) ? inputScratch[static_cast<size_t> (first)].data() + offset : nullptr;
        const float* inRight = (second >= 0 && second < numDeviceInputs) ? inputScratch[static_cast<size_t> (second)].data() + offset : inLeft;

        const auto& controls = local.controls;
        const float gain = controls.mute.load (std::memory_order_relaxed) ? 0.0f : controls.gain.load (std::memory_order_relaxed);
        float monitorLeft = 0.0f, monitorRight = 0.0f;
        const bool monitored = local.monitor.load (std::memory_order_relaxed)
                            && (! anySolo || controls.solo.load (std::memory_order_relaxed));
        if (monitored)
            panGains (gain, controls.pan.load (std::memory_order_relaxed), monitorLeft, monitorRight);

        float peaks[2] {}, sums[2] {};
        if (inLeft != nullptr)
        {
            for (int i = 0; i < numFrames; ++i)
            {
                const float l = inLeft[i] * gain;
                const float r = inRight[i] * gain;
                peaks[0] = std::max (peaks[0], std::abs (l));
                peaks[1] = std::max (peaks[1], std::abs (r));
                sums[0] += l * l;
                sums[1] += r * r;
                mixLeft[static_cast<size_t> (offset + i)] += inLeft[i] * monitorLeft;
                mixRight[static_cast<size_t> (offset + i)] += inRight[i] * monitorRight;
            }
        }
        local.meter.update (peaks[0], peaks[1], sums[0], sums[1], numFrames, smoothing);
    }

    if (running && transmitMask != 0)
        pushCapture (numFrames, offset);
}

void AudioEngine::pushCapture (int numFrames, int offset) noexcept
{
    const int count = numLocals.load (std::memory_order_relaxed);
    int done = 0;
    while (done < numFrames)
    {
        const int frames = std::min (numFrames - done, InputChunk::maxFrames - capture.numFrames);
        for (int c = 0; c < count; ++c)
        {
            auto& local = locals[static_cast<size_t> (c)];
            const int first = local.firstInput.load (std::memory_order_relaxed);
            const int second = local.stereo.load (std::memory_order_relaxed) ? first + 1 : first;
            const float gain = local.controls.mute.load (std::memory_order_relaxed) ? 0.0f : local.controls.gain.load (std::memory_order_relaxed);
            for (int side = 0; side < 2; ++side)
            {
                const int input = side == 0 ? first : second;
                float* dest = capture.samples[c][side] + capture.numFrames;
                if (input >= 0 && input < numDeviceInputs)
                {
                    const float* src = inputScratch[static_cast<size_t> (input)].data() + offset + done;
                    for (int i = 0; i < frames; ++i)
                        dest[i] = src[i] * gain;
                }
                else
                {
                    std::fill_n (dest, frames, 0.0f);
                }
            }
        }

        capture.numFrames += frames;
        done += frames;
        if (capture.numFrames == InputChunk::maxFrames)
            flushCapture();
    }
}

void AudioEngine::flushCapture() noexcept
{
    if (capture.numFrames == 0)
        return;

    capture.transmitMask = transmitMask;
    if (! inputChunks.push (capture))
        inputOverflows.fetch_add (1, std::memory_order_relaxed);
    capture.flags = 0;
    capture.numFrames = 0;
}

void AudioEngine::mixMetronome (int offset, int numFrames) noexcept
{
    const bool muted = metronome.mute.load (std::memory_order_relaxed);
    float gainLeft = 0.0f, gainRight = 0.0f;
    panGains (metronome.gain.load (std::memory_order_relaxed), metronome.pan.load (std::memory_order_relaxed), gainLeft, gainRight);
    const int accent = accentEvery.load (std::memory_order_relaxed);

    float peaks[2] {}, sums[2] {};
    for (int i = 0; i < numFrames; ++i)
    {
        const std::int64_t sample = position + i;
        if (sample == nextBeatSample && beat < bpi)
        {
            clickAccent = beat == 0 || (accent > 0 && beat % accent == 0);
            clickPosition = 0;
            ++beat;
            nextBeatSample = std::llround (samplesPerBeat * beat);
        }

        if (clickPosition < 0)
            continue;

        const auto& sound = clickAccent ? clickAccentSound : clickBeatSound;
        if (static_cast<size_t> (clickPosition) >= sound.size())
        {
            clickPosition = -1;
            continue;
        }

        const float value = sound[static_cast<size_t> (clickPosition++)];
        const float l = value * gainLeft, r = value * gainRight;
        peaks[0] = std::max (peaks[0], std::abs (l));
        peaks[1] = std::max (peaks[1], std::abs (r));
        sums[0] += l * l;
        sums[1] += r * r;
        if (! muted)
        {
            mixLeft[static_cast<size_t> (offset + i)] += l;
            mixRight[static_cast<size_t> (offset + i)] += r;
        }
    }

    const float smoothing = static_cast<float> (std::exp (-numFrames / (meterTimeSeconds * sampleRate)));
    metronomeMeter.update (peaks[0], peaks[1], sums[0], sums[1], numFrames, smoothing);
}

void AudioEngine::retire (RemoteInterval* interval) noexcept
{
    if (interval != nullptr)
        retired.push (interval); // sized for every interval that can be in flight
}

void AudioEngine::serviceReleasingSlots() noexcept
{
    for (auto& slot : slots)
    {
        if (slot.state.load (std::memory_order_acquire) != RemoteSlot::releasing)
            continue;

        retire (slot.current);
        slot.current = nullptr;
        slot.readPosition = 0;

        RemoteInterval* queued = nullptr;
        while (slot.queue.pop (queued))
            retire (queued);

        slot.meter.reset();
        slot.state.store (RemoteSlot::free, std::memory_order_release);
    }
}

} // namespace jamtaba::audio
