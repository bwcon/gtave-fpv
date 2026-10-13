#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace fpvlog {

void write(const char *fmt, ...) {
    static std::mutex mutex;
    static FILE *file = std::fopen("GTAIV.FPV.log", "w");
    if (!file) return;

    std::lock_guard<std::mutex> lock(mutex);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(file, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(file, fmt, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
}

}  // namespace fpvlog
