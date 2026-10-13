// GTA IV FPV drone mode, powered by libfpv. This is GTAIV.FPV.core.dll; the
// GTAIV.FPV.asi loader calls it every script tick and hot-reloads it.
#include <windows.h>

#include "autotest.h"
#include "drone.h"
#include "log.h"
#include "recon.h"
#include "render.h"

namespace {

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
        recon::run_once();
    }
    if (key_pressed(VK_F7)) drone::toggle();
    autotest::tick();
    drone::tick();
}

}  // namespace

extern "C" __declspec(dllexport) void gtaiv_fpv_core_tick() { tick(); }

// The loader's D3D hook, on the render thread.
extern "C" __declspec(dllexport) void gtaiv_fpv_core_render(IDirect3DDevice9 *dev) { render::frame(dev); }
extern "C" __declspec(dllexport) void gtaiv_fpv_core_release_d3d() { render::release(); }

// Called before the loader unloads this core: leave nothing pointing into it.
extern "C" __declspec(dllexport) void gtaiv_fpv_core_unload() {
    if (drone::active()) drone::toggle();
    autotest::abort();
    fpvlog::write("core unloaded");
}
