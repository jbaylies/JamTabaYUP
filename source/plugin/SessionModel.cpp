/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "plugin/SessionModel.h"

#include <yup_core/yup_core.h>

#include <algorithm>

namespace jamtaba
{

namespace
{
constexpr std::size_t maxChatLines = 500;
}

std::string SessionModel::displayNameOf (const std::string& fullName)
{
    const auto at = fullName.find ('@');
    return at == std::string::npos ? fullName : fullName.substr (0, at);
}

SessionModel::User* SessionModel::findUser (const std::string& fullName)
{
    const auto found = std::find_if (users.begin(), users.end(), [&] (const User& u) { return u.fullName == fullName; });
    return found == users.end() ? nullptr : &*found;
}

SessionModel::Channel* SessionModel::findChannel (const std::string& fullName, int index)
{
    auto* user = findUser (fullName);
    if (user == nullptr)
        return nullptr;
    const auto found = std::find_if (user->channels.begin(), user->channels.end(), [&] (const Channel& c) { return c.index == index; });
    return found == user->channels.end() ? nullptr : &*found;
}

void SessionModel::reset()
{
    users.clear();
    topic.clear();
    licenceText.clear();
    chords = {};
    bpm = bpi = 0;
    userCount = maxUsers = 0;
    ++layoutVersion;
}

void SessionModel::addChat (ChatLine line)
{
    chat.push_back (std::move (line));
    while (chat.size() > maxChatLines)
        chat.pop_front();
    ++chatVersion;
}

void SessionModel::addNotice (std::string text)
{
    addChat ({ ChatLine::Kind::notice, {}, std::move (text) });
}

bool SessionModel::handle (const JamEvent& e)
{
    using T = JamEvent::Type;
    switch (e.type)
    {
        case T::connecting:
            reset();
            connection = Connection::connecting;
            serverAddress = e.text;
            lastError.clear();
            chat.clear();
            addNotice ("Connecting to " + e.text + "...");
            return true;

        case T::licence:
            connection = Connection::licence;
            licenceText = e.text;
            return false;

        case T::connected:
            connection = Connection::connected;
            licenceText.clear();
            ourName = e.user;
            addNotice ("Connected as " + displayNameOf (e.user));
            return false;

        case T::disconnected:
            connection = Connection::disconnected;
            lastError = e.text;
            addNotice (e.text.empty() ? std::string ("Disconnected") : "Disconnected: " + e.text);
            reset();
            return true;

        case T::tempo:
            if (bpm != 0 && (bpm != e.number || bpi != e.number2))
                addNotice ("Tempo changes to " + std::to_string (e.number) + " BPM, " + std::to_string (e.number2) + " BPI at the next interval");
            bpm = e.number;
            bpi = e.number2;
            return false;

        case T::channelAdded:
        case T::channelChanged:
        {
            auto* user = findUser (e.user);
            if (user == nullptr)
            {
                users.push_back ({ e.user, displayNameOf (e.user), nextColor++, {} });
                user = &users.back();
            }
            auto* channel = findChannel (e.user, e.channelIndex);
            if (channel == nullptr)
            {
                user->channels.push_back ({ e.channelIndex, e.channel, e.slot });
                std::sort (user->channels.begin(), user->channels.end(), [] (const Channel& a, const Channel& b) { return a.index < b.index; });
            }
            else
            {
                channel->name = e.channel;
                channel->slot = e.slot;
            }
            ++layoutVersion;
            return true;
        }

        case T::channelRemoved:
            if (auto* user = findUser (e.user))
            {
                std::erase_if (user->channels, [&] (const Channel& c) { return c.index == e.channelIndex; });
                ++layoutVersion;
                return true;
            }
            return false;

        case T::userLeft:
            std::erase_if (users, [&] (const User& u) { return u.fullName == e.user; });
            ++layoutVersion;
            return true;

        case T::chat:
            if (auto progression = ChordProgression::parse (e.text))
                chords = std::move (*progression);
            addChat ({ e.user.empty() ? ChatLine::Kind::server : ChatLine::Kind::message, displayNameOf (e.user), e.text });
            return false;

        case T::privateChat:
            addChat ({ ChatLine::Kind::privateMessage, displayNameOf (e.user), e.text });
            return false;

        case T::topic:
            topic = e.text;
            if (! e.text.empty())
                addChat ({ ChatLine::Kind::server, {}, "Topic: " + e.text });
            return false;

        case T::userJoined:
            addNotice (displayNameOf (e.user) + " joined");
            return false;

        case T::userParted:
            addNotice (displayNameOf (e.user) + " left");
            return false;

        case T::userCount:
            userCount = e.number;
            maxUsers = e.number2;
            return false;

        case T::intervalReceived:
            if (auto* channel = findChannel (e.user, e.channelIndex))
            {
                ++channel->intervalsReceived;
                channel->lastIntervalTime = yup::Time::getMillisecondCounterHiRes();
            }
            return false;

        case T::intervalSent:
            return false;
    }
    return false;
}

} // namespace jamtaba
