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

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jamtaba::protocol
{

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;
using Guid = std::array<std::uint8_t, 16>;

inline constexpr std::uint32_t protocolVersionMin = 0x00020000;
inline constexpr std::uint32_t protocolVersionMax = 0x0002ffff;
inline constexpr std::uint32_t protocolVersionCurrent = 0x00020000;

/** Largest payload the reference implementation accepts in one message. */
inline constexpr std::uint32_t maxPayloadSize = 16384;

/** Fourcc of Ogg Vorbis intervals, the only audio format NINJAM clients use. */
inline constexpr std::uint32_t fourccOggVorbis = 'O' | ('G' << 8) | ('G' << 16) | ('v' << 24);

enum class MessageType : std::uint8_t
{
    serverAuthChallenge = 0x00,
    serverAuthReply = 0x01,
    serverConfigChangeNotify = 0x02,
    serverUserInfoChangeNotify = 0x03,
    serverDownloadIntervalBegin = 0x04,
    serverDownloadIntervalWrite = 0x05,
    clientAuthUser = 0x80,
    clientSetUserMask = 0x81,
    clientSetChannelInfo = 0x82,
    clientUploadIntervalBegin = 0x83,
    clientUploadIntervalWrite = 0x84,
    chatMessage = 0xc0,
    keepAlive = 0xfd,
};

/** One framed message: a type byte, then a 32-bit little-endian payload size. */
struct Message
{
    MessageType type = MessageType::keepAlive;
    Bytes payload;
};

inline constexpr std::size_t headerSize = 5;

/** The 5 + payload bytes that go on the wire. */
Bytes frame (const Message& message);

//==============================================================================
// Server to client

struct AuthChallenge
{
    std::array<std::uint8_t, 8> challenge {};
    std::uint32_t serverCapabilities = 0;
    std::uint32_t protocolVersion = 0;
    std::optional<std::string> licenceAgreement;

    /** Seconds between keepalives the server asks for (0 means its default). */
    int keepAliveSeconds() const noexcept { return static_cast<int> ((serverCapabilities >> 8) & 0xff); }
};

struct AuthReply
{
    bool succeeded = false;
    std::string message; // on success, the name the server gave us; otherwise the error
    int maxChannels = 32;
};

struct ConfigChange
{
    int bpm = 120;
    int bpi = 32;
};

namespace channelFlags
{
    inline constexpr int noDefaultSubscribe = 1;
    inline constexpr int voiceChat = 2; // "instamode"
    inline constexpr int session = 4;
    inline constexpr int inactiveFiller = 0x80;
}

struct UserChannelInfo
{
    bool active = false;
    int channelIndex = 0;
    int volume = 0; // dB * 10
    int pan = 0; // -128..127
    int flags = 0;
    std::string userName;
    std::string channelName;
};

struct DownloadIntervalBegin
{
    Guid guid {};
    std::uint32_t estimatedSize = 0;
    std::uint32_t fourcc = 0;
    int channelIndex = 0;
    std::string userName;
};

struct IntervalWrite // the same layout both ways
{
    Guid guid {};
    bool last = false;
    Bytes data;
};

/** Chat messages carry up to five strings. Server to client:
    MSG user text, PRIVMSG user text, TOPIC user topic, JOIN user, PART user,
    USERCOUNT users maxusers. */
struct ChatMessage
{
    std::array<std::string, 5> params;
};

std::optional<AuthChallenge> parseAuthChallenge (ByteView payload);
std::optional<AuthReply> parseAuthReply (ByteView payload);
std::optional<ConfigChange> parseConfigChange (ByteView payload);
std::optional<std::vector<UserChannelInfo>> parseUserInfoChange (ByteView payload);
std::optional<DownloadIntervalBegin> parseDownloadIntervalBegin (ByteView payload);
std::optional<IntervalWrite> parseIntervalWrite (ByteView payload);
std::optional<ChatMessage> parseChatMessage (ByteView payload);

//==============================================================================
// Client to server

struct LocalChannelInfo
{
    std::string name;
    int volume = 0;
    int pan = 0;
    int flags = 0;
};

namespace clientCapabilities
{
    inline constexpr std::uint32_t acceptsLicence = 1;
}

Message makeAuthUser (std::string_view userName, const std::array<std::uint8_t, 20>& passHash, std::uint32_t capabilities);
Message makeSetUserMask (const std::vector<std::pair<std::string, std::uint32_t>>& masks);
Message makeSetChannelInfo (const std::vector<LocalChannelInfo>& channels);
Message makeUploadIntervalBegin (const Guid& guid, std::uint32_t fourcc, int channelIndex);
Message makeUploadIntervalWrite (const Guid& guid, ByteView data, bool last);
Message makeChatMessage (std::initializer_list<std::string_view> params);
Message makeKeepAlive();

/** For tests and tools: the server side of the messages above. */
Message makeAuthChallenge (const AuthChallenge& challenge);
Message makeUserInfoChange (const std::vector<UserChannelInfo>& channels);
Message makeDownloadIntervalBegin (const DownloadIntervalBegin& begin);

/** SHA1(SHA1(user ":" password) + challenge). */
std::array<std::uint8_t, 20> authPassHash (std::string_view userName,
                                           std::string_view password,
                                           const std::array<std::uint8_t, 8>& challenge);

bool isZero (const Guid& guid) noexcept;

} // namespace jamtaba::protocol
