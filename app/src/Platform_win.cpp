#include "Platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

namespace app::platform {

namespace {

std::atomic<bool>* g_quit = nullptr;

BOOL WINAPI consoleHandler(DWORD) {
    if (g_quit) g_quit->store(true);
    return TRUE;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                        nullptr);
    return s;
}

BOOL CALLBACK monitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* out = reinterpret_cast<std::vector<eyepointer::Monitor>*>(data);
    MONITORINFO info{};
    info.cbSize = sizeof info;
    if (GetMonitorInfoW(monitor, &info)) {
        const RECT& r = info.rcMonitor;
        eyepointer::Monitor m;
        m.rect = {r.left, r.top, r.right - r.left, r.bottom - r.top};
        m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        out->push_back(m);
    }
    return TRUE;
}

void sendButton(DWORD flags, DWORD data = 0) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    in.mi.mouseData = data;
    SendInput(1, &in, sizeof in);
}

} // namespace

void initProcess(bool wantConsole) {
    // Per-monitor DPI awareness v2, looked up dynamically (Windows 10 1703+).
    using SetCtx = BOOL(WINAPI*)(HANDLE);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto fn = reinterpret_cast<SetCtx>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (!fn || !fn(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)))) SetProcessDPIAware();
    }

    if (wantConsole) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        SetConsoleOutputCP(CP_UTF8);
    }
}

std::string executableDir() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    size_t slash = path.find_last_of(L"\\/");
    return toUtf8(slash == std::wstring::npos ? L"." : path.substr(0, slash));
}

uint32_t processId() { return GetCurrentProcessId(); }

void installQuitHandler(std::atomic<bool>* quit) {
    g_quit = quit;
    SetConsoleCtrlHandler(consoleHandler, TRUE);
}

std::vector<eyepointer::Monitor> monitors() {
    std::vector<eyepointer::Monitor> out;
    EnumDisplayMonitors(nullptr, nullptr, monitorProc, reinterpret_cast<LPARAM>(&out));
    return out;
}

bool mouseSupported() { return true; }

void sendMouse(const std::vector<eyepointer::MouseCommand>& commands) {
    using eyepointer::MouseButton;
    using eyepointer::MouseCommand;
    for (const auto& c : commands) {
        switch (c.type) {
        case MouseCommand::Type::Move:
            // With per-monitor DPI awareness these are physical pixels.
            SetCursorPos(c.position.x, c.position.y);
            break;
        case MouseCommand::Type::ButtonDown:
            sendButton(c.button == MouseButton::Left ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_RIGHTDOWN);
            break;
        case MouseCommand::Type::ButtonUp:
            sendButton(c.button == MouseButton::Left ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP);
            break;
        case MouseCommand::Type::Wheel:
            sendButton(MOUSEEVENTF_WHEEL, static_cast<DWORD>(c.wheelDelta));
            break;
        }
    }
}

} // namespace app::platform
