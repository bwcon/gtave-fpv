// GTA IV FPV drone mode, powered by libfpv.
#include <windows.h>

#include "drone.h"
#include "log.h"
#include "scripthook.h"

namespace {

bool key_pressed(int vk) {
    static bool was_down[256];
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !was_down[vk];
    was_down[vk] = down;
    return pressed;
}

void tick() {
    static bool first = true;
    if (first) {
        first = false;
        fpvlog::write("first script tick");
    }
    if (key_pressed(VK_F7)) drone::toggle();
    drone::tick();
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        fpvlog::write("GTAIV.FPV attached");
        sh::register_thread(&tick);
    }
    return TRUE;
}
