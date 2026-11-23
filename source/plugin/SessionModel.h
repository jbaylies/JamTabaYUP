/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include "engine/JamEngine.h"
#include "plugin/ChordProgression.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace jamtaba
{

/** The session as the UI shows it, built from the engine's events. Lives on
    the message thread, in the processor, so it survives the editor closing. */
class SessionModel
{
public:
    enum class Connection
    {
        disconnected,
        connecting,
        licence, // waiting for the user to accept or decline
        connected,
    };

    struct Channel
    {
        int index = 0;
        std::string name;
        int slot = -1;
        bool receiving = true;
        int intervalsReceived = 0;
        double lastIntervalTime = 0.0; // yup::Time::getMillisecondCounterHiRes()
    };

    struct User
    {
        std::string fullName;    // as the server knows them: name@ip
        std::string displayName; // without the address
        int colorIndex = 0;
        std::vector<Channel> channels;
    };

    struct ChatLine
    {
        enum class Kind
        {
            message,
            privateMessage,
            server,
            notice, // joins, parts, tempo, our own status lines
        };

        Kind kind = Kind::message;
        std::string user;
        std::string text;
    };

    /** Applies one engine event. Returns true if the user/channel layout changed. */
    bool handle (const JamEvent& event);

    void reset();
    void addNotice (std::string text);

    Connection connection = Connection::disconnected;
    std::string serverAddress;
    std::string ourName;
    std::string topic;
    std::string licenceText;
    std::string lastError;
    int bpm = 0, bpi = 0;
    int userCount = 0, maxUsers = 0;
    std::vector<User> users;
    std::deque<ChatLine> chat;
    ChordProgression chords;

    std::uint32_t layoutVersion = 0; // bumps when users or channels change
    std::uint32_t chatVersion = 0;   // bumps when a chat line arrives

    static std::string displayNameOf (const std::string& fullName);

    User* findUser (const std::string& fullName);
    Channel* findChannel (const std::string& fullName, int index);
    const Channel* findChannel (const std::string& fullName, int index) const;

private:
    void addChat (ChatLine line);
    int nextColor = 0;
};

} // namespace jamtaba
