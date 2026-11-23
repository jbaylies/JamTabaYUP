/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "plugin/JamProcessor.h"
#include "ui/Widgets.h"

namespace jamtaba::ui
{

/** The public rooms, live, with one-click join; plus a form for a private
    server by host:port and password, and the user's name. */
class ServerBrowser final : public yup::Component
{
public:
    explicit ServerBrowser (JamProcessor& processor);
    ~ServerBrowser() override;

    void resized() override;
    void paint (yup::Graphics& g) override;
    void refreshDisplay (double lastFrameTimeSeconds) override;

private:
    class RoomList;

    void join (const std::string& host, int port, const std::string& password);
    void joinCustom();
    bool storeName();
    void setStatus (yup::String text, bool isError);

    JamProcessor& processor;
    LineEdit nameField { "Your name" };
    LineEdit channelField { "Channel name" };
    LineEdit addressField { "host:port" };
    LineEdit passwordField { "password (optional)" };
    FlatButton connectButton { "Connect", true };
    FlatButton refreshButton { "Refresh" };
    std::unique_ptr<RoomList> rooms;

    yup::String status;
    bool statusIsError = false;
    std::uint32_t seenVersion = 0;
    double nextRefresh = 0.0;
    bool wasFetching = false;
};

} // namespace jamtaba::ui
