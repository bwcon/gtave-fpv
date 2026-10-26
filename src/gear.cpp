#include "gear.h"

#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>

#include "log.h"
#include "natives.h"
#include "shv.h"

namespace gear {
namespace {

constexpr int kHat = 0, kGlasses = 1;  // ped prop slots
// Both hands in front of the chest, as if reading a tablet: holds the radio.
constexpr const char *kHoldDict = "amb@code_human_in_bus_passenger_idles@female@tablet@base";
constexpr const char *kHoldAnim = "base";
constexpr int kAnimLoopUpperBody = 1 | 16 | 32;  // loop, upper body only, the player can still be moved

struct ItemInfo {
    const char *model, *section, *label;
    int bone;  // ped bone id
    Placement fallback;
};
// The placements are starting points; the tab's sliders fit them.
const ItemInfo kItems[ItemCount] = {
    {"gtavefpv_goggles", "Goggles", "Pilot goggles on the player's head", 31086,  // SKEL_Head
     {true, {0.06f, 0.04f, 0.0f}, {0.0f, 90.0f, 0.0f}}},
    {"gtavefpv_radio", "Radio", "Radio in the player's hands", 60309,  // PH_L_Hand
     {true, {0.03f, 0.002f, 0.0f}, {10.0f, 160.0f, 0.0f}}},
};

std::mutex g_mutex;  // the tab's sliders write from the render thread
Placement g_placement[ItemCount];
bool g_read = false;
int g_object[ItemCount] = {};
bool g_missing[ItemCount] = {};  // not installed: do not look again every tick
bool g_holding = false;          // the hold animation runs
struct Saved {
    int drawable = -1, texture = 0;
} g_saved[2];  // the hat and glasses taken off for the flight

std::string ini_path() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path, n);
    return dir.substr(0, dir.find_last_of("\\/")) + "\\GTAVE.FPV.ini";
}

void write(Item item) {
    const std::string ini = ini_path();
    const Placement &p = g_placement[item];
    const char *s = kItems[item].section;
    char buf[96];
    WritePrivateProfileStringA(s, "Show", p.show ? "1" : "0", ini.c_str());
    std::snprintf(buf, sizeof buf, "%.3f,%.3f,%.3f", p.offset[0], p.offset[1], p.offset[2]);
    WritePrivateProfileStringA(s, "Offset", buf, ini.c_str());
    std::snprintf(buf, sizeof buf, "%.1f,%.1f,%.1f", p.rotation[0], p.rotation[1], p.rotation[2]);
    WritePrivateProfileStringA(s, "Rotation", buf, ini.c_str());
}

// Under g_mutex.
void read_once() {
    if (g_read) return;
    g_read = true;
    const std::string ini = ini_path();
    for (int i = 0; i < ItemCount; ++i) {
        const char *s = kItems[i].section;
        Placement p = kItems[i].fallback;
        char buf[96];
        p.show = GetPrivateProfileIntA(s, "Show", 1, ini.c_str()) != 0;
        GetPrivateProfileStringA(s, "Offset", "", buf, sizeof buf, ini.c_str());
        std::sscanf(buf, "%f,%f,%f", &p.offset[0], &p.offset[1], &p.offset[2]);
        GetPrivateProfileStringA(s, "Rotation", "", buf, sizeof buf, ini.c_str());
        std::sscanf(buf, "%f,%f,%f", &p.rotation[0], &p.rotation[1], &p.rotation[2]);
        g_placement[i] = p;
        write(Item(i));  // documents the keys
    }
}

