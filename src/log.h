#pragma once

namespace fpvlog {

// Appends a timestamped line to GTAIV.FPV.log next to the game exe.
void write(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace fpvlog
