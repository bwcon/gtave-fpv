#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace fpvlog {

void write(const char *fmt, ...) {
    static std::mutex mutex;
    // The loader and the core both write this file, so both append; only the
    // loader starts it afresh.
    static FILE *file = [] {
#ifndef FPVLOG_APPEND
        if (FILE *f = std::fopen("GTAIV.FPV.log", "w")) std::fclose(f);
#endif
        return std::fopen("GTAIV.FPV.log", "a");
    }();
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