void spawn(Item item, int ped) {
    if (g_object[item] || g_missing[item]) return;
    const uint32_t model = shv::call<uint32_t>(nat::GET_HASH_KEY, kItems[item].model);
    if (!shv::call<bool>(nat::IS_MODEL_IN_CDIMAGE, model)) {
        fpvlog::write("gear: %s not installed (dlcpacks/gtavefpv)", kItems[item].model);
        g_missing[item] = true;
        return;
    }
    shv::call(nat::REQUEST_MODEL, model);
    for (int i = 0; i < 200 && !shv::call<bool>(nat::HAS_MODEL_LOADED, model); ++i) shv::wait(0);
    if (!shv::call<bool>(nat::HAS_MODEL_LOADED, model)) return;
    shv::Vec3 p = shv::call<shv::Vec3>(nat::GET_ENTITY_COORDS, ped, true);
    int obj = shv::call<int>(nat::CREATE_OBJECT_NO_OFFSET, model, p.x, p.y, p.z + 1.0f, false, false, false, 0);
    shv::call(nat::SET_MODEL_AS_NO_LONGER_NEEDED, model);
    if (!obj) return;
    shv::call(nat::SET_ENTITY_COLLISION, obj, false, false);
    g_object[item] = obj;
    if (item == Goggles) {  // a hat or glasses would poke through the goggles
        for (int slot : {kHat, kGlasses}) {
            g_saved[slot].drawable = shv::call<int>(nat::GET_PED_PROP_INDEX, ped, slot, 0);
            g_saved[slot].texture = shv::call<int>(nat::GET_PED_PROP_TEXTURE_INDEX, ped, slot);
            if (g_saved[slot].drawable >= 0) shv::call(nat::CLEAR_PED_PROP, ped, slot, 0);
        }
    }
    fpvlog::write("gear: %s on (object %d)", kItems[item].model, obj);
}

void remove(Item item, int ped) {
    if (g_object[item]) {
        if (shv::call<bool>(nat::DOES_ENTITY_EXIST, g_object[item])) {
            shv::call(nat::SET_ENTITY_AS_MISSION_ENTITY, g_object[item], true, true);
            shv::call(nat::DELETE_ENTITY, &g_object[item]);
        }
        g_object[item] = 0;
    }
    if (item == Goggles) {
        for (int slot : {kHat, kGlasses}) {
            if (g_saved[slot].drawable >= 0)
                shv::call(nat::SET_PED_PROP_INDEX, ped, slot, g_saved[slot].drawable, g_saved[slot].texture, true, 0);
            g_saved[slot].drawable = -1;
        }
    }
}

// The radio needs both hands on it: loop the hold while it is shown.
void hold(int ped, bool on) {
    if (on) {
        shv::call(nat::REQUEST_ANIM_DICT, kHoldDict);
        if (!shv::call<bool>(nat::HAS_ANIM_DICT_LOADED, kHoldDict)) return;  // next tick
        if (!shv::call<bool>(nat::IS_ENTITY_PLAYING_ANIM, ped, kHoldDict, kHoldAnim, 3))
            shv::call(nat::TASK_PLAY_ANIM, ped, kHoldDict, kHoldAnim, 4.0f, -4.0f, -1, kAnimLoopUpperBody, 0.0f, false,
                      false, false);
        g_holding = true;
    } else if (g_holding) {
        shv::call(nat::STOP_ANIM_TASK, ped, kHoldDict, kHoldAnim, 2.0f);
        g_holding = false;
    }
}

}  // namespace

const char *label(Item item) { return kItems[item].label; }

Placement placement(Item item) {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_once();
    return g_placement[item];
}

void set_placement(Item item, const Placement &p, bool save) {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_once();
    g_placement[item] = p;
    if (save) write(item);
}

void start(int ped) { tick(ped); }

// Every tick: the tab's checkboxes and sliders work live.
void tick(int ped) {
    for (int i = 0; i < ItemCount; ++i) {
        const Item item = Item(i);
        const Placement p = placement(item);
        if (!p.show) {
            remove(item, ped);
            continue;
        }
        spawn(item, ped);
        if (!g_object[item]) continue;
        const int bone = shv::call<int>(nat::GET_PED_BONE_INDEX, ped, kItems[item].bone);
        shv::call(nat::ATTACH_ENTITY_TO_ENTITY, g_object[item], ped, bone, p.offset[0], p.offset[1], p.offset[2],
                  p.rotation[0], p.rotation[1], p.rotation[2], false, false, false, false, 2, true, 0);
    }
    hold(ped, g_object[Radio] != 0);
}

void stop(int ped) {
    for (int i = 0; i < ItemCount; ++i) remove(Item(i), ped);
    hold(ped, false);
    for (bool &m : g_missing) m = false;  // the next flight looks again
}

}  // namespace gear
