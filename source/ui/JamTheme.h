/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include <yup_gui/yup_gui.h>

#include <string>

namespace jamtaba::theme
{

// Dark, with one teal accent; transmit is red, receive is green
inline constexpr yup::Color background { 0xff14171d };
inline constexpr yup::Color panel { 0xff1c2028 };
inline constexpr yup::Color panelRaised { 0xff242934 };
inline constexpr yup::Color hairline { 0xff2e3440 };
inline constexpr yup::Color track { 0xff323947 };
inline constexpr yup::Color text { 0xffe3e7ef };
inline constexpr yup::Color dim { 0xff9aa3b5 };
inline constexpr yup::Color faint { 0xff5f687a };
inline constexpr yup::Color accent { 0xff3fb8a8 };
inline constexpr yup::Color transmit { 0xffe0524f };
inline constexpr yup::Color receive { 0xff5cc46a };
inline constexpr yup::Color mute { 0xffe0a03c };
inline constexpr yup::Color solo { 0xff4f8fe0 };
inline constexpr yup::Color meterLow { 0xff4cc27a };
inline constexpr yup::Color meterMid { 0xffd8c444 };
inline constexpr yup::Color meterHigh { 0xffe0524f };

/** One colour per remote user, cycled. */
inline yup::Color userColor (int index)
{
    static constexpr yup::uint32 colors[] = { 0xff5fa8e8, 0xffe8935f, 0xffa27be8, 0xff6fd0a0,
                                              0xffe86f9a, 0xffd6c35a, 0xff5fd0d6, 0xffb5d65f };
    return yup::Color (colors[static_cast<size_t> (index) % std::size (colors)]);
}

inline yup::Font font (float height)
{
    return yup::ApplicationTheme::getGlobalTheme()->getDefaultFont().withHeight (height);
}

inline yup::Font monoFont (float height)
{
    return yup::ApplicationTheme::getGlobalTheme()->getDefaultMonospaceFont().withHeight (height);
}

inline yup::String str (const std::string& utf8)
{
    return yup::String::fromUTF8 (utf8.c_str(), static_cast<int> (utf8.size()));
}

} // namespace jamtaba::theme
