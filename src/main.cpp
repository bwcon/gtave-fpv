// GTA IV FPV drone mode, powered by libfpv.
#include <windows.h>

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
    if (!key_pressed(VK_F7)) return;

    uint32_t player = sh::call<uint32_t>(N("CONVERT_INT_TO_PLAYERINDEX"),
                                         sh::call<uint32_t>(N("GET_PLAYER_ID")));
    uint32_t ped = 0;
    sh::call(N("GET_PLAYER_CHAR"), player, &ped);
    float x = 0, y = 0, z = 0;
    sh::call(N("GET_CHAR_COORDINATES"), ped, &x, &y, &z);
    fpvlog::write("F7: player %u ped %u at %.2f %.2f %.2f", player, ped, x, y, z);
    sh::call(N("PRINT_STRING_WITH_LITERAL_STRING_NOW"), "STRING", "GTAIV.FPV loaded", 2000u, 1u);
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
