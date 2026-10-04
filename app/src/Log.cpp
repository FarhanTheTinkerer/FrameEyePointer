#include "Log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>

namespace app {

namespace {
std::FILE* g_file = nullptr;
std::mutex g_mutex;
} // namespace

void logOpen(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
#ifdef _WIN32
    g_file = _wfopen(std::filesystem::u8path(path).c_str(), L"w");
#else
    g_file = std::fopen(path.c_str(), "w");
#endif
}

void logClose() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
}

void logf(const char* fmt, ...) {
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &tm);

    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "[%s] %s\n", stamp, msg);
    std::fflush(stderr);
    if (g_file) {
        std::fprintf(g_file, "[%s] %s\n", stamp, msg);
        std::fflush(g_file);
    }
}

} // namespace app
