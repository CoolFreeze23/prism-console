// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QtGlobal>

// Whether Prism presents the controller interface instead of the desktop window.
// PRISM_DESKTOP or PRISM_CONSOLE force a mode; otherwise SteamOS Gaming Mode (gamescope) selects it.
// Builds without the console UI never use it.
inline bool consoleMode()
{
#ifdef PRISM_CONSOLE_UI
    static const bool enabled = [] {
        if (qEnvironmentVariableIsSet("PRISM_DESKTOP"))
            return false;
        if (qEnvironmentVariableIsSet("PRISM_CONSOLE"))
            return true;
        return qgetenv("XDG_CURRENT_DESKTOP") == "gamescope" || qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY") ||
               qgetenv("SteamGamepadUI") == "1";
    }();
    return enabled;
#else
    return false;
#endif
}
