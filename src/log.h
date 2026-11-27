#pragma once
#include <cstdarg>
#include <cstdio>

namespace wdc {

enum class LogLevel { Error, Warn, Info, Debug, Trace };

// Set by the entry point before starting SDL or the runtime threads.
inline LogLevel log_level = LogLevel::Trace;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
inline void log(LogLevel level, const char* format, ...) {
    if (level > log_level) return;
    std::va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

}
