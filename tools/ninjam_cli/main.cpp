/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

/*  ninjam_cli: the headless harness for the engine.

    Phase 1 only checks the platform pieces the engine stands on, with no GUI:

      ninjam_cli --check [--offline]

    1. Vorbis: encodes two seconds of a stereo sine to Ogg Vorbis in memory and
       decodes it back.
    2. HTTPS + JSON: fetches the public server list and parses it.
    3. TCP: connects to the first listed server and reads its NINJAM auth
       challenge (message 0x00), printing the protocol version.

    Phase 2 grows this into a real client: connect, follow BPM/BPI, receive and
    decode intervals, chat and vote.
*/

#include <yup_core/yup_core.h>
#include <libogg/libogg.h>
#include <libvorbis/libvorbis.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <vector>

namespace
{

constexpr const char* serverListUrl = "https://ninbot.com/app/servers.php";

void print (const yup::String& text)
{
    std::printf ("%s\n", text.toRawUTF8());
    std::fflush (stdout);
}

//==============================================================================
bool checkVorbis()
{
    constexpr int sampleRate = 44100;
    constexpr int channels = 2;
    constexpr int totalFrames = sampleRate * 2;
    constexpr int blockFrames = 1024;

    vorbis_info info;
    vorbis_info_init (&info);
    if (vorbis_encode_init_vbr (&info, channels, sampleRate, 0.4f) != 0)
    {
        print ("vorbis: encoder init failed");
        vorbis_info_clear (&info);
        return false;
    }

    vorbis_comment comment;
    vorbis_comment_init (&comment);
    vorbis_dsp_state dsp;
    vorbis_analysis_init (&dsp, &info);
    vorbis_block block;
    vorbis_block_init (&dsp, &block);
    ogg_stream_state stream;
    ogg_stream_init (&stream, 1);

    std::vector<unsigned char> encoded;
    const auto appendPage = [&encoded] (const ogg_page& page)
    {
        encoded.insert (encoded.end(), page.header, page.header + page.header_len);
        encoded.insert (encoded.end(), page.body, page.body + page.body_len);
    };

    ogg_packet header, headerComment, headerCode;
    vorbis_analysis_headerout (&dsp, &comment, &header, &headerComment, &headerCode);
    ogg_stream_packetin (&stream, &header);
    ogg_stream_packetin (&stream, &headerComment);
    ogg_stream_packetin (&stream, &headerCode);

    ogg_page page;
    while (ogg_stream_flush (&stream, &page) != 0)
        appendPage (page);

    const auto drain = [&]
    {
        ogg_packet packet;
        while (vorbis_analysis_blockout (&dsp, &block) == 1)
        {
            vorbis_analysis (&block, nullptr);
            vorbis_bitrate_addblock (&block);
            while (vorbis_bitrate_flushpacket (&dsp, &packet) != 0)
            {
                ogg_stream_packetin (&stream, &packet);
                while (ogg_stream_pageout (&stream, &page) != 0)
                    appendPage (page);
            }
        }
    };

    for (int frame = 0; frame < totalFrames; frame += blockFrames)
    {
        const int frames = std::min (blockFrames, totalFrames - frame);
        float** buffer = vorbis_analysis_buffer (&dsp, frames);
        for (int i = 0; i < frames; ++i)
        {
            const auto phase = 2.0 * std::numbers::pi * 440.0 * static_cast<double> (frame + i) / sampleRate;
            buffer[0][i] = static_cast<float> (0.5 * std::sin (phase));
            buffer[1][i] = buffer[0][i];
        }
        vorbis_analysis_wrote (&dsp, frames);
        drain();
    }

    vorbis_analysis_wrote (&dsp, 0);
    drain();
    while (ogg_stream_flush (&stream, &page) != 0)
        appendPage (page);

    ogg_stream_clear (&stream);
    vorbis_block_clear (&block);
    vorbis_dsp_clear (&dsp);
    vorbis_comment_clear (&comment);
    vorbis_info_clear (&info);

    // Decode it back with vorbisfile reading from memory
    struct Reader
    {
        const std::vector<unsigned char>* data = nullptr;
        size_t position = 0;
    } reader { &encoded, 0 };

    ov_callbacks callbacks {};
    callbacks.read_func = [] (void* dest, size_t size, size_t count, void* source) -> size_t
    {
        auto& r = *static_cast<Reader*> (source);
        const auto wanted = size * count;
        const auto available = r.data->size() - r.position;
        const auto bytes = std::min (wanted, available);
        std::memcpy (dest, r.data->data() + r.position, bytes);
        r.position += bytes;
        return size > 0 ? bytes / size : 0;
    };

    OggVorbis_File file;
    if (ov_open_callbacks (&reader, &file, nullptr, 0, callbacks) != 0)
    {
        print ("vorbis: could not open the encoded stream");
        return false;
    }

    long decodedFrames = 0;
    double peak = 0.0;
    int section = 0;
    for (;;)
    {
        float** pcm = nullptr;
        const long frames = ov_read_float (&file, &pcm, 4096, &section);
        if (frames <= 0)
            break;

        for (long i = 0; i < frames; ++i)
            peak = std::max (peak, static_cast<double> (std::abs (pcm[0][i])));
        decodedFrames += frames;
    }
    ov_clear (&file);

    const bool ok = decodedFrames == totalFrames && peak > 0.4 && peak < 0.6;
    print (yup::String::formatted ("vorbis: %d bytes for %d frames, decoded %ld frames, peak %.3f: %s",
                                   static_cast<int> (encoded.size()), totalFrames, decodedFrames, peak, ok ? "ok" : "FAILED"));
    return ok;
}

//==============================================================================
struct ServerAddress
{
    yup::String host;
    int port = 0;
};

bool checkServerList (ServerAddress& firstServer)
{
    auto stream = yup::URL (serverListUrl)
                      .createInputStream (yup::URL::InputStreamOptions (yup::URL::ParameterHandling::inAddress)
                                              .withConnectionTimeoutMs (10000));
    if (stream == nullptr)
    {
        print (yup::String ("server list: could not open ") + serverListUrl);
        return false;
    }

    const auto json = yup::JSON::parse (stream->readEntireStreamAsString());
    const auto* servers = json["servers"].getArray();
    if (servers == nullptr || servers->isEmpty())
    {
        print ("server list: no \"servers\" array in the reply");
        return false;
    }

    for (const auto& server : *servers)
    {
        print (yup::String::formatted ("  %-28s users %s/%s  bpm %s  bpi %s",
                                       server["name"].toString().toRawUTF8(),
                                       server["user_count"].toString().toRawUTF8(),
                                       server["user_max"].toString().toRawUTF8(),
                                       server["bpm"].toString().toRawUTF8(),
                                       server["bpi"].toString().toRawUTF8()));
    }

    const auto& first = servers->getReference (0);
    firstServer.host = first["host"].toString();
    firstServer.port = first["port"].toString().getIntValue();

    print (yup::String::formatted ("server list: %d servers: ok", servers->size()));
    return firstServer.host.isNotEmpty() && firstServer.port > 0;
}

//==============================================================================
yup::uint32 readLE32 (const yup::uint8* bytes)
{
    return static_cast<yup::uint32> (bytes[0])
         | (static_cast<yup::uint32> (bytes[1]) << 8)
         | (static_cast<yup::uint32> (bytes[2]) << 16)
         | (static_cast<yup::uint32> (bytes[3]) << 24);
}

bool checkAuthChallenge (const ServerAddress& server)
{
    yup::StreamingSocket socket;
    if (! socket.connect (server.host, server.port, 10000))
    {
        print (yup::String::formatted ("tcp: could not connect to %s:%d", server.host.toRawUTF8(), server.port));
        return false;
    }

    // Every NINJAM message: 1 byte type, 4 bytes little-endian payload length
    yup::uint8 header[5] {};
    if (socket.waitUntilReady (true, 10000) != 1 || socket.read (header, 5, true) != 5)
    {
        print ("tcp: no message from the server");
        return false;
    }

    const auto type = header[0];
    const auto length = readLE32 (header + 1);
    if (type != 0x00 || length < 16 || length > 65536)
    {
        print (yup::String::formatted ("tcp: expected an auth challenge, got type 0x%02x, %u bytes", type, length));
        return false;
    }

    std::vector<yup::uint8> payload (length);
    if (socket.read (payload.data(), static_cast<int> (length), true) != static_cast<int> (length))
    {
        print ("tcp: auth challenge cut short");
        return false;
    }

    // challenge[8], server capabilities, protocol version, then the licence text if capabilities & 1
    const auto capabilities = readLE32 (payload.data() + 8);
    const auto protocol = readLE32 (payload.data() + 12);
    const auto keepAlive = (capabilities >> 8) & 0xff;
    const bool ok = protocol >= 0x00020000 && protocol <= 0x0002ffff;

    print (yup::String::formatted ("tcp: %s:%d protocol 0x%08x, keepalive %us, licence %s: %s",
                                   server.host.toRawUTF8(), server.port, protocol, keepAlive,
                                   (capabilities & 1) != 0 ? "yes" : "no", ok ? "ok" : "FAILED"));
    return ok;
}

} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    bool check = false;
    bool offline = false;
    for (int i = 1; i < argc; ++i)
    {
        const yup::String arg (argv[i]);
        check = check || arg == "--check";
        offline = offline || arg == "--offline";
    }

    if (! check)
    {
        print ("usage: ninjam_cli --check [--offline]");
        return 2;
    }

    bool ok = checkVorbis();

    if (! offline)
    {
        ServerAddress server;
        ok = checkServerList (server) && ok;
        if (server.port > 0)
            ok = checkAuthChallenge (server) && ok;
    }

    print (ok ? "all checks passed" : "some checks FAILED");
    return ok ? 0 : 1;
}
