#pragma once

#include <windows.h>

struct Settings {
    // 1, 2, 4 or 8. Each mirror pass renders at this multiple of the mirror
    // texture size and is reduced back by halving.
    int supersample = 4;
};

// Creates MirrorAntiAliasingFix.ini next to the plugin from the embedded
// canonical file when it is missing, then reads it.
Settings LoadSettings(HMODULE module);
