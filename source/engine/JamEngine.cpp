/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.

    The session behaviour (authentication, subscriptions, when a downloaded
    interval starts playing, silent intervals) follows Cockos NINJAM's
    njclient.cpp (Copyright (C) 2005-2007 Cockos Incorporated, GPL-2.0-or-later).
*/

#include "engine/JamEngine.h"
#include "engine/codec/VorbisCodec.h"
#include "engine/protocol/Messages.h"

#include <yup_core/yup_core.h>

#include <algorithm>
#include <array>
#include <map>

namespace jamtaba
{

using namespace protocol;

namespace
{
constexpr int maxUserChannels = 32;
constexpr std::size_t maxWriteChunk = 8192;   // bytes of Vorbis per upload message
constexpr std::size_t playAfterBytes = 8192;  // NINJAM's default prebuffer before an interval is queued
constexpr int maxEncodeRate = 48000;

JamEvent makeEvent (JamEvent::Type type, std::string text = {})
{
    JamEvent e;
    e.type = type;
    e.text = std::move (text);
    return e;
}

double nowSeconds()
{
    return yup::Time::getMillisecondCounterHiRes() / 1000.0;
}
} // namespace

//==============================================================================
class JamEngine::Worker final : public yup::Thread
{
public:
    Worker (JamEngine& ownerToUse, ConnectionSettings settingsToUse)
        : yup::Thread ("NINJAM engine")
        , owner (ownerToUse)
        , audio (ownerToUse.audioEngine)
        , settings (std::move (settingsToUse))
    {
        chunk = std::make_unique<audio::InputChunk>();
    }

    ~Worker() override
    {
        stopThread (5000);
    }

    void post (std::function<void (Worker&)> command)
    {
        std::lock_guard lock (commandsLock);
        commands.push_back (std::move (command));
    }

    //==============================================================================
    // Commands, run on the engine thread

    void acceptLicence (bool accept)
    {
        if (state != State::awaitingLicence)
            return;

        if (! accept)
        {
            fail ("Licence declined");
            return;
        }
        sendAuth (clientCapabilities::acceptsLicence);
    }

    void sendChat (std::initializer_list<std::string_view> params)
    {
        if (state == State::connected)
            send (makeChatMessage (params));
    }

    void setReceiving (const std::string& userName, int channelIndex, bool receive)
    {
        auto found = users.find (userName);
        if (found == users.end() || channelIndex < 0 || channelIndex >= maxUserChannels)
            return;

        auto& user = found->second;
        const auto bit = 1u << channelIndex;
        user.subscribed = receive ? (user.subscribed | bit) : (user.subscribed & ~bit);
        send (makeSetUserMask ({ { userName, user.subscribed } }));
    }

private:
    enum class State
    {
        connecting,
        awaitingChallenge,
        awaitingLicence,
        authenticating,
        connected,
    };

    struct ChannelState
    {
        bool present = false;
        std::string name;
        int flags = 0;
        int slot = -1;
    };

    struct RemoteUser
    {
        std::array<ChannelState, maxUserChannels> channels;
        std::uint32_t subscribed = 0;
    };

    struct Download
    {
        Guid guid {};
        std::string userName;
        int channelIndex = 0;
        int slot = -1;
        std::size_t bytes = 0;
        codec::VorbisDecoder decoder;
        std::unique_ptr<codec::StreamResampler> resampler;
        audio::RemoteInterval* interval = nullptr;
        bool queued = false;
    };

    struct Upload
    {
        Guid guid {};
        std::unique_ptr<codec::VorbisEncoder> encoder;
        std::unique_ptr<codec::StreamResampler> resampler;
        std::vector<std::uint8_t> pending;
        std::size_t bytes = 0;
        int channels = 2;
    };

