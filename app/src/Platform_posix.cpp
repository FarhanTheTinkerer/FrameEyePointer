#include "Platform.h"

#include <csignal>
#include <limits.h>
#include <unistd.h>

namespace app::platform {

namespace {
std::atomic<bool>* g_quit = nullptr;

void onSignal(int) {
    if (g_quit) g_quit->store(true);
}
} // namespace

void initProcess(bool) {}

std::string executableDir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string path(buf, static_cast<size_t>(n));
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

uint32_t processId() { return static_cast<uint32_t>(getpid()); }

void installQuitHandler(std::atomic<bool>* quit) {
    g_quit = quit;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
}

// Desktop mouse control is Windows-only for now. The pointer still works for
// overlays; desktop-mode targets just do not move the cursor.
std::vector<eyepointer::Monitor> monitors() { return {}; }

bool mouseSupported() { return false; }

void sendMouse(const std::vector<eyepointer::MouseCommand>&) {}

} // namespace app::platform
