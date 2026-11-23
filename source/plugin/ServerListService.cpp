/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "plugin/ServerListService.h"

#include <algorithm>

namespace jamtaba
{

ServerListService::ServerListService()
    : yup::Thread ("NINJAM server list")
{
    startThread();
}

ServerListService::~ServerListService()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (12000); // a fetch in progress times out after 8 s
}

void ServerListService::requestRefresh()
{
    requested.store (true);
    wake.signal();
}

std::vector<net::PublicServer> ServerListService::getServers() const
{
    std::lock_guard guard (lock);
    return servers;
}

void ServerListService::run()
{
    while (! threadShouldExit())
    {
        wake.wait (1000);
        if (threadShouldExit() || ! requested.exchange (false))
            continue;

        fetching.store (true);

        // For scripted testing: JAMTABA_SERVER_LIST=file.json reads a local list
        const auto localList = yup::SystemStats::getEnvironmentVariable ("JAMTABA_SERVER_LIST", {});
        auto result = localList.isNotEmpty()
                        ? net::parsePublicServerList (yup::File (localList).loadFileAsString().toStdString())
                        : net::fetchPublicServerList (8000);
        if (result)
        {
            // Busiest rooms first
            std::stable_sort (result->begin(), result->end(), [] (const auto& a, const auto& b) { return a.userCount > b.userCount; });
            std::lock_guard guard (lock);
            servers = std::move (*result);
        }
        failed.store (! result.has_value());
        lastFetch.store (yup::Time::getMillisecondCounterHiRes() / 1000.0);
        fetching.store (false);
        version.fetch_add (1);
    }
}

} // namespace jamtaba
