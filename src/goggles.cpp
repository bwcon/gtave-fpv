#include "goggles.h"

#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>

#include "log.h"
#include "natives.h"
#include "shv.h"

namespace goggles {
namespace {

constexpr int kHeadBone = 31086;  // SKEL_Head
constexpr int kHat = 0, kGlasses = 1;  // ped prop slots

std::mutex g_mutex;  // the tab's sliders write from the render thread
Placement g_placement;
bool g_read = false;
int g_object = 0;
struct Saved {
    int drawable = -1, texture = 0;
} g_saved[2];  // the hat and glasses taken off for the flight

std::string ini_path() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path, n);
    return dir.substr(0, dir.find_last_of("\\/")) + "\\GTAVE.FPV.ini";
}

void write(const Placement &p) {
    const std::string ini = ini_path();
    char buf[96];
    WritePrivateProfileStringA("Goggles", "Show", p.show ? "1" : "0", ini.c_str());
    std::snprintf(buf, sizeof buf, "%.3f,%.3f,%.3f", p.offset[0], p.offset[1], p.offset[2]);
    WritePrivateProfileStringA("Goggles", "Offset", buf, ini.c_str());
    std::snprintf(buf, sizeof buf, "%.1f,%.1f,%.1f", p.rotation[0], p.rotation[1], p.rotation[2]);
    WritePrivateProfileStringA("Goggles", "Rotation", buf, ini.c_str());
}

// Under g_mutex.
void read_once() {
    if (g_read) return;
    g_read = true;
    const std::string ini = ini_path();
    char buf[96];
    Placement p;
    p.show = GetPrivateProfileIntA("Goggles", "Show", 1, ini.c_str()) != 0;
    GetPrivateProfileStringA("Goggles", "Offset", "", buf, sizeof buf, ini.c_str());
    std::sscanf(buf, "%f,%f,%f", &p.offset[0], &p.offset[1], &p.offset[2]);
    GetPrivateProfileStringA("Goggles", "Rotation", "", buf, sizeof buf, ini.c_str());
    std::sscanf(buf, "%f,%f,%f", &p.rotation[0], &p.rotation[1], &p.rotation[2]);
    g_placement = p;
    write(p);  // documents the keys
}

}  // namespace

Placement placement() {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_once();
    return g_placement;
}

void set_placement(const Placement &p, bool save) {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_once();
    g_placement = p;
    if (save) write(p);
}

bool g_missing = false;  // not installed: do not look again every tick

void start(int ped) {
    if (g_object || g_missing || !placement().show) return;
    const uint32_t model = shv::call<uint32_t>(nat::GET_HASH_KEY, "gtavefpv_goggles");
    if (!shv::call<bool>(nat::IS_MODEL_IN_CDIMAGE, model)) {
        fpvlog::write("goggles: gtavefpv_goggles not installed (dlcpacks/gtavefpv)");
        g_missing = true;
        return;
    }
    shv::call(nat::REQUEST_MODEL, model);
    for (int i = 0; i < 200 && !shv::call<bool>(nat::HAS_MODEL_LOADED, model); ++i) shv::wait(0);
    if (!shv::call<bool>(nat::HAS_MODEL_LOADED, model)) return;
    shv::Vec3 p = shv::call<shv::Vec3>(nat::GET_ENTITY_COORDS, ped, true);
    g_object = shv::call<int>(nat::CREATE_OBJECT_NO_OFFSET, model, p.x, p.y, p.z + 1.0f, false, false, false, 0);
    shv::call(nat::SET_MODEL_AS_NO_LONGER_NEEDED, model);
    if (!g_object) return;
    shv::call(nat::SET_ENTITY_COLLISION, g_object, false, false);
    // A hat or glasses would poke through the goggles.
    for (int slot : {kHat, kGlasses}) {
        g_saved[slot].drawable = shv::call<int>(nat::GET_PED_PROP_INDEX, ped, slot, 0);
        g_saved[slot].texture = shv::call<int>(nat::GET_PED_PROP_TEXTURE_INDEX, ped, slot);
        if (g_saved[slot].drawable >= 0) shv::call(nat::CLEAR_PED_PROP, ped, slot, 0);
    }
    tick(ped);
    fpvlog::write("goggles: on (object %d)", g_object);
}

// Every tick, so the tab's sliders move them live.
void tick(int ped) {
    const Placement p = placement();
    if (!p.show) return stop(ped);
    if (!g_object) return start(ped);
    const int bone = shv::call<int>(nat::GET_PED_BONE_INDEX, ped, kHeadBone);
    shv::call(nat::ATTACH_ENTITY_TO_ENTITY, g_object, ped, bone, p.offset[0], p.offset[1], p.offset[2], p.rotation[0],
              p.rotation[1], p.rotation[2], false, false, false, false, 2, true, 0);
}

void stop(int ped) {
    if (g_object) {
        if (shv::call<bool>(nat::DOES_ENTITY_EXIST, g_object)) {
            shv::call(nat::SET_ENTITY_AS_MISSION_ENTITY, g_object, true, true);
            shv::call(nat::DELETE_ENTITY, &g_object);
        }
        g_object = 0;
    }
    for (int slot : {kHat, kGlasses}) {
        if (g_saved[slot].drawable >= 0)
            shv::call(nat::SET_PED_PROP_INDEX, ped, slot, g_saved[slot].drawable, g_saved[slot].texture, true, 0);
        g_saved[slot].drawable = -1;
    }
    g_missing = false;  // the next flight looks again
}

}  // namespace goggles