    //==============================================================================
    void run() override
    {
        const auto target = settings.host + ":" + std::to_string (settings.port);
        event (makeEvent (JamEvent::Type::connecting, target));

        if (! socket.connect (yup::String (settings.host), settings.port, 10000))
        {
            finish ("Could not connect to " + target);
            return;
        }

        state = State::awaitingChallenge;
        lastReceive = lastSend = nowSeconds();

        std::array<std::uint8_t, 65536> buffer {};
        while (! threadShouldExit() && ! failed)
        {
            const int ready = socket.waitUntilReady (true, 5);
            if (ready < 0)
            {
                fail ("Connection lost");
                break;
            }
            if (ready > 0)
            {
                const int count = socket.read (buffer.data(), static_cast<int> (buffer.size()), false);
                if (count <= 0)
                {
                    fail ("The server closed the connection");
                    break;
                }
                received.insert (received.end(), buffer.begin(), buffer.begin() + count);
                lastReceive = nowSeconds();
                handleReceived();
            }

            runCommands();
            owner.collectRetiredIntervals();
            if (state == State::connected)
                encodeCapturedAudio();
            keepAlive();
        }

        finish (failed ? failure : std::string {});
    }

    //==============================================================================
    void handleReceived()
    {
        std::size_t offset = 0;
        while (! failed && received.size() - offset >= headerSize)
        {
            const auto* header = received.data() + offset;
            const auto type = static_cast<MessageType> (header[0]);
            const auto size = static_cast<std::uint32_t> (header[1]) | (static_cast<std::uint32_t> (header[2]) << 8)
                            | (static_cast<std::uint32_t> (header[3]) << 16) | (static_cast<std::uint32_t> (header[4]) << 24);
            if (size > 4 * maxPayloadSize)
            {
                fail ("The server sent a message that is too large");
                return;
            }
            if (received.size() - offset < headerSize + size)
                break;

            handleMessage (type, ByteView (header + headerSize, size));
            offset += headerSize + size;
        }
        received.erase (received.begin(), received.begin() + static_cast<std::ptrdiff_t> (offset));
    }

    void handleMessage (MessageType type, ByteView payload)
    {
        switch (type)
        {
            case MessageType::serverAuthChallenge: handleChallenge (payload); break;
            case MessageType::serverAuthReply: handleAuthReply (payload); break;
            case MessageType::serverConfigChangeNotify: handleConfig (payload); break;
            case MessageType::serverUserInfoChangeNotify: handleUserInfo (payload); break;
            case MessageType::serverDownloadIntervalBegin: handleDownloadBegin (payload); break;
            case MessageType::serverDownloadIntervalWrite: handleDownloadWrite (payload); break;
            case MessageType::chatMessage: handleChat (payload); break;
            default: break; // keepalives, and anything newer than us
        }
    }

    void handleChallenge (ByteView payload)
    {
        const auto parsed = parseAuthChallenge (payload);
        if (! parsed || state != State::awaitingChallenge)
            return;

        if (parsed->protocolVersion < protocolVersionMin || parsed->protocolVersion > protocolVersionMax)
        {
            fail ("The server speaks an unsupported protocol version");
            return;
        }

        challenge = *parsed;
        keepAliveSeconds = challenge.keepAliveSeconds() > 0 ? challenge.keepAliveSeconds() : 3;

        if (challenge.licenceAgreement)
        {
            state = State::awaitingLicence;
            event (makeEvent (JamEvent::Type::licence, *challenge.licenceAgreement));
            return;
        }
        sendAuth (0);
    }

    void sendAuth (std::uint32_t capabilities)
    {
        loginName = settings.password.empty() ? "anonymous:" + settings.userName : settings.userName;
        send (makeAuthUser (loginName, authPassHash (loginName, settings.password, challenge.challenge), capabilities));
        state = State::authenticating;
    }

