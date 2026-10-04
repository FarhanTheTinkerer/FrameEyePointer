#include "EyePointerApp.h"
#include "Log.h"
#include "Platform.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#ifndef FRAMEEYEPOINTER_VERSION
#define FRAMEEYEPOINTER_VERSION "dev"
#endif

namespace {

const char* kUsage =
    "FrameEyePointer " FRAMEEYEPOINTER_VERSION " - eye-tracked laser pointer for SteamVR\n"
    "\n"
    "Usage: FrameEyePointer [options]\n"
    "  --console          show a console window with the log\n"
    "  --probe            report eye tracking, devices, overlays and monitors, then exit\n"
    "  --autostart on|off start FrameEyePointer whenever SteamVR starts\n"
    "  --config FILE      settings file (default: FrameEyePointer.ini next to the exe)\n"
    "  --version          print the version\n"
    "  --help             this text\n";

} // namespace

int main(int argc, char** argv) {
    app::Options options;
    bool showHelp = false, showVersion = false, bad = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--console") options.console = true;
        else if (a == "--probe") options.probe = options.console = true;
        else if (a == "--help" || a == "-h") showHelp = true;
        else if (a == "--version") showVersion = true;
        else if (a == "--autostart" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "on") options.autostart = true;
            else if (v == "off") options.autostart = false;
            else bad = true;
        } else if (a == "--config" && i + 1 < argc) {
            options.configPath = argv[++i];
        } else {
            bad = true;
        }
    }
    if (showHelp || showVersion || bad) options.console = true;

    app::platform::initProcess(options.console);
    if (showVersion) {
        std::printf("FrameEyePointer %s\n", FRAMEEYEPOINTER_VERSION);
        return 0;
    }
    if (showHelp || bad) {
        std::fputs(kUsage, bad ? stderr : stdout);
        return bad ? 1 : 0;
    }

    std::string dir = app::platform::executableDir();
    app::logOpen((std::filesystem::u8path(dir) / "FrameEyePointer.log").u8string().c_str());
    app::logf("FrameEyePointer %s starting", FRAMEEYEPOINTER_VERSION);

    std::atomic<bool> quit{false};
    app::platform::installQuitHandler(&quit);

    int code;
    {
        app::EyePointerApp eyePointer(options);
        code = eyePointer.run(quit);
    }
    app::logClose();
    return code;
}
