// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace FileIconHandlerWindows {
inline constexpr char ClassIdString[] = "{D325870F-4B1C-4D26-A03C-3575B1607635}";
inline constexpr char LibraryFileName[] = "8bitCodeFileIcons.dll";
}

#ifdef _WIN32
#include <guiddef.h>
namespace FileIconHandlerWindows {
inline constexpr CLSID ClassId = {0xd325870f, 0x4b1c, 0x4d26,
    {0xa0, 0x3c, 0x35, 0x75, 0xb1, 0x60, 0x76, 0x35}};
}
#endif