    void handleAuthReply (ByteView payload)
    {
        const auto reply = parseAuthReply (payload);
        if (! reply || state != State::authenticating)
            return;

        if (! reply->succeeded)
        {
            fail (reply->message.empty() ? "The server refused the login" : reply->message);
            return;
        }

        ourName = reply->message.empty() ? settings.userName : reply->message;
        state = State::connected;
        owner.connected.store (true);

        const int count = std::min (static_cast<int> (settings.channels.size()),
                                    std::min (reply->maxChannels, audio::AudioEngine::maxLocalChannels));
        std::vector<LocalChannelInfo> infos;
        for (int i = 0; i < count; ++i)
            infos.push_back ({ settings.channels[static_cast<size_t> (i)].name, 0, 0, 0 });
        send (makeSetChannelInfo (infos));
        numChannels = count;

        auto connectedEvent = makeEvent (JamEvent::Type::connected);
        connectedEvent.user = ourName;
        connectedEvent.number = reply->maxChannels;
        event (std::move (connectedEvent));
    }

    void handleConfig (ByteView payload)
    {
        const auto config = parseConfigChange (payload);
        if (! config)
            return;

        audio.setTempo (config->bpm, config->bpi);
        auto tempo = makeEvent (JamEvent::Type::tempo);
        tempo.number = config->bpm;
        tempo.number2 = config->bpi;
        event (std::move (tempo));
    }

    void handleUserInfo (ByteView payload)
    {
        const auto records = parseUserInfoChange (payload);
        if (! records)
            return;

        std::vector<std::pair<std::string, std::uint32_t>> masks;
        for (const auto& info : *records)
        {
            if (info.channelIndex < 0 || info.channelIndex >= maxUserChannels || info.userName == ourName)
                continue;

            auto& user = users[info.userName];
            auto& channel = user.channels[static_cast<size_t> (info.channelIndex)];
            const auto bit = 1u << info.channelIndex;

            JamEvent change;
            change.user = info.userName;
            change.channelIndex = info.channelIndex;
            change.channel = info.channelName;

            if (info.active)
            {
                const bool isNew = ! channel.present;
                channel.present = true;
                channel.name = info.channelName;
                channel.flags = info.flags;
                if (channel.slot < 0)
                    channel.slot = allocateSlot();

                if (isNew && (info.flags & channelFlags::noDefaultSubscribe) == 0)
                {
                    user.subscribed |= bit;
                    masks.emplace_back (info.userName, user.subscribed);
                }

                change.type = isNew ? JamEvent::Type::channelAdded : JamEvent::Type::channelChanged;
                change.slot = channel.slot;
                event (std::move (change));
            }
            else
            {
                auto found = users.find (info.userName);
                if (found == users.end() || ! channel.present)
                    continue;

                change.type = JamEvent::Type::channelRemoved;
                change.slot = channel.slot;
                dropDownloadsFor (info.userName, info.channelIndex);
                releaseSlot (channel.slot);
                channel = {};
                user.subscribed &= ~bit;
                event (std::move (change));

                const bool anyLeft = std::any_of (user.channels.begin(), user.channels.end(), [] (const auto& c) { return c.present; });
                if (! anyLeft)
                {
                    auto left = makeEvent (JamEvent::Type::userLeft);
                    left.user = info.userName;
                    users.erase (found);
                    event (std::move (left));
                }
            }
        }

        if (! masks.empty())
            send (makeSetUserMask (masks));
    }

