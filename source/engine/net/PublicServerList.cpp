/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "engine/net/PublicServerList.h"

#include <yup_core/yup_core.h>

#include <cmath>

namespace jamtaba::net
{

namespace
{
/** The list sends numbers as strings, and sometimes "NaN" or nothing. */
int toInt (const yup::var& value, int fallback)
{
    if (value.isInt() || value.isInt64() || value.isDouble())
        return static_cast<int> (value);

    const auto text = value.toString().trim();
    if (text.isEmpty() || ! text.containsOnly ("-0123456789"))
        return fallback;
    return text.getIntValue();
}

/** Latitudes and longitudes also come as strings. */
std::optional<float> toCoordinate (const yup::var& value, float limit)
{
    if (value.isVoid() || value.isUndefined())
        return std::nullopt;

    const auto text = value.toString().trim();
    if (text.isEmpty() || ! text.containsOnly ("-+.0123456789"))
        return std::nullopt;

    const auto number = text.getFloatValue();
    if (! std::isfinite (number) || std::abs (number) > limit)
        return std::nullopt;
    return number;
}

std::string toStdString (const yup::var& value)
{
    return value.toString().toStdString();
}
} // namespace

std::optional<std::vector<PublicServer>> parsePublicServerList (const std::string& json)
{
    yup::var root;
    if (yup::JSON::parse (yup::String (json), root).failed())
        return std::nullopt;

    const auto* servers = root["servers"].getArray();
    if (servers == nullptr)
        return std::nullopt;

    std::vector<PublicServer> result;
    for (const auto& entry : *servers)
    {
        PublicServer server;
        server.host = toStdString (entry["host"]);
        server.port = toInt (entry["port"], 0);

        // Older entries only have "name" as host:port
        if (server.host.empty() || server.port <= 0)
        {
            const auto name = entry["name"].toString();
            server.host = name.upToLastOccurrenceOf (":", false, false).toStdString();
            server.port = name.fromLastOccurrenceOf (":", false, false).getIntValue();
        }
        if (server.host.empty() || server.port <= 0 || server.port > 65535)
            continue;

        server.bpm = toInt (entry["bpm"], 0);
        server.bpi = toInt (entry["bpi"], 0);
        server.userCount = toInt (entry["user_count"], 0);
        server.maxUsers = toInt (entry["user_max"], 0);
        server.streamUrl = toStdString (entry["ssl_stream"]);
        if (server.streamUrl.empty())
            server.streamUrl = toStdString (entry["stream"]);

        if (const auto* users = entry["users"].getArray())
        {
            for (const auto& user : *users)
            {
                PublicUser person;
                if (user.isObject())
                {
                    person.name = toStdString (user["name"]);
                    person.latitude = toCoordinate (user["lat"], 90.0f);
                    person.longitude = toCoordinate (user["lon"], 180.0f);
                    person.country = toStdString (user["country"]);
                }
                else
                {
                    person.name = toStdString (user);
                }
                server.users.push_back (std::move (person));
            }
        }

        result.push_back (std::move (server));
    }
    return result;
}

std::optional<std::vector<PublicServer>> fetchPublicServerList (int timeoutMs)
{
    auto stream = yup::URL (publicServerListUrl)
                      .createInputStream (yup::URL::InputStreamOptions (yup::URL::ParameterHandling::inAddress)
                                              .withConnectionTimeoutMs (timeoutMs));
    if (stream == nullptr)
        return std::nullopt;

    return parsePublicServerList (stream->readEntireStreamAsString().toStdString());
}

} // namespace jamtaba::net
