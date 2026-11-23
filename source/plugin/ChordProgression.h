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
#include <string_view>
#include <vector>

namespace jamtaba
{

/** A chord progression someone posted in chat, written with bars between
    measures: "| Am7 | D7 | Gmaj7 | Cmaj7 |". Each measure holds one or more
    chords separated by spaces. */
struct ChordProgression
{
    std::vector<std::vector<std::string>> measures;

    bool isEmpty() const noexcept { return measures.empty(); }

    /** Finds a progression in a chat line: at least two measures, and every
        token must look like a chord symbol. */
    static std::optional<ChordProgression> parse (std::string_view text);

    /** True for symbols like C, F#m7, Bbmaj7/D, G7(b9), Dsus4, E°, N.C. or a % repeat. */
    static bool isChordSymbol (std::string_view token);
};

} // namespace jamtaba
