/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "ui/ServerBrowser.h"

#include <algorithm>

namespace jamtaba::ui
{

namespace
{
constexpr double refreshSeconds = 30.0;
constexpr float rowHeight = 64.0f;
constexpr float rowGap = 8.0f;

double now()
{
    return yup::Time::getMillisecondCounterHiRes() / 1000.0;
}

/** Names the server list counts as bots rather than musicians. */
bool isBot (const std::string& name)
{
    const auto lower = yup::String (name).toLowerCase();
    return lower.startsWith ("ninbot") || lower.startsWith ("jambot") || lower.contains ("ninjamer.com") || lower == "jammr";
}
} // namespace

//==============================================================================
class ServerBrowser::RoomList final : public yup::Component
{
public:
    std::function<void (const net::PublicServer&)> onJoin;

    void setServers (std::vector<net::PublicServer> newServers)
    {
        servers = std::move (newServers);
        scroll = std::clamp (scroll, 0.0f, maxScroll());
        repaint();
    }

    bool isEmpty() const noexcept { return servers.empty(); }

    void paint (yup::Graphics& g) override
    {
        const float width = getWidth();
        for (size_t i = 0; i < servers.size(); ++i)
        {
            const auto row = rowBounds (i);
            if (row.getBottom() < 0.0f || row.getY() > getHeight())
                continue;

            const auto& server = servers[i];
            int musicians = 0;
            yup::String names;
            for (const auto& user : server.users)
            {
                if (isBot (user))
                    continue;
                ++musicians;
                names << (names.isEmpty() ? "" : ", ") << theme::str (user.substr (0, user.find ('@')));
            }
            if (server.users.empty())
                musicians = server.userCount;

            const bool empty = musicians == 0;
            const bool hovered = static_cast<int> (i) == hoverRow;
            g.setFillColor (hovered ? theme::panelRaised : theme::panel);
            g.fillRoundedRect (row, 8.0f);

            auto content = row.reduced (16.0f, 10.0f);
            const auto joinArea = content.removeFromRight (96.0f).withSizeKeepingCenter (96.0f, 34.0f);
            g.setFillColor (hovered ? theme::accent : theme::panelRaised.brighter (0.08f));
            g.fillRoundedRect (joinArea, 6.0f);
            drawText (g, "Join", theme::font (14.0f), joinArea, hovered ? theme::background : theme::text, yup::Justification::center);

            // Fullness: one dot per seat
            content.removeFromRight (14.0f);
            auto seats = content.removeFromRight (std::min (150.0f, width * 0.2f));
            const int maxSeats = server.maxUsers > 0 ? server.maxUsers : std::max (musicians, 8);
            drawText (g, yup::String (musicians) + " / " + (server.maxUsers > 0 ? yup::String (server.maxUsers) : yup::String ("?")),
                      theme::font (15.0f), seats.removeFromTop (seats.getHeight() * 0.55f), empty ? theme::faint : theme::text,
                      yup::Justification::centerRight);
            const float dot = 6.0f;
            float x = seats.getRight() - dot;
            for (int s = maxSeats - 1; s >= 0 && x > seats.getX(); --s, x -= dot + 3.0f)
            {
                g.setFillColor (s < musicians ? theme::accent : theme::track);
                g.fillEllipse (x, seats.getCenterY() - dot * 0.5f, dot, dot);
            }

            content.removeFromRight (16.0f);
            auto top = content.removeFromTop (content.getHeight() * 0.5f);
            const auto tempo = top.removeFromRight (150.0f);
            drawText (g, theme::str (server.address()), theme::font (16.0f), top, empty ? theme::dim : theme::text);
            drawText (g, yup::String (server.bpm) + " BPM   " + yup::String (server.bpi) + " BPI", theme::monoFont (13.0f), tempo,
                      empty ? theme::faint : theme::dim, yup::Justification::centerRight);
            drawText (g, empty ? yup::String ("Nobody here yet") : names, theme::font (13.0f), content, empty ? theme::faint : theme::dim);
        }

        if (servers.empty())
            drawText (g, "Looking for public servers...", theme::font (14.0f), getLocalBounds(), theme::dim, yup::Justification::center);
    }

    void mouseMove (const yup::MouseEvent& event) override { setHover (rowAt (event.getPosition())); }
    void mouseExit (const yup::MouseEvent&) override { setHover (-1); }

    void mouseUp (const yup::MouseEvent& event) override
    {
        const int row = rowAt (event.getPosition());
        if (row >= 0 && onJoin)
            onJoin (servers[static_cast<size_t> (row)]);
    }

    bool mouseWheel (const yup::MouseEvent& event, const yup::MouseWheelData& wheel) override
    {
        scroll = std::clamp (scroll - wheel.getDeltaY() * 40.0f, 0.0f, maxScroll());
        setHover (rowAt (event.getPosition()));
        repaint();
        return true;
    }

private:
    yup::Rectangle<float> rowBounds (size_t index) const
    {
        return { 0.0f, static_cast<float> (index) * (rowHeight + rowGap) - scroll, getWidth(), rowHeight };
    }

    int rowAt (yup::Point<float> position) const
    {
        for (size_t i = 0; i < servers.size(); ++i)
            if (rowBounds (i).contains (position))
                return static_cast<int> (i);
        return -1;
    }

    float maxScroll() const
    {
        return std::max (0.0f, static_cast<float> (servers.size()) * (rowHeight + rowGap) - getHeight());
    }

    void setHover (int row)
    {
        if (row != hoverRow)
        {
            hoverRow = row;
            repaint();
        }
    }

