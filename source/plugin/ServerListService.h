/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "engine/net/PublicServerList.h"

#include <yup_core/yup_core.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace jamtaba
{

/** Fetches the public server list on a background thread when asked. */
class ServerListService final : private yup::Thread
{
public:
    ServerListService();
    ~ServerListService() override;

    void requestRefresh();
    bool isFetching() const noexcept { return fetching.load(); }

    /** Bumps every time a fetch finishes, whether it worked or not. */
    std::uint32_t getVersion() const noexcept { return version.load(); }

    std::vector<net::PublicServer> getServers() const;
    bool lastFetchFailed() const noexcept { return failed.load(); }
    double getLastFetchTime() const noexcept { return lastFetch.load(); } // seconds, yup::Time::getMillisecondCounterHiRes() / 1000

private:
    void run() override;

    yup::WaitableEvent wake;
    mutable std::mutex lock;
    std::vector<net::PublicServer> servers;
    std::atomic<bool> fetching { false }, failed { false }, requested { false };
    std::atomic<std::uint32_t> version { 0 };
    std::atomic<double> lastFetch { 0.0 };
};

} // namespace jamtaba
