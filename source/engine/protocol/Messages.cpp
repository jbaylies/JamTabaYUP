/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.

    The message layouts follow Cockos NINJAM's mpb.h / mpb.cpp
    (Copyright (C) 2005-2007 Cockos Incorporated, GPL-2.0-or-later).
*/

#include "engine/protocol/Messages.h"

#include <yup_core/yup_core.h>

#include <algorithm>
#include <cstring>

namespace jamtaba::protocol
{

namespace
{

/** Little-endian writer. */
class Writer
{
public:
    explicit Writer (Bytes& destination) : bytes (destination) {}

    void u8 (std::uint32_t value) { bytes.push_back (static_cast<std::uint8_t> (value & 0xff)); }
    void u16 (std::uint32_t value) { u8 (value); u8 (value >> 8); }
    void u32 (std::uint32_t value) { u16 (value); u16 (value >> 16); }

    void raw (ByteView data) { bytes.insert (bytes.end(), data.begin(), data.end()); }

    void string (std::string_view text) // NUL terminated
    {
        bytes.insert (bytes.end(), text.begin(), text.end());
        bytes.push_back (0);
    }

private:
    Bytes& bytes;
};

/** Little-endian reader that fails softly: once it runs past the end, every
    later read returns zero and ok() turns false. */
class Reader
{
public:
    explicit Reader (ByteView source) : data (source) {}

    bool ok() const noexcept { return ! overrun; }
    std::size_t remaining() const noexcept { return data.size() - position; }

    std::uint32_t u8()
    {
        if (position >= data.size())
        {
            overrun = true;
            return 0;
        }
        return data[position++];
    }

    std::uint32_t u16() { const auto lo = u8(); return lo | (u8() << 8); }
    std::uint32_t u32() { const auto lo = u16(); return lo | (u16() << 16); }

    template <std::size_t N>
    std::array<std::uint8_t, N> fixed()
    {
        std::array<std::uint8_t, N> result {};
        if (remaining() < N)
        {
            overrun = true;
            position = data.size();
            return result;
        }
        std::copy_n (data.begin() + static_cast<std::ptrdiff_t> (position), N, result.begin());
        position += N;
        return result;
    }

    /** A NUL terminated string; nullopt (and not ok) if the NUL is missing. */
    std::optional<std::string> string()
    {
        const auto begin = data.begin() + static_cast<std::ptrdiff_t> (position);
        const auto nul = std::find (begin, data.end(), std::uint8_t { 0 });
        if (nul == data.end())
        {
            overrun = true;
            position = data.size();
            return std::nullopt;
        }
        std::string text (begin, nul);
        position = static_cast<std::size_t> (nul - data.begin()) + 1;
        return text;
    }

