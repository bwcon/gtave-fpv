#include "fpvapi.h"

#include <windows.h>

#include <atomic>
#include <string>

#include "log.h"

namespace fpvapi {

Api fpv;

namespace {

std::atomic<bool> g_loaded{false};  // the ReShade tab reads it on the render thread
bool g_failed = false;

std::string game_dir() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path, n);
    return dir.substr(0, dir.find_last_of("\\/"));
}

void on_fpv_log(void *, fpv_log_level level, const char *message) {
    static const char *names[] = {"debug", "info", "warn", "error"};
    fpvlog::write("libfpv %s: %s", names[level & 3], message);
}

}  // namespace

bool loaded() { return g_loaded; }

bool load() {
    if (g_loaded) return true;
    if (g_failed) return false;
    g_failed = true;  // until proven otherwise; don't retry every key press

    std::string dll = game_dir() + "\\libfpv.dll";
    HMODULE lib = LoadLibraryA(dll.c_str());
    if (!lib) {
        fpvlog::write("cannot load %s (error %lu)", dll.c_str(), GetLastError());
        return false;
    }
    bool ok = true;
#define FPV_FN(name)                                                                                     \
    fpv.name = reinterpret_cast<decltype(fpv.name)>(reinterpret_cast<void *>(GetProcAddress(lib, #name))); \
    if (!fpv.name) {                                                                                     \
        fpvlog::write("libfpv.dll has no %s", #name);                                                    \
        ok = false;                                                                                      \
    }
    FPV_FUNCTIONS(FPV_FN)
#undef FPV_FN
    if (!ok) return false;

    // The per-user folder every libfpv game shares (bwcon/libfpv), so the radio
    // setup and the Betaflight tune follow the pilot. Upstream builds lack it:
    // keep the settings next to the game then.
    std::string data_dir = game_dir() + "\\fpv-data";
    using SharedDirFn = decltype(&::fpv_shared_data_dir);
    char shared[1024];
    if (auto shared_dir = reinterpret_cast<SharedDirFn>(
            reinterpret_cast<void *>(GetProcAddress(lib, "fpv_shared_data_dir"))))
        if (shared_dir(shared, sizeof shared) == FPV_OK) data_dir = shared;
    fpv_global_config cfg;
    fpv.fpv_global_config_defaults(&cfg);
    cfg.data_dir = data_dir.c_str();
    // GTA V: metres, X east, Y north, Z up.
    cfg.coords = {FPV_AXIS_POS_X, FPV_AXIS_POS_Z, FPV_AXIS_POS_Y, 1.0f};
    // Prefer the loader's callback: libfpv keeps it across core reloads.
    if (auto loader_log = reinterpret_cast<fpv_log_fn>(reinterpret_cast<void *>(
            GetProcAddress(GetModuleHandleA("GTAVE.FPV.asi"), "gtave_fpv_libfpv_log"))))
        cfg.log = loader_log;
    else
        cfg.log = &on_fpv_log;
    cfg.log_level = FPV_LOG_INFO;
    fpv_result r = fpv.fpv_global_init(&cfg);
    if (r != FPV_OK && r != FPV_ERROR_ALREADY_INITIALIZED) {
        fpvlog::write("fpv_global_init: %s (%s)", fpv.fpv_result_string(r), fpv.fpv_last_error());
        return false;
    }
    g_failed = false;
    g_loaded = true;
    fpvlog::write("libfpv %s initialised, data in %s", fpv.fpv_version_string(), data_dir.c_str());
    return true;
}

}  // namespace fpvapi
