/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace jamtaba::net
{

struct PublicServer
{
    std::string host;
    int port = 2049;
    int bpm = 0;
    int bpi = 0;
    int userCount = 0;
    int maxUsers = 0; // 0 when the list doesn't say
    std::vector<std::string> users;
    std::string streamUrl;

    std::string address() const { return host + ":" + std::to_string (port); }
};

/** The list JamTaba and other clients use: the servers ninbot.com watches. */
inline constexpr const char* publicServerListUrl = "https://ninbot.com/app/servers.php";

/** Parses the list's JSON. nullopt if it isn't a server list at all. */
std::optional<std::vector<PublicServer>> parsePublicServerList (const std::string& json);

/** Downloads and parses the list. Blocks: call it off the message thread. */
std::optional<std::vector<PublicServer>> fetchPublicServerList (int timeoutMs = 10000);

} // namespace jamtaba::net
