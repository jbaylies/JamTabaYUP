/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace jamtaba::codec
{

/** Encodes one NINJAM interval as one Ogg Vorbis stream. Each interval gets a
    fresh encoder, so every interval decodes on its own. Not real-time safe:
    used on the engine thread. */
class VorbisEncoder
{
public:
    /** channels: 1 or 2. quality: Vorbis VBR quality, -0.1 .. 1. */
    VorbisEncoder (int channels, int sampleRate, float quality);
    ~VorbisEncoder();

    bool isValid() const noexcept;

    /** Planar input. Appends any finished Ogg pages to output. */
    void encode (const float* const* channelData, int numFrames, std::vector<std::uint8_t>& output);

    /** Ends the stream, appending the last pages. */
    void finish (std::vector<std::uint8_t>& output);

private:
    struct State;
    std::unique_ptr<State> state;
};

//==============================================================================
/** Decodes one interval's Ogg Vorbis stream as its bytes arrive. */
class VorbisDecoder
{
public:
    VorbisDecoder();
    ~VorbisDecoder();

    /** Feeds bytes; returns false once the stream turns out to be broken. */
    bool push (const std::uint8_t* data, std::size_t size);

    /** Known once the three header packets have arrived. */
    int getSampleRate() const noexcept;
    int getNumChannels() const noexcept;

    /** Appends all decodable audio to left and right (mono goes to both). */
    void pull (std::vector<float>& left, std::vector<float>& right);

    bool hasFailed() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state;
};

//==============================================================================
/** Streaming sample rate converter for one or two planar channels. */
class StreamResampler
{
public:
    StreamResampler (double sourceRate, double targetRate);
    ~StreamResampler();

    bool isPassThrough() const noexcept;

    /** Appends input; converted output goes to the end of outLeft/outRight.
        A little input is held back for the interpolator's look-ahead. */
    void process (const float* left, const float* right, int numFrames,
                  std::vector<float>& outLeft, std::vector<float>& outRight);

    /** Converts whatever input is still held back. */
    void flush (std::vector<float>& outLeft, std::vector<float>& outRight);

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace jamtaba::codec