    std::vector<net::PublicServer> servers;
    float scroll = 0.0f;
    int hoverRow = -1;
};

//==============================================================================
ServerBrowser::ServerBrowser (JamProcessor& p)
    : yup::Component ("ServerBrowser")
    , processor (p)
    , rooms (std::make_unique<RoomList>())
{
    setOpaque (true);

    const auto& settings = processor.getSettings();
    nameField.setText (theme::str (settings.userName), yup::dontSendNotification);
    channelField.setText (theme::str (settings.channelName), yup::dontSendNotification);
    addressField.setText (theme::str (settings.lastServer), yup::dontSendNotification);

    for (auto* c : std::initializer_list<yup::Component*> { &nameField, &channelField, &addressField, &passwordField,
                                                           &connectButton, &refreshButton, rooms.get() })
        addAndMakeVisible (c);

    connectButton.onClick = [this] { joinCustom(); };
    addressField.onReturn = [this] { joinCustom(); };
    passwordField.onReturn = [this] { joinCustom(); };
    refreshButton.onClick = [this] { processor.getServerList().requestRefresh(); };
    rooms->onJoin = [this] (const net::PublicServer& server) { join (server.host, server.port, {}); };

    const auto& model = processor.getModel();
    if (! model.lastError.empty())
        setStatus (theme::str (model.lastError), true);

    rooms->setServers (processor.getServerList().getServers());
    seenVersion = processor.getServerList().getVersion();
    processor.getServerList().requestRefresh();
    nextRefresh = now() + refreshSeconds;
}

ServerBrowser::~ServerBrowser() = default;

bool ServerBrowser::storeName()
{
    auto name = nameField.getText().trim().removeCharacters (" @:");
    auto& settings = processor.getSettings();
    settings.channelName = channelField.getText().trim().toStdString();
    if (settings.channelName.empty())
        settings.channelName = "my channel";

    if (name.isEmpty())
    {
        setStatus ("Choose a name first, so the others know who you are.", true);
        nameField.focus();
        return false;
    }
    settings.userName = name.substring (0, 32).toStdString();
    return true;
}

void ServerBrowser::join (const std::string& host, int port, const std::string& password)
{
    if (! storeName())
        return;
    setStatus ("Connecting to " + theme::str (host) + ":" + yup::String (port) + "...", false);
    processor.connectTo (host, port, password);
}

void ServerBrowser::joinCustom()
{
    const auto address = addressField.getText().trim();
    const auto host = address.containsChar (':') ? address.upToLastOccurrenceOf (":", false, false) : address;
    const int port = address.containsChar (':') ? address.fromLastOccurrenceOf (":", false, false).getIntValue() : 2049;
    if (host.isEmpty() || port <= 0 || port > 65535)
    {
        setStatus ("Type a server address as host:port, for example ninbot.com:2049.", true);
        addressField.focus();
        return;
    }
    join (host.toStdString(), port, passwordField.getText().toStdString());
}

void ServerBrowser::setStatus (yup::String text, bool isError)
{
    status = std::move (text);
    statusIsError = isError;
    repaint();
}

void ServerBrowser::refreshDisplay (double)
{
    auto& service = processor.getServerList();
    if (service.getVersion() != seenVersion)
    {
        seenVersion = service.getVersion();
        rooms->setServers (service.getServers());
        if (service.lastFetchFailed() && rooms->isEmpty())
            setStatus ("Couldn't load the public server list. Check your internet connection, or connect to a server by address.", true);
    }

    if (service.isFetching() != wasFetching)
    {
        wasFetching = service.isFetching();
        repaint();
    }

    if (now() >= nextRefresh)
    {
        nextRefresh = now() + refreshSeconds;
        service.requestRefresh();
    }
}

void ServerBrowser::resized()
{
    auto area = getLocalBounds().reduced (28.0f, 20.0f);
    area.removeFromTop (44.0f); // title

    auto form = area.removeFromTop (36.0f);
    nameField.setBounds (form.removeFromLeft (200.0f));
    form.removeFromLeft (10.0f);
    channelField.setBounds (form.removeFromLeft (180.0f));
    form.removeFromLeft (28.0f);
    connectButton.setBounds (form.removeFromRight (110.0f));
    form.removeFromRight (10.0f);
    passwordField.setBounds (form.removeFromRight (std::min (190.0f, form.getWidth() * 0.4f)));
    form.removeFromRight (10.0f);
    addressField.setBounds (form);

    area.removeFromTop (14.0f);
    auto heading = area.removeFromTop (30.0f);
    refreshButton.setBounds (heading.removeFromRight (90.0f).reduced (0.0f, 2.0f));
    area.removeFromTop (34.0f); // heading text and status line

    rooms->setBounds (area);
}

void ServerBrowser::paint (yup::Graphics& g)
{
    g.setFillColor (theme::background);
    g.fillAll();

    auto area = getLocalBounds().reduced (28.0f, 20.0f);
    auto title = area.removeFromTop (44.0f);
    drawText (g, "JamTabaYUP", theme::font (24.0f), title.removeFromLeft (200.0f), theme::text);
    drawText (g, "Jam online with musicians around the world on NINJAM servers", theme::font (13.0f), title, theme::dim);

    area.removeFromTop (36.0f + 14.0f);
    auto heading = area.removeFromTop (30.0f);
    heading.removeFromRight (100.0f);
    auto& service = processor.getServerList();
    drawText (g, service.isFetching() ? "Public rooms  (refreshing...)" : "Public rooms", theme::font (17.0f), heading, theme::text);

    const auto statusArea = area.removeFromTop (34.0f).withTrimmedBottom (8.0f);
    drawText (g, status, theme::font (13.0f), statusArea, statusIsError ? theme::transmit : theme::dim);
}

} // namespace jamtaba::ui
