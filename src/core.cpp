// GTA V Enhanced FPV drone mode, powered by libfpv. This is GTAVE.FPV.core.dll;
// the GTAVE.FPV.asi loader calls it every script tick and every ReShade frame,
// and hot-reloads it when a new build is deployed.
#include "drone.h"
#include "lens.h"
#include "log.h"
#include "osd.h"
#include "render.h"
#include "tab.h"

namespace {

constexpr int kToggleKey = VK_F9;  // F7 is AddonSpawner's

// The first reading of each key is only a baseline (see drone.cpp).
bool key_pressed(int vk) {
    static bool was_down[256], seen[256];
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = seen[vk] && down && !was_down[vk];
    was_down[vk] = down;
    seen[vk] = true;
    return pressed;
}

void tick() {
    static bool first = true;
    if (first) {
        first = false;
        fpvlog::write("first script tick");
    }
    tab::tick();
    // Alt+F9 is ShadowPlay's record key (and the DVR presses it): not a toggle.
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (key_pressed(kToggleKey) && !alt) drone::toggle();
    drone::tick();
}

}  // namespace

extern "C" __declspec(dllexport) void gtave_fpv_core_tick() { tick(); }

// ReShade calls, forwarded by the loader on the render thread. The ImGui
// function table belongs to the loader's ReShade registration.
extern "C" __declspec(dllexport) void gtave_fpv_core_tab(reshade::api::effect_runtime *runtime,
                                                         const imgui_function_table *table) {
    imgui_function_table_instance() = table;
    tab::draw(runtime);
}
extern "C" __declspec(dllexport) void gtave_fpv_core_overlay(reshade::api::effect_runtime *runtime,
                                                             const imgui_function_table *table) {
    imgui_function_table_instance() = table;
    osd::draw(runtime);
}
extern "C" __declspec(dllexport) void gtave_fpv_core_begin_effects(reshade::api::effect_runtime *runtime) {
    lens::begin_effects(runtime);
}
extern "C" __declspec(dllexport) void gtave_fpv_core_reloaded_effects(reshade::api::effect_runtime *runtime) {
    lens::forget(runtime);
}
extern "C" __declspec(dllexport) void gtave_fpv_core_release_device(reshade::api::device *device) {
    render::release_device(device);
}

// Called before the loader unloads this core: leave nothing pointing into it.
extern "C" __declspec(dllexport) void gtave_fpv_core_unload() {
    if (drone::active()) drone::toggle();
    render::release_all();
    fpvlog::write("core unloaded");
}
