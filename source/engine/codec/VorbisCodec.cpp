/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "engine/codec/VorbisCodec.h"

#include <yup_audio_basics/yup_audio_basics.h>
#include <libogg/libogg.h>
#include <libvorbis/libvorbis.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace jamtaba::codec
{

namespace
{
void appendPage (const ogg_page& page, std::vector<std::uint8_t>& output)
{
    output.insert (output.end(), page.header, page.header + page.header_len);
    output.insert (output.end(), page.body, page.body + page.body_len);
}
} // namespace

//==============================================================================
struct VorbisEncoder::State
{
    vorbis_info info {};
    vorbis_comment comment {};
    vorbis_dsp_state dsp {};
    vorbis_block block {};
    ogg_stream_state stream {};
    int channels = 2;
    bool valid = false;
    bool finished = false;
    bool headersWritten = false;

    void drain (std::vector<std::uint8_t>& output, bool flushAll)
    {
        ogg_packet packet;
        ogg_page page;
        while (vorbis_analysis_blockout (&dsp, &block) == 1)
        {
            vorbis_analysis (&block, nullptr);
            vorbis_bitrate_addblock (&block);
            while (vorbis_bitrate_flushpacket (&dsp, &packet) != 0)
            {
                ogg_stream_packetin (&stream, &packet);
                while (ogg_stream_pageout (&stream, &page) != 0)
                    appendPage (page, output);
            }
        }

        if (flushAll)
            while (ogg_stream_flush (&stream, &page) != 0)
                appendPage (page, output);
    }
};

VorbisEncoder::VorbisEncoder (int channels, int sampleRate, float quality)
    : state (std::make_unique<State>())
{
    auto& s = *state;
    s.channels = std::clamp (channels, 1, 2);

    vorbis_info_init (&s.info);
    if (vorbis_encode_init_vbr (&s.info, s.channels, sampleRate, quality) != 0)
    {
        vorbis_info_clear (&s.info);
        return;
    }

    vorbis_comment_init (&s.comment);
    vorbis_comment_add_tag (&s.comment, "ENCODER", "JamTabaYUP");
    vorbis_analysis_init (&s.dsp, &s.info);
    vorbis_block_init (&s.dsp, &s.block);
    ogg_stream_init (&s.stream, static_cast<int> (yup::Random::getSystemRandom().nextInt()));
    s.valid = true;
}

VorbisEncoder::~VorbisEncoder()
{
    auto& s = *state;
    if (! s.valid)
        return;

    ogg_stream_clear (&s.stream);
    vorbis_block_clear (&s.block);
    vorbis_dsp_clear (&s.dsp);
    vorbis_comment_clear (&s.comment);
    vorbis_info_clear (&s.info);
}

bool VorbisEncoder::isValid() const noexcept
{
    return state->valid;
}

void VorbisEncoder::encode (const float* const* channelData, int numFrames, std::vector<std::uint8_t>& output)
{
    auto& s = *state;
    if (! s.valid || s.finished)
        return;

    // The three header packets go first, each on a page of its own
    if (! s.headersWritten)
    {
        s.headersWritten = true;
        ogg_packet header, headerComment, headerCode;
        vorbis_analysis_headerout (&s.dsp, &s.comment, &header, &headerComment, &headerCode);
        ogg_stream_packetin (&s.stream, &header);
        ogg_stream_packetin (&s.stream, &headerComment);
        ogg_stream_packetin (&s.stream, &headerCode);

        ogg_page page;
        while (ogg_stream_flush (&s.stream, &page) != 0)
            appendPage (page, output);
    }

    if (numFrames <= 0)
        return;

    float** buffer = vorbis_analysis_buffer (&s.dsp, numFrames);
    for (int channel = 0; channel < s.channels; ++channel)
        std::memcpy (buffer[channel], channelData[channel], sizeof (float) * static_cast<size_t> (numFrames));

    vorbis_analysis_wrote (&s.dsp, numFrames);
    s.drain (output, false);
}

void VorbisEncoder::finish (std::vector<std::uint8_t>& output)
{
    auto& s = *state;
    if (! s.valid || s.finished)
        return;

    encode (nullptr, 0, output); // headers, if no audio came at all
    vorbis_analysis_wrote (&s.dsp, 0);
    s.drain (output, true);
    s.finished = true;
}

//==============================================================================
struct VorbisDecoder::State
{
    ogg_sync_state sync {};
    ogg_stream_state stream {};
    vorbis_info info {};
    vorbis_comment comment {};
    vorbis_dsp_state dsp {};
    vorbis_block block {};
    bool streamStarted = false;
    int headersRead = 0;
    bool synthesisReady = false;
    bool failed = false;

    void readPackets (std::vector<float>* left, std::vector<float>* right)
    {
        ogg_page page;
        while (! failed && ogg_sync_pageout (&sync, &page) == 1)
        {
            if (! streamStarted)
            {
                ogg_stream_init (&stream, ogg_page_serialno (&page));
                streamStarted = true;
            }

            if (ogg_stream_pagein (&stream, &page) != 0)
                continue; // a page of another stream; NINJAM sends one stream per interval

            ogg_packet packet;
            while (ogg_stream_packetout (&stream, &packet) == 1)
            {
                if (headersRead < 3)
                {
                    if (vorbis_synthesis_headerin (&info, &comment, &packet) != 0)
                    {
                        failed = true;
                        return;
                    }
                    if (++headersRead == 3)
                    {
                        vorbis_synthesis_init (&dsp, &info);
                        vorbis_block_init (&dsp, &block);
                        synthesisReady = true;
                    }
                    continue;
                }

                if (vorbis_synthesis (&block, &packet) == 0)
                    vorbis_synthesis_blockin (&dsp, &block);

                float** pcm = nullptr;
                int frames = 0;
                while ((frames = vorbis_synthesis_pcmout (&dsp, &pcm)) > 0)
                {
                    if (left != nullptr && right != nullptr)
                    {
                        const auto count = static_cast<size_t> (frames);
                        left->insert (left->end(), pcm[0], pcm[0] + count);
                        const float* r = info.channels > 1 ? pcm[1] : pcm[0];
                        right->insert (right->end(), r, r + count);
                    }
                    vorbis_synthesis_read (&dsp, frames);
                }
            }
        }
    }
};

VorbisDecoder::VorbisDecoder()
    : state (std::make_unique<State>())
{
    ogg_sync_init (&state->sync);
    vorbis_info_init (&state->info);
    vorbis_comment_init (&state->comment);
}

VorbisDecoder::~VorbisDecoder()
{
    auto& s = *state;
    if (s.synthesisReady)
    {
        vorbis_block_clear (&s.block);
        vorbis_dsp_clear (&s.dsp);
    }
    if (s.streamStarted)
        ogg_stream_clear (&s.stream);
    vorbis_comment_clear (&s.comment);
    vorbis_info_clear (&s.info);
    ogg_sync_clear (&s.sync);
}

bool VorbisDecoder::push (const std::uint8_t* data, std::size_t size)
{
    auto& s = *state;
    if (s.failed || size == 0)
        return ! s.failed;

    char* buffer = ogg_sync_buffer (&s.sync, static_cast<long> (size));
    std::memcpy (buffer, data, size);
    ogg_sync_wrote (&s.sync, static_cast<long> (size));
    return true;
}

int VorbisDecoder::getSampleRate() const noexcept
{
    return state->synthesisReady ? static_cast<int> (state->info.rate) : 0;
}

int VorbisDecoder::getNumChannels() const noexcept
{
    return state->synthesisReady ? state->info.channels : 0;
}

void VorbisDecoder::pull (std::vector<float>& left, std::vector<float>& right)
{
    state->readPackets (&left, &right);
}

bool VorbisDecoder::hasFailed() const noexcept
{
    return state->failed;
}

//==============================================================================
struct StreamResampler::State
{
    double ratio = 1.0; // input samples per output sample
    yup::Interpolators::Lagrange interpolators[2];
    std::vector<float> pending[2];

    void convert (std::vector<float>& outLeft, std::vector<float>& outRight, int keepBack)
    {
        const auto available = static_cast<int> (pending[0].size()) - keepBack;
        if (available <= 0)
            return;

        const auto outFrames = static_cast<int> (std::floor (static_cast<double> (available) / ratio));
        if (outFrames <= 0)
            return;

        std::vector<float>* outs[2] { &outLeft, &outRight };
        int used = 0;
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& out = *outs[channel];
            const auto start = out.size();
            out.resize (start + static_cast<size_t> (outFrames));
            used = interpolators[channel].process (ratio,
                                                   pending[channel].data(),
                                                   out.data() + start,
                                                   outFrames,
                                                   static_cast<int> (pending[channel].size()),
                                                   0);
        }

        used = std::min (used, static_cast<int> (pending[0].size()));
        for (auto& p : pending)
            p.erase (p.begin(), p.begin() + used);
    }
};

StreamResampler::StreamResampler (double sourceRate, double targetRate)
    : state (std::make_unique<State>())
{
    state->ratio = (sourceRate > 0.0 && targetRate > 0.0) ? sourceRate / targetRate : 1.0;
}

StreamResampler::~StreamResampler() = default;

bool StreamResampler::isPassThrough() const noexcept
{
    return std::abs (state->ratio - 1.0) < 1.0e-9;
}

void StreamResampler::process (const float* left, const float* right, int numFrames,
                               std::vector<float>& outLeft, std::vector<float>& outRight)
{
    if (numFrames <= 0)
        return;

    const auto count = static_cast<size_t> (numFrames);
    if (isPassThrough())
    {
        outLeft.insert (outLeft.end(), left, left + count);
        outRight.insert (outRight.end(), right, right + count);
        return;
    }

    state->pending[0].insert (state->pending[0].end(), left, left + count);
    state->pending[1].insert (state->pending[1].end(), right, right + count);
    state->convert (outLeft, outRight, 8);
}

void StreamResampler::flush (std::vector<float>& outLeft, std::vector<float>& outRight)
{
    if (! isPassThrough())
        state->convert (outLeft, outRight, 0);
}

} // namespace jamtaba::codec