    Bytes rest()
    {
        Bytes result (data.begin() + static_cast<std::ptrdiff_t> (position), data.end());
        position = data.size();
        return result;
    }

private:
    ByteView data;
    std::size_t position = 0;
    bool overrun = false;
};

int signedByte (std::uint32_t value) noexcept
{
    return static_cast<int> (static_cast<std::int8_t> (static_cast<std::uint8_t> (value)));
}

int signedShort (std::uint32_t value) noexcept
{
    return static_cast<int> (static_cast<std::int16_t> (static_cast<std::uint16_t> (value)));
}

std::uint32_t clampPan (int pan) noexcept
{
    return static_cast<std::uint8_t> (static_cast<std::int8_t> (std::clamp (pan, -128, 127)));
}

} // namespace

//==============================================================================
Bytes frame (const Message& message)
{
    Bytes bytes;
    bytes.reserve (headerSize + message.payload.size());
    Writer writer (bytes);
    writer.u8 (static_cast<std::uint32_t> (message.type));
    writer.u32 (static_cast<std::uint32_t> (message.payload.size()));
    writer.raw (message.payload);
    return bytes;
}

bool isZero (const Guid& guid) noexcept
{
    return std::all_of (guid.begin(), guid.end(), [] (std::uint8_t b) { return b == 0; });
}

//==============================================================================
std::optional<AuthChallenge> parseAuthChallenge (ByteView payload)
{
    Reader reader (payload);
    AuthChallenge result;
    result.challenge = reader.fixed<8>();
    result.serverCapabilities = reader.u32();
    result.protocolVersion = reader.u32();
    if (! reader.ok())
        return std::nullopt;

    // A licence the server forgot to terminate is ignored, as the reference client does
    if ((result.serverCapabilities & 1) != 0)
        result.licenceAgreement = reader.string();

    return result;
}

std::optional<AuthReply> parseAuthReply (ByteView payload)
{
    Reader reader (payload);
    AuthReply result;
    result.succeeded = (reader.u8() & 1) != 0;
    if (! reader.ok())
        return std::nullopt;

    if (reader.remaining() > 0)
    {
        if (auto text = reader.string())
        {
            result.message = std::move (*text);
            if (reader.remaining() > 0)
                result.maxChannels = static_cast<int> (reader.u8());
        }
    }
    return result;
}

std::optional<ConfigChange> parseConfigChange (ByteView payload)
{
    Reader reader (payload);
    ConfigChange result;
    result.bpm = static_cast<int> (reader.u16());
    result.bpi = static_cast<int> (reader.u16());
    if (! reader.ok())
        return std::nullopt;
    return result;
}

std::optional<std::vector<UserChannelInfo>> parseUserInfoChange (ByteView payload)
{
    Reader reader (payload);
    std::vector<UserChannelInfo> result;
    while (reader.remaining() > 0)
    {
        UserChannelInfo info;
        info.active = reader.u8() != 0;
        info.channelIndex = static_cast<int> (reader.u8());
        info.volume = signedShort (reader.u16());
        info.pan = signedByte (reader.u8());
        info.flags = static_cast<int> (reader.u8());
        auto user = reader.string();
        auto channel = reader.string();
        if (! reader.ok() || ! user || ! channel)
            break; // keep the complete records, like the reference parser

        info.userName = std::move (*user);
        info.channelName = std::move (*channel);
        result.push_back (std::move (info));
    }
    return result;
}

std::optional<DownloadIntervalBegin> parseDownloadIntervalBegin (ByteView payload)
{
    Reader reader (payload);
    DownloadIntervalBegin result;
    result.guid = reader.fixed<16>();
    result.estimatedSize = reader.u32();
    result.fourcc = reader.u32();
    result.channelIndex = static_cast<int> (reader.u8());
    auto user = reader.string();
    if (! reader.ok() || ! user)
        return std::nullopt;
    result.userName = std::move (*user);
    return result;
}

std::optional<IntervalWrite> parseIntervalWrite (ByteView payload)
{
    Reader reader (payload);
    IntervalWrite result;
    result.guid = reader.fixed<16>();
    result.last = (reader.u8() & 1) != 0;
    if (! reader.ok())
        return std::nullopt;
    result.data = reader.rest();
    return result;
}

std::optional<ChatMessage> parseChatMessage (ByteView payload)
{
    if (payload.empty())
        return std::nullopt;

    ChatMessage result;
    std::size_t position = 0;
    for (auto& param : result.params)
    {
        if (position >= payload.size())
            break;
        const auto begin = payload.begin() + static_cast<std::ptrdiff_t> (position);
        const auto end = std::find (begin, payload.end(), std::uint8_t { 0 });
        param.assign (begin, end);
        position = static_cast<std::size_t> (end - payload.begin()) + 1;
    }
    return result;
}

//==============================================================================
Message makeAuthUser (std::string_view userName, const std::array<std::uint8_t, 20>& passHash, std::uint32_t capabilities)
{
    Message message { MessageType::clientAuthUser, {} };
    Writer writer (message.payload);
    writer.raw (passHash);
    writer.string (userName);
    writer.u32 (capabilities);
    writer.u32 (protocolVersionCurrent);
    return message;
}

Message makeSetUserMask (const std::vector<std::pair<std::string, std::uint32_t>>& masks)
{
    Message message { MessageType::clientSetUserMask, {} };
    Writer writer (message.payload);
    for (const auto& [user, mask] : masks)
    {
        writer.string (user);
        writer.u32 (mask);
    }
    return message;
}

Message makeSetChannelInfo (const std::vector<LocalChannelInfo>& channels)
{
    constexpr std::uint32_t fieldsSize = 4; // volume (2), pan, flags

    Message message { MessageType::clientSetChannelInfo, {} };
    Writer writer (message.payload);
    writer.u16 (fieldsSize);
    for (const auto& channel : channels)
    {
        writer.string (channel.name);
        writer.u16 (static_cast<std::uint16_t> (static_cast<std::int16_t> (channel.volume)));
        writer.u8 (clampPan (channel.pan));
        writer.u8 (static_cast<std::uint32_t> (channel.flags));
    }
    return message;
}

Message makeUploadIntervalBegin (const Guid& guid, std::uint32_t fourcc, int channelIndex)
{
    Message message { MessageType::clientUploadIntervalBegin, {} };
    Writer writer (message.payload);
    writer.raw (guid);
    writer.u32 (0); // estimated size, unused by servers
    writer.u32 (fourcc);
    writer.u8 (static_cast<std::uint32_t> (channelIndex));
    return message;
}

Message makeUploadIntervalWrite (const Guid& guid, ByteView data, bool last)
{
    Message message { MessageType::clientUploadIntervalWrite, {} };
    Writer writer (message.payload);
    writer.raw (guid);
    writer.u8 (last ? 1u : 0u);
    writer.raw (data);
    return message;
}

Message makeChatMessage (std::initializer_list<std::string_view> params)
{
    Message message { MessageType::chatMessage, {} };
    Writer writer (message.payload);
    std::size_t count = 0;
    for (const auto param : params)
    {
        if (count++ == 5)
            break;
        writer.string (param);
    }
    for (; count < 5; ++count)
        writer.string ({});
    return message;
}

Message makeKeepAlive()
{
    return { MessageType::keepAlive, {} };
}

Message makeAuthChallenge (const AuthChallenge& challenge)
{
    Message message { MessageType::serverAuthChallenge, {} };
    Writer writer (message.payload);
    writer.raw (challenge.challenge);
    writer.u32 (challenge.serverCapabilities | (challenge.licenceAgreement ? 1u : 0u));
    writer.u32 (challenge.protocolVersion);
    if (challenge.licenceAgreement)
        writer.string (*challenge.licenceAgreement);
    return message;
}

Message makeUserInfoChange (const std::vector<UserChannelInfo>& channels)
{
    Message message { MessageType::serverUserInfoChangeNotify, {} };
    Writer writer (message.payload);
    for (const auto& channel : channels)
    {
        writer.u8 (channel.active ? 1u : 0u);
        writer.u8 (static_cast<std::uint32_t> (channel.channelIndex));
        writer.u16 (static_cast<std::uint16_t> (static_cast<std::int16_t> (channel.volume)));
        writer.u8 (clampPan (channel.pan));
        writer.u8 (static_cast<std::uint32_t> (channel.flags));
        writer.string (channel.userName);
        writer.string (channel.channelName);
    }
    return message;
}

Message makeDownloadIntervalBegin (const DownloadIntervalBegin& begin)
{
    Message message { MessageType::serverDownloadIntervalBegin, {} };
    Writer writer (message.payload);
    writer.raw (begin.guid);
    writer.u32 (begin.estimatedSize);
    writer.u32 (begin.fourcc);
    writer.u8 (static_cast<std::uint32_t> (begin.channelIndex));
    writer.string (begin.userName);
    return message;
}

//==============================================================================
std::array<std::uint8_t, 20> authPassHash (std::string_view userName,
                                           std::string_view password,
                                           const std::array<std::uint8_t, 8>& challenge)
{
    std::string userPass;
    userPass.reserve (userName.size() + 1 + password.size());
    userPass.append (userName).append (":").append (password);

    const yup::SHA1 inner (userPass.data(), userPass.size());

    std::array<std::uint8_t, 28> outerInput {};
    const auto innerBytes = inner.getRawData();
    std::copy (innerBytes.begin(), innerBytes.end(), outerInput.begin());
    std::copy (challenge.begin(), challenge.end(), outerInput.begin() + 20);

    const yup::SHA1 outer (outerInput.data(), outerInput.size());

    std::array<std::uint8_t, 20> result {};
    const auto outerBytes = outer.getRawData();
    std::copy (outerBytes.begin(), outerBytes.end(), result.begin());
    return result;
}

} // namespace jamtaba::protocol
