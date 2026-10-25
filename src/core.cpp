// GTA V Enhanced FPV drone mode, powered by libfpv. This is GTAVE.FPV.core.dll;
// the GTAVE.FPV.asi loader calls it every script tick and every ReShade frame,
// and hot-reloads it when a new build is deployed.
#include <windows.h>

#define ImTextureID ImU64  // ReShade passes resource views as 64-bit texture IDs
#include <imgui.h>
#include <reshade.hpp>

#include "drone.h"
#include "fpvapi.h"
#include "log.h"

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
    if (key_pressed(kToggleKey)) drone::toggle();
    drone::tick();
}

// ReShade's "GTAVE.FPV" tab.
void draw_tab() {
    drone::Status s = drone::status();
    ImGui::TextUnformatted("GTA V Enhanced FPV (Phase 1)");
    ImGui::Separator();
    if (!s.loaded) {
        ImGui::TextUnformatted("libfpv loads on the first F9.");
    } else {
        ImGui::Text("Input: %s%s", s.device[0] ? s.device : "keyboard", s.needs_setup ? " (needs setup)" : "");
        ImGui::Text("Drone: %s", s.flying ? "flying" : "landed / off");
        if (s.flying) {
            ImGui::Text("Position %.1f %.1f %.1f", s.position[0], s.position[1], s.position[2]);
            ImGui::Text("Speed %.0f km/h   FOV %.0f   near clip %.3f", s.speed_kmh, s.fov, s.near_clip);
            if (s.arming[0]) ImGui::Text("Arming blocked: %s", s.arming);
        }
    }
    ImGui::Separator();
    ImGui::TextUnformatted("F9 launch / land   C camera   R respawn   Y move player here");
    ImGui::TextUnformatted("Keyboard: W/S throttle, A/D yaw, arrows pitch/roll, Enter arm");
}

}  // namespace

extern "C" __declspec(dllexport) void gtave_fpv_core_tick() { tick(); }

// ReShade calls, forwarded by the loader on the render thread. The ImGui
// function table belongs to the loader's ReShade registration.
extern "C" __declspec(dllexport) void gtave_fpv_core_tab(reshade::api::effect_runtime *, const imgui_function_table *table) {
    imgui_function_table_instance() = table;
    draw_tab();
}

// Called before the loader unloads this core: leave nothing pointing into it.
extern "C" __declspec(dllexport) void gtave_fpv_core_unload() {
    if (drone::active()) drone::toggle();
    fpvlog::write("core unloaded");
}
