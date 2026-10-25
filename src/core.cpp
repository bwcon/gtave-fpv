// GTA V Enhanced FPV drone mode, powered by libfpv. This is GTAVE.FPV.core.dll;
// the GTAVE.FPV.asi loader calls it every script tick and every ReShade frame,
// and hot-reloads it when a new build is deployed.
#include <windows.h>

#define ImTextureID ImU64  // ReShade passes resource views as 64-bit texture IDs
#include <imgui.h>
#include <reshade.hpp>

#include "drone.h"
#include "log.h"
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
    if (key_pressed(kToggleKey)) drone::toggle();
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
extern "C" __declspec(dllexport) void gtave_fpv_core_release_device(reshade::api::device *device) {
    tab::release_device(device);
}

// Called before the loader unloads this core: leave nothing pointing into it.
extern "C" __declspec(dllexport) void gtave_fpv_core_unload() {
    if (drone::active()) drone::toggle();
    tab::release_all();
    fpvlog::write("core unloaded");
}
