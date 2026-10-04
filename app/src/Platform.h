#pragma once

#include "eyepointer/DesktopMapping.h"
#include "eyepointer/PointerController.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace app::platform {

/// Per-process setup: DPI awareness (so cursor coordinates are physical pixels)
/// and, if requested, a console window for log output.
void initProcess(bool wantConsole);

/// Directory containing the executable, UTF-8, without a trailing separator.
std::string executableDir();

uint32_t processId();

/// Sets `*quit` on Ctrl+C / console close / SIGTERM.
void installQuitHandler(std::atomic<bool>* quit);

/// Connected monitors in virtual-desktop physical pixels, in OS enumeration
/// order (this order defines `monitor:N` in the settings).
std::vector<eyepointer::Monitor> monitors();

/// Whether this platform can drive the OS mouse.
bool mouseSupported();

void sendMouse(const std::vector<eyepointer::MouseCommand>& commands);

} // namespace app::platform
