// GTAVE.FPV.asi: a thin loader around GTAVE.FPV.core.dll. It owns the
// ScriptHookV script, the log and the ReShade add-on registration, and reloads
// the core when a new build is deployed (the file's timestamp changes), so the
// game needn't restart. The core is loaded from a copy so the build can
// overwrite the original. ReShade's per-frame calls are forwarded to the core.
#include <windows.h>

#include <atomic>
#include <cstring>

#define ImTextureID ImU64  // ReShade passes resource views as 64-bit texture IDs
#include <imgui.h>
#include <reshade.hpp>

#include "log.h"
#include "shv.h"

// Shown in ReShade's Add-ons tab.
extern "C" {
__declspec(dllexport) const char *NAME = "GTAVE.FPV";
__declspec(dllexport) const char *DESCRIPTION = "FPV drone with real Betaflight (libfpv) for GTA V Enhanced";
}

namespace {

const char *const kCore = "GTAVE.FPV.core.dll";
const char *const kLiveCopy = "GTAVE.FPV.core.live.dll";

using CoreFn = void (*)();
using CoreTabFn = void (*)(reshade::api::effect_runtime *, const imgui_function_table *);
using CoreDeviceFn = void (*)(reshade::api::device *);
using CoreRuntimeFn = void (*)(reshade::api::effect_runtime *);

HMODULE g_module = nullptr;
HMODULE g_core = nullptr;
CoreFn g_core_tick = nullptr;
CoreFn g_core_unload = nullptr;
CoreTabFn g_core_tab = nullptr;
CoreTabFn g_core_overlay = nullptr;
CoreDeviceFn g_core_release_device = nullptr;
CoreRuntimeFn g_core_begin_effects = nullptr;
CoreRuntimeFn g_core_reloaded_effects = nullptr;
FILETIME g_core_time{};
std::atomic<bool> g_addon{false};
SRWLOCK g_register_lock = SRWLOCK_INIT;

// The lens effect's Active uniform, for the frames before the core loads.
reshade::api::effect_runtime *g_lens_runtime = nullptr;
reshade::api::effect_uniform_variable g_lens_active = {0};

// The render thread holds this shared while it calls into the core; a reload
// takes it exclusively, so the core is never unloaded mid-frame.
SRWLOCK g_core_lock = SRWLOCK_INIT;

void on_tab(reshade::api::effect_runtime *runtime) {
    AcquireSRWLockShared(&g_core_lock);
    if (g_core_tab)
        g_core_tab(runtime, imgui_function_table_instance());
    else
        ImGui::TextUnformatted("GTAVE.FPV.core.dll is not loaded, see GTAVE.FPV.log");
    ReleaseSRWLockShared(&g_core_lock);
}

// Every frame, menu open or closed: the OSD.
void on_overlay(reshade::api::effect_runtime *runtime) {
    AcquireSRWLockShared(&g_core_lock);
    if (g_core_overlay) g_core_overlay(runtime, imgui_function_table_instance());
    ReleaseSRWLockShared(&g_core_lock);
}

// Until the core loads (with the world), the lens stays off. A preset saved in
// the FPV view holds Active = 1, which would show the lens over the logos.
// Uniform handles come from enumerate_uniform_variables (MSVC/MinGW ABI, see lens.cpp).
void keep_lens_off(reshade::api::effect_runtime *runtime) {
    if (runtime != g_lens_runtime || !g_lens_active.handle) {
        g_lens_runtime = runtime;
        g_lens_active = {0};
        runtime->enumerate_uniform_variables(
            "GTAVE_FPV_Lens.fx", [](reshade::api::effect_runtime *rt, reshade::api::effect_uniform_variable var) {
                char name[16] = "";
                size_t size = sizeof name;
                rt->get_uniform_variable_name(var, name, &size);
                if (std::strcmp(name, "Active") == 0) g_lens_active = var;
            });
    }
    const float off = 0.0f;
    if (g_lens_active.handle) runtime->set_uniform_value_float(g_lens_active, &off, 1, 0);
}

// Before ReShade runs its effects: the lens effect's uniforms.
void on_begin_effects(reshade::api::effect_runtime *runtime, reshade::api::command_list *,
                      reshade::api::resource_view, reshade::api::resource_view) {
    AcquireSRWLockShared(&g_core_lock);
    if (g_core_begin_effects) g_core_begin_effects(runtime);
    else keep_lens_off(runtime);
    ReleaseSRWLockShared(&g_core_lock);
}

void on_reloaded_effects(reshade::api::effect_runtime *runtime) {
    g_lens_runtime = nullptr;  // the handles changed
    AcquireSRWLockShared(&g_core_lock);
    if (g_core_reloaded_effects) g_core_reloaded_effects(runtime);
    ReleaseSRWLockShared(&g_core_lock);
}

// The game recreates its D3D12 device during startup: the core's textures
// must go before the device they were made on.
void on_destroy_device(reshade::api::device *device) {
    AcquireSRWLockShared(&g_core_lock);
    if (g_core_release_device) g_core_release_device(device);
    ReleaseSRWLockShared(&g_core_lock);
}

// ReShade.asi loads after this ASI (the loader goes alphabetically), so a
// thread from DllMain registers as soon as ReShade is in the process, before
// the logos; the script fiber tries too, in case that thread gave up.
void try_register_addon() {
    if (g_addon) return;
    AcquireSRWLockExclusive(&g_register_lock);
    if (g_addon || !reshade::register_addon(g_module)) {
        ReleaseSRWLockExclusive(&g_register_lock);
        return;
    }
    reshade::register_overlay("GTAVE.FPV", &on_tab);  // its own tab: dockable, like other add-ons
    reshade::register_event<reshade::addon_event::destroy_device>(&on_destroy_device);
    reshade::register_event<reshade::addon_event::reshade_overlay>(&on_overlay);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(&on_begin_effects);
    reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(&on_reloaded_effects);
    g_addon = true;
    ReleaseSRWLockExclusive(&g_register_lock);
    fpvlog::write("loader: registered with ReShade as an add-on (API %d)", RESHADE_API_VERSION);
}

DWORD WINAPI early_register(LPVOID) {
    for (int i = 0; i < 2400 && !g_addon; ++i) {  // up to 2 minutes
        Sleep(50);
        try_register_addon();
    }
    return 0;
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
    AcquireSRWLockExclusive(&g_core_lock);
    if (g_core_unload) g_core_unload();
    FreeLibrary(g_core);
    g_core = nullptr;
    g_core_tick = g_core_unload = nullptr;
    g_core_tab = nullptr;
    g_core_overlay = nullptr;
    g_core_release_device = nullptr;
    g_core_begin_effects = g_core_reloaded_effects = nullptr;
    ReleaseSRWLockExclusive(&g_core_lock);
}

void load_core() {
    unload_core();
    if (!CopyFileA(kCore, kLiveCopy, FALSE)) {
        fpvlog::write("loader: cannot copy %s (error %lu)", kCore, GetLastError());
        return;
    }
    HMODULE core = LoadLibraryA(kLiveCopy);
    if (!core) {
        fpvlog::write("loader: cannot load %s (error %lu)", kLiveCopy, GetLastError());
        return;
    }
    auto get = [core](const char *name) { return reinterpret_cast<void *>(GetProcAddress(core, name)); };
    AcquireSRWLockExclusive(&g_core_lock);
    g_core = core;
    g_core_tick = reinterpret_cast<CoreFn>(get("gtave_fpv_core_tick"));
    g_core_unload = reinterpret_cast<CoreFn>(get("gtave_fpv_core_unload"));
    g_core_tab = reinterpret_cast<CoreTabFn>(get("gtave_fpv_core_tab"));
    g_core_overlay = reinterpret_cast<CoreTabFn>(get("gtave_fpv_core_overlay"));
    g_core_release_device = reinterpret_cast<CoreDeviceFn>(get("gtave_fpv_core_release_device"));
    g_core_begin_effects = reinterpret_cast<CoreRuntimeFn>(get("gtave_fpv_core_begin_effects"));
    g_core_reloaded_effects = reinterpret_cast<CoreRuntimeFn>(get("gtave_fpv_core_reloaded_effects"));
    ReleaseSRWLockExclusive(&g_core_lock);
    fpvlog::write("loader: core loaded (tick %p)", reinterpret_cast<void *>(g_core_tick));
}

void script_main() {
    fpvlog::write("loader: script running");
    for (unsigned poll = 0;; ++poll) {
        try_register_addon();
        if (poll % 30 == 0 && core_changed()) load_core();
        if (g_core_tick) g_core_tick();
        shv::wait(0);
    }
}

}  // namespace

// libfpv's log callback lives here, not in the core, because libfpv keeps
// calling it from its own threads across core reloads.
extern "C" __declspec(dllexport) void gtave_fpv_libfpv_log(void *, int level, const char *message) {
    static const char *names[] = {"debug", "info", "warn", "error"};
    fpvlog::write("libfpv %s: %s", names[level & 3], message);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_module = module;
        fpvlog::write("GTAVE.FPV attached");
        shv::register_script(module, &script_main);
        if (HANDLE t = CreateThread(nullptr, 0, &early_register, nullptr, 0, nullptr)) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        // Only on a real unload. At process exit ReShade may already be gone.
        if (g_addon) {
            reshade::unregister_overlay("GTAVE.FPV", &on_tab);
            reshade::unregister_event<reshade::addon_event::destroy_device>(&on_destroy_device);
            reshade::unregister_event<reshade::addon_event::reshade_overlay>(&on_overlay);
            reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(&on_begin_effects);
            reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(&on_reloaded_effects);
            reshade::unregister_addon(module);
        }
        shv::unregister_script(module);
    }
    return TRUE;
}