    //==============================================================================
    void handleDownloadBegin (ByteView payload)
    {
        const auto begin = parseDownloadIntervalBegin (payload);
        if (! begin || begin->channelIndex < 0 || begin->channelIndex >= maxUserChannels)
            return;

        const auto user = users.find (begin->userName);
        if (user == users.end())
            return;

        const auto& channel = user->second.channels[static_cast<size_t> (begin->channelIndex)];
        if (! channel.present || channel.slot < 0)
            return;

        // Voice chat and session channels aren't interval based; JamTabaYUP doesn't play them
        if ((channel.flags & (channelFlags::voiceChat | channelFlags::session)) != 0)
            return;

        if (isZero (begin->guid))
        {
            // A silent interval: queue silence
            audio.getSlot (channel.slot).queue.push (nullptr);
            return;
        }

        if (begin->fourcc != fourccOggVorbis)
            return;

        auto download = std::make_unique<Download>();
        download->guid = begin->guid;
        download->userName = begin->userName;
        download->channelIndex = begin->channelIndex;
        download->slot = channel.slot;

        // Room for the whole interval at the device rate, with some slack for
        // tempo changes; anything past the end is dropped
        const double rate = audio.getSampleRate();
        const int expected = audio.getSamplesPerInterval() > 0 ? audio.getSamplesPerInterval() : static_cast<int> (rate * 30.0);
        const int capacity = std::min (static_cast<int> (rate * 600.0), expected + expected / 4 + static_cast<int> (rate));
        owner.intervals.push_back (std::make_unique<audio::RemoteInterval> (capacity));
        download->interval = owner.intervals.back().get();

        downloads.push_back (std::move (download));
    }

    void handleDownloadWrite (ByteView payload)
    {
        const auto write = parseIntervalWrite (payload);
        if (! write)
            return;

        const auto found = std::find_if (downloads.begin(), downloads.end(), [&] (const auto& d) { return d->guid == write->guid; });
        if (found == downloads.end())
            return;

        auto& download = **found;
        download.bytes += write->data.size();
        download.decoder.push (write->data.data(), write->data.size());
        decode (download, write->last);

        if (! download.queued && (download.bytes >= playAfterBytes || write->last))
            queueDownload (download);

        if (write->last || download.decoder.hasFailed())
        {
            auto receivedEvent = makeEvent (JamEvent::Type::intervalReceived);
            receivedEvent.user = download.userName;
            receivedEvent.channelIndex = download.channelIndex;
            receivedEvent.slot = download.slot;
            receivedEvent.number = static_cast<int> (download.bytes);
            event (std::move (receivedEvent));

            download.interval->downloading = false;
            downloads.erase (found);
        }
    }

    void decode (Download& download, bool last)
    {
        decodedLeft.clear();
        decodedRight.clear();
        download.decoder.pull (decodedLeft, decodedRight);

        if (download.resampler == nullptr && download.decoder.getSampleRate() > 0)
            download.resampler = std::make_unique<codec::StreamResampler> (download.decoder.getSampleRate(), audio.getSampleRate());
        if (download.resampler == nullptr)
            return;

        resampledLeft.clear();
        resampledRight.clear();
        download.resampler->process (decodedLeft.data(), decodedRight.data(), static_cast<int> (decodedLeft.size()), resampledLeft, resampledRight);
        if (last)
            download.resampler->flush (resampledLeft, resampledRight);

        auto& interval = *download.interval;
        const int start = interval.framesReady.load (std::memory_order_relaxed);
        const int count = std::min (static_cast<int> (resampledLeft.size()), interval.capacity() - start);
        if (count <= 0)
            return;

        std::copy_n (resampledLeft.begin(), count, interval.left.begin() + start);
        std::copy_n (resampledRight.begin(), count, interval.right.begin() + start);
        interval.framesReady.store (start + count, std::memory_order_release);
    }

    void queueDownload (Download& download)
    {
        download.queued = true;
        auto& slot = audio.getSlot (download.slot);
        if (slot.state.load() == audio::RemoteSlot::active && slot.queue.push (download.interval))
            download.interval->inAudio = true;
    }

