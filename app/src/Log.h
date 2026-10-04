#pragma once

#include <string>

namespace app {

/// Opens the log file (truncating it). Messages also go to stderr.
void logOpen(const std::string& path);
void logClose();

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void logf(const char* fmt, ...);

} // namespace app
