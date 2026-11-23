/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#include "plugin/ChordProgression.h"

#include <algorithm>
#include <cctype>

namespace jamtaba
{

namespace
{
std::string_view trim (std::string_view text)
{
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.front())))
        text.remove_prefix (1);
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.back())))
        text.remove_suffix (1);
    return text;
}
} // namespace

bool ChordProgression::isChordSymbol (std::string_view token)
{
    if (token == "%" || token == "N.C." || token == "NC" || token == "-" || token == ".")
        return true;
    if (token.empty() || token.front() < 'A' || token.front() > 'G')
        return false;

    // After the root: accidentals, qualities, extensions, alterations and a slash bass
    static constexpr std::string_view allowed = "#bmMajindimaugsus0123456789+-()/°ø^Δ,";
    for (std::size_t i = 1; i < token.size(); ++i)
    {
        const char c = token[i];
        const bool bassNote = c >= 'A' && c <= 'G' && i > 0 && token[i - 1] == '/';
        if (! bassNote && allowed.find (c) == std::string_view::npos && static_cast<unsigned char> (c) < 0x80)
            return false;
    }
    return token.size() <= 16;
}

std::optional<ChordProgression> ChordProgression::parse (std::string_view text)
{
    const auto first = text.find ('|');
    const auto last = text.rfind ('|');
    if (first == std::string_view::npos || first == last)
        return std::nullopt;

    ChordProgression progression;
    auto body = text.substr (first + 1, last - first - 1);
    while (true)
    {
        const auto bar = body.find ('|');
        const auto measureText = trim (body.substr (0, bar));

        std::vector<std::string> chords;
        std::size_t position = 0;
        while (position < measureText.size())
        {
            while (position < measureText.size() && std::isspace (static_cast<unsigned char> (measureText[position])))
                ++position;
            auto end = position;
            while (end < measureText.size() && ! std::isspace (static_cast<unsigned char> (measureText[end])))
                ++end;
            if (end > position)
            {
                const auto token = measureText.substr (position, end - position);
                if (! isChordSymbol (token))
                    return std::nullopt;
                chords.emplace_back (token);
            }
            position = end;
        }

        if (! chords.empty())
            progression.measures.push_back (std::move (chords));

        if (bar == std::string_view::npos)
            break;
        body.remove_prefix (bar + 1);
    }

    if (progression.measures.size() < 2)
        return std::nullopt;
    return progression;
}

} // namespace jamtaba