    void dropDownloadsFor (const std::string& userName, int channelIndex)
    {
        for (auto it = downloads.begin(); it != downloads.end();)
        {
            if ((*it)->userName == userName && (*it)->channelIndex == channelIndex)
            {
                (*it)->interval->downloading = false;
                it = downloads.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

    //==============================================================================
    void handleChat (ByteView payload)
    {
        const auto chat = parseChatMessage (payload);
        if (! chat)
            return;

        const auto& p = chat->params;
        JamEvent e;
        e.user = p[1];
        if (p[0] == "MSG")
        {
            e.type = JamEvent::Type::chat;
            e.text = p[2];
        }
        else if (p[0] == "PRIVMSG")
        {
            e.type = JamEvent::Type::privateChat;
            e.text = p[2];
        }
        else if (p[0] == "TOPIC")
        {
            e.type = JamEvent::Type::topic;
            e.text = p[2];
        }
        else if (p[0] == "JOIN")
        {
            e.type = JamEvent::Type::userJoined;
        }
        else if (p[0] == "PART")
        {
            e.type = JamEvent::Type::userParted;
        }
        else if (p[0] == "USERCOUNT")
        {
            e.type = JamEvent::Type::userCount;
            e.user.clear();
            e.number = yup::String (p[1]).getIntValue();
            e.number2 = yup::String (p[2]).getIntValue();
        }
        else
        {
            return;
        }
        event (std::move (e));
    }

    //==============================================================================
    void encodeCapturedAudio()
    {
        auto& c = *chunk;
        while (audio.popInputChunk (c))
        {
            if ((c.flags & audio::InputChunk::intervalStart) != 0)
                startUploads (c.transmitMask);

            if (c.numFrames <= 0)
                continue;

            for (int i = 0; i < numChannels; ++i)
            {
                auto& upload = uploads[static_cast<size_t> (i)];
                if (upload.encoder == nullptr || (c.transmitMask & (1u << i)) == 0)
                    continue;

                const float* in[2] { c.samples[i][0], c.samples[i][1] };
                if (upload.resampler != nullptr)
                {
                    resampledLeft.clear();
                    resampledRight.clear();
                    upload.resampler->process (in[0], in[1], c.numFrames, resampledLeft, resampledRight);
                    const float* converted[2] { resampledLeft.data(), resampledRight.data() };
                    upload.encoder->encode (converted, static_cast<int> (resampledLeft.size()), upload.pending);
                }
                else
                {
                    upload.encoder->encode (in, c.numFrames, upload.pending);
                }
                sendPending (upload, i, false);
            }
        }
    }

    void startUploads (std::uint32_t transmitMask)
    {
        for (int i = 0; i < numChannels; ++i)
        {
            auto& upload = uploads[static_cast<size_t> (i)];
            finishUpload (upload, i);

            if ((transmitMask & (1u << i)) == 0)
            {
                send (makeUploadIntervalBegin (Guid {}, 0, i)); // a silent interval
                continue;
            }

            const auto& setup = settings.channels[static_cast<size_t> (i)];
            const double deviceRate = audio.getSampleRate();
            const int encodeRate = deviceRate > maxEncodeRate ? maxEncodeRate : static_cast<int> (deviceRate);
            upload.channels = setup.stereo ? 2 : 1;
            upload.encoder = std::make_unique<codec::VorbisEncoder> (upload.channels, encodeRate, settings.encoderQuality);
            upload.resampler = deviceRate > maxEncodeRate ? std::make_unique<codec::StreamResampler> (deviceRate, encodeRate) : nullptr;
            if (! upload.encoder->isValid())
            {
                upload.encoder.reset();
                continue;
            }

            yup::Random::getSystemRandom().fillBitsRandomly (upload.guid.data(), upload.guid.size());
            upload.bytes = 0;
            send (makeUploadIntervalBegin (upload.guid, fourccOggVorbis, i));
        }
    }

    void finishUpload (Upload& upload, int channelIndex)
    {
        if (upload.encoder == nullptr)
            return;

        if (upload.resampler != nullptr)
        {
            resampledLeft.clear();
            resampledRight.clear();
            upload.resampler->flush (resampledLeft, resampledRight);
            const float* converted[2] { resampledLeft.data(), resampledRight.data() };
            upload.encoder->encode (converted, static_cast<int> (resampledLeft.size()), upload.pending);
        }

        upload.encoder->finish (upload.pending);
        sendPending (upload, channelIndex, true);
        upload.encoder.reset();
        upload.resampler.reset();

        auto sent = makeEvent (JamEvent::Type::intervalSent);
        sent.channelIndex = channelIndex;
        sent.number = static_cast<int> (upload.bytes);
        event (std::move (sent));
    }

    void sendPending (Upload& upload, int, bool last)
    {
        std::size_t offset = 0;
        while (upload.pending.size() - offset > 0 || last)
        {
            const auto size = std::min (maxWriteChunk, upload.pending.size() - offset);
            const bool final = last && offset + size == upload.pending.size();
            send (makeUploadIntervalWrite (upload.guid, ByteView (upload.pending.data() + offset, size), final));
            upload.bytes += size;
            offset += size;
            if (final)
                break;
        }
        upload.pending.clear();
    }

    //==============================================================================
    int allocateSlot()
    {
        for (int i = 0; i < audio::AudioEngine::maxRemoteSlots; ++i)
        {
            auto& slot = audio.getSlot (i);
            if (slot.state.load() != audio::RemoteSlot::free || slotOwned[static_cast<size_t> (i)])
                continue;

            slot.controls.gain.store (1.0f);
            slot.controls.pan.store (0.0f);
            slot.controls.mute.store (false);
            slot.controls.solo.store (false);
            slot.meter.reset();
            slotOwned[static_cast<size_t> (i)] = true;
            slot.state.store (audio::RemoteSlot::active, std::memory_order_release);
            return i;
        }
        return -1;
    }

    void releaseSlot (int index)
    {
        if (index < 0)
            return;
        slotOwned[static_cast<size_t> (index)] = false;
        audio.getSlot (index).state.store (audio::RemoteSlot::releasing, std::memory_order_release);
    }

    //==============================================================================
    void send (const Message& message)
    {
        const auto bytes = frame (message);
        if (socket.write (bytes.data(), static_cast<int> (bytes.size())) != static_cast<int> (bytes.size()))
        {
            fail ("Sending to the server failed");
            return;
        }
        lastSend = nowSeconds();
    }

    void keepAlive()
    {
        const auto now = nowSeconds();
        if (now - lastSend >= keepAliveSeconds)
            send (makeKeepAlive());

        // The server keeps alive at least as often as it asks us to
        const double timeout = state == State::awaitingLicence ? 600.0 : keepAliveSeconds * 4.0 + 10.0;
        if (now - lastReceive > timeout)
            fail ("The server stopped responding");
    }

    void runCommands()
    {
        std::vector<std::function<void (Worker&)>> toRun;
        {
            std::lock_guard lock (commandsLock);
            toRun.swap (commands);
        }
        for (auto& command : toRun)
            command (*this);
    }

    void fail (const std::string& reason)
    {
        if (! failed)
        {
            failed = true;
            failure = reason;
        }
    }

    void finish (const std::string& reason)
    {
        audio.stopClock();
        for (auto& download : downloads)
            download->interval->downloading = false;
        downloads.clear();
        for (int i = 0; i < audio::AudioEngine::maxRemoteSlots; ++i)
            if (slotOwned[static_cast<size_t> (i)])
                releaseSlot (i);
        for (auto& upload : uploads)
        {
            upload.encoder.reset();
            upload.resampler.reset();
        }
        socket.close();
        owner.connected.store (false);
        state = State::connecting;

        auto e = makeEvent (JamEvent::Type::disconnected);
        e.text = reason;
        event (std::move (e));
    }

    void event (JamEvent e)
    {
        owner.pushEvent (std::move (e));
    }

    JamEngine& owner;
    audio::AudioEngine& audio;
    ConnectionSettings settings;

    yup::StreamingSocket socket;
    std::vector<std::uint8_t> received;
    State state = State::connecting;
    AuthChallenge challenge;
    int keepAliveSeconds = 3;
    double lastSend = 0.0, lastReceive = 0.0;
    bool failed = false;
    std::string failure;
    std::string loginName, ourName;

    std::map<std::string, RemoteUser> users;
    std::vector<std::unique_ptr<Download>> downloads;
    std::array<Upload, audio::AudioEngine::maxLocalChannels> uploads;
    std::array<bool, audio::AudioEngine::maxRemoteSlots> slotOwned {};
    int numChannels = 0;

    std::unique_ptr<audio::InputChunk> chunk;
    std::vector<float> decodedLeft, decodedRight, resampledLeft, resampledRight;

    std::mutex commandsLock;
    std::vector<std::function<void (Worker&)>> commands;
};

//==============================================================================
JamEngine::JamEngine() = default;

JamEngine::~JamEngine()
{
    disconnect();
}

void JamEngine::connect (const ConnectionSettings& settings)
{
    disconnect();

    const int count = std::min (static_cast<int> (settings.channels.size()), audio::AudioEngine::maxLocalChannels);
    audioEngine.setNumLocalChannels (count);
    for (int i = 0; i < count; ++i)
    {
        const auto& setup = settings.channels[static_cast<size_t> (i)];
        auto& local = audioEngine.getLocalChannel (i);
        local.firstInput.store (setup.firstInput);
        local.stereo.store (setup.stereo);
        local.transmit.store (setup.transmit);
    }

    worker = std::make_unique<Worker> (*this, settings);
    worker->startThread();
}

void JamEngine::disconnect()
{
    if (worker != nullptr)
    {
        worker->signalThreadShouldExit();
        worker->stopThread (5000);
        worker.reset();
    }
    connected.store (false);
}

bool JamEngine::isRunning() const noexcept
{
    return worker != nullptr && worker->isThreadRunning();
}

void JamEngine::post (std::function<void (Worker&)> command)
{
    if (worker != nullptr)
        worker->post (std::move (command));
}

void JamEngine::acceptLicence (bool accept)
{
    post ([accept] (Worker& w) { w.acceptLicence (accept); });
}

void JamEngine::sendChat (const std::string& text)
{
    post ([text] (Worker& w) { w.sendChat ({ "MSG", text }); });
}

void JamEngine::sendPrivateChat (const std::string& user, const std::string& text)
{
    post ([user, text] (Worker& w) { w.sendChat ({ "PRIVMSG", user, text }); });
}

void JamEngine::voteBpm (int bpm)
{
    sendChat ("!vote bpm " + std::to_string (bpm));
}

void JamEngine::voteBpi (int bpi)
{
    sendChat ("!vote bpi " + std::to_string (bpi));
}

void JamEngine::setReceiving (const std::string& user, int channelIndex, bool receive)
{
    post ([user, channelIndex, receive] (Worker& w) { w.setReceiving (user, channelIndex, receive); });
}

void JamEngine::setTransmitting (int localChannel, bool transmit)
{
    if (localChannel >= 0 && localChannel < audio::AudioEngine::maxLocalChannels)
        audioEngine.getLocalChannel (localChannel).transmit.store (transmit);
}

void JamEngine::pushEvent (JamEvent event)
{
    std::lock_guard lock (eventsLock);
    events.push_back (std::move (event));
}

void JamEngine::pollEvents (const std::function<void (const JamEvent&)>& handler)
{
    std::deque<JamEvent> toHandle;
    {
        std::lock_guard lock (eventsLock);
        toHandle.swap (events);
    }
    for (const auto& e : toHandle)
        handler (e);
}

void JamEngine::collectRetiredIntervals()
{
    audio::RemoteInterval* interval = nullptr;
    while (audioEngine.popRetired (interval))
        interval->inAudio = false;

    std::erase_if (intervals, [] (const auto& i) { return ! i->inAudio && ! i->downloading; });
}

} // namespace jamtaba
