// GTAIV.FPV.asi: a thin loader around GTAIV.FPV.core.dll. It owns the
// ScriptHook thread and the log, and reloads the core when a new build is
// deployed (the file's timestamp changes), so the game needn't restart.
// The core is loaded from a copy so the build can overwrite the original.
#include <windows.h>

#include "log.h"
#include "scripthook.h"

namespace {

const char *const kCore = "GTAIV.FPV.core.dll";
const char *const kLiveCopy = "GTAIV.FPV.core.live.dll";

using CoreFn = void (*)();

HMODULE g_core = nullptr;
CoreFn g_core_tick = nullptr;
CoreFn g_core_unload = nullptr;
FILETIME g_core_time{};

bool core_changed() {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(kCore, GetFileExInfoStandard, &data)) return false;
    if (CompareFileTime(&data.ftLastWriteTime, &g_core_time) == 0) return false;
    g_core_time = data.ftLastWriteTime;
    return true;
}

void unload_core() {
    if (!g_core) return;
    if (g_core_unload) g_core_unload();
    FreeLibrary(g_core);
    g_core = nullptr;
    g_core_tick = g_core_unload = nullptr;
}

void load_core() {
    unload_core();
    if (!CopyFileA(kCore, kLiveCopy, FALSE)) {
        fpvlog::write("loader: cannot copy %s (error %lu)", kCore, GetLastError());
        return;
    }
    g_core = LoadLibraryA(kLiveCopy);
    if (!g_core) {
        fpvlog::write("loader: cannot load %s (error %lu)", kLiveCopy, GetLastError());
        return;
    }
    g_core_tick = reinterpret_cast<CoreFn>(reinterpret_cast<void *>(GetProcAddress(g_core, "gtaiv_fpv_core_tick")));
    g_core_unload =
        reinterpret_cast<CoreFn>(reinterpret_cast<void *>(GetProcAddress(g_core, "gtaiv_fpv_core_unload")));
    fpvlog::write("loader: core loaded (tick %p)", reinterpret_cast<void *>(g_core_tick));
}

void tick() {
    static int poll = 0;
    if (poll++ % 30 == 0 && core_changed()) load_core();
    if (g_core_tick) g_core_tick();
}

}  // namespace

// libfpv's log callback lives here, not in the core, because libfpv keeps
// calling it from its own threads across core reloads.
extern "C" __declspec(dllexport) void gtaiv_fpv_libfpv_log(void *, int level, const char *message) {
    static const char *names[] = {"debug", "info", "warn", "error"};
    fpvlog::write("libfpv %s: %s", names[level & 3], message);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        fpvlog::write("GTAIV.FPV attached");
        sh::register_thread(&tick);
    }
    return TRUE;
}
