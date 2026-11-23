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

namespace jamtaba::theme
{

inline constexpr yup::Color background { 0xff15181e };
inline constexpr yup::Color panel { 0xff1d2129 };
inline constexpr yup::Color hairline { 0xff2b313c };
inline constexpr yup::Color text { 0xffe1e5ee };
inline constexpr yup::Color dim { 0xff8d97a9 };
inline constexpr yup::Color accent { 0xff4fb3a5 };

inline yup::Font labelFont (float height)
{
    return yup::ApplicationTheme::getGlobalTheme()->getDefaultFont().withHeight (height);
}

} // namespace jamtaba::theme
