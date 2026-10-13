// GTAIV.FPV.asi: a thin loader around GTAIV.FPV.core.dll. It owns the
// ScriptHook thread and the log, and reloads the core when a new build is
// deployed (the file's timestamp changes), so the game needn't restart.
// The core is loaded from a copy so the build can overwrite the original.
// It also owns ScriptHook's D3D hook and forwards each frame to the core.
#include <windows.h>

#include <d3d9.h>

#include "log.h"
#include "scripthook.h"

namespace {

const char *const kCore = "GTAIV.FPV.core.dll";
const char *const kLiveCopy = "GTAIV.FPV.core.live.dll";

using CoreFn = void (*)();
using CoreRenderFn = void (*)(IDirect3DDevice9 *);

HMODULE g_core = nullptr;
CoreFn g_core_tick = nullptr;
CoreFn g_core_unload = nullptr;
CoreRenderFn g_core_render = nullptr;
CoreFn g_core_release_d3d = nullptr;
FILETIME g_core_time{};

// The render thread holds this shared while it calls into the core; a reload
// takes it exclusively, so the core is never unloaded mid-frame.
SRWLOCK g_core_lock = SRWLOCK_INIT;
// Before unloading, the core's D3D resources are released on the render thread.
volatile LONG g_release_request = 0;
HANDLE g_released = CreateEventA(nullptr, TRUE, FALSE, nullptr);

// ScriptHook's ID3DDeviceHook, built by hand (MSVC thiscall vtable):
// OnCreateDevice, OnResetDevice, OnLostDevice, OnDestroyDevice, OnRender.
void __attribute__((thiscall)) on_create(void *, IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *) {}
void __attribute__((thiscall)) on_reset(void *, IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *) {}

void __attribute__((thiscall)) on_lost(void *, IDirect3DDevice9 *) {
    AcquireSRWLockShared(&g_core_lock);
    if (g_core_release_d3d) g_core_release_d3d();
    ReleaseSRWLockShared(&g_core_lock);
}

void __attribute__((thiscall)) on_render(void *, IDirect3DDevice9 *dev) {
    AcquireSRWLockShared(&g_core_lock);
    if (InterlockedExchange(&g_release_request, 0)) {
        if (g_core_release_d3d) g_core_release_d3d();
        SetEvent(g_released);
    } else if (g_core_render) {
        g_core_render(dev);
    }
    ReleaseSRWLockShared(&g_core_lock);
}

void *g_hook_vtable[5] = {
    reinterpret_cast<void *>(&on_create), reinterpret_cast<void *>(&on_reset), reinterpret_cast<void *>(&on_lost),
    reinterpret_cast<void *>(&on_lost),  // OnDestroyDevice: same cleanup
    reinterpret_cast<void *>(&on_render),
};
struct {
    void **vtable;
} g_hook = {g_hook_vtable};

void install_d3d_hook() {
    using RequestFn = void *(__cdecl *)(const char *);
    auto request = reinterpret_cast<RequestFn>(reinterpret_cast<void *>(GetProcAddress(
        GetModuleHandleA("ScriptHook.dll"), "?RequestService@ScriptHookManager@@SAPAVIService@@PBD@Z")));
    void *service = request ? request("D3DHook") : nullptr;
    if (!service) {
        fpvlog::write("loader: ScriptHook D3DHook service unavailable, no OSD/overlay");
        return;
    }
    // IService: dtor, GetServiceName, GetDependencies, OnServiceStart, OnServiceStop;
    // ID3DHookService: [5] AddDeviceHook, [6] RemoveDeviceHook.
    using AddHookFn = void(__attribute__((thiscall)) *)(void *, void *);
    auto add = reinterpret_cast<AddHookFn>((*static_cast<void ***>(service))[5]);
    add(service, &g_hook);
    fpvlog::write("loader: D3D hook installed");
}

bool core_changed() {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(kCore, GetFileExInfoStandard, &data)) return false;
    if (CompareFileTime(&data.ftLastWriteTime, &g_core_time) == 0) return false;
    g_core_time = data.ftLastWriteTime;
    return true;
}

void unload_core() {
    if (!g_core) return;
    if (g_core_release_d3d) {  // let the render thread free the core's textures
        ResetEvent(g_released);
        InterlockedExchange(&g_release_request, 1);
        if (WaitForSingleObject(g_released, 500) != WAIT_OBJECT_0) fpvlog::write("loader: render thread idle, D3D release skipped");
    }
    AcquireSRWLockExclusive(&g_core_lock);
    if (g_core_unload) g_core_unload();
    FreeLibrary(g_core);
    g_core = nullptr;
    g_core_tick = g_core_unload = g_core_release_d3d = nullptr;
    g_core_render = nullptr;
    ReleaseSRWLockExclusive(&g_core_lock);
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
    auto get = [](const char *name) { return reinterpret_cast<void *>(GetProcAddress(g_core, name)); };
    AcquireSRWLockExclusive(&g_core_lock);
    g_core_tick = reinterpret_cast<CoreFn>(get("gtaiv_fpv_core_tick"));
    g_core_unload = reinterpret_cast<CoreFn>(get("gtaiv_fpv_core_unload"));
    g_core_render = reinterpret_cast<CoreRenderFn>(get("gtaiv_fpv_core_render"));
    g_core_release_d3d = reinterpret_cast<CoreFn>(get("gtaiv_fpv_core_release_d3d"));
    ReleaseSRWLockExclusive(&g_core_lock);
    fpvlog::write("loader: core loaded (tick %p)", reinterpret_cast<void *>(g_core_tick));
}

void tick() {
    static bool hooked = false;
    if (!hooked) {  // services are up once scripts tick
        hooked = true;
        install_d3d_hook();
    }
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
