// Unattended test scenario, enabled by creating GTAIV/fpv-data/autotest.txt
// and re-run whenever that file is touched. Moves the player to the nearest
// road, sweeps the world probe around him, then launches the drone and flies
// a fixed stick sequence, logging everything.
#include "autotest.h"

#include <windows.h>

#include <cmath>

#include "drone.h"
#include "log.h"
#include "scripthook.h"
#include "world.h"

namespace autotest {
namespace {

enum class Stage { Idle, Waiting, Settling, Flying };
Stage g_stage = Stage::Idle;
LARGE_INTEGER g_t0;
FILETIME g_trigger_time{};

// True when autotest.txt appeared or was touched since the last run.
bool triggered() {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA("fpv-data\\autotest.txt", GetFileExInfoStandard, &data)) return false;
    if (CompareFileTime(&data.ftLastWriteTime, &g_trigger_time) == 0) return false;
    g_trigger_time = data.ftLastWriteTime;
    return true;
}

uint32_t player_ped() {
    uint32_t player = sh::call<uint32_t>(N("CONVERT_INT_TO_PLAYERINDEX"), sh::call<uint32_t>(N("GET_PLAYER_ID")));
    uint32_t ped = 0;
    sh::call(N("GET_PLAYER_CHAR"), player, &ped);
    return ped;
}

// Outdoors, so the flight has room: the nearest road node.
void move_to_road() {
    uint32_t ped = player_ped();
    float x, y, z, rx, ry, rz;
    sh::call(N("GET_CHAR_COORDINATES"), ped, &x, &y, &z);
    if (!sh::call<bool>(N("GET_CLOSEST_CAR_NODE"), x, y, z, &rx, &ry, &rz)) {
        fpvlog::write("autotest: no road node near %.2f %.2f %.2f, staying", x, y, z);
        return;
    }
    sh::call(N("SET_CHAR_COORDINATES"), ped, rx, ry, rz);
    sh::call(N("LOAD_SCENE"), rx, ry, rz);
    fpvlog::write("autotest: moved player from %.2f %.2f %.2f to road %.2f %.2f %.2f", x, y, z, rx, ry, rz);
}

float seconds_since(const LARGE_INTEGER &t) {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return float(now.QuadPart - t.QuadPart) / float(freq.QuadPart);
}

void probe_sweep() {
    uint32_t ped = player_ped();
    float o[3];
    sh::call(N("GET_CHAR_COORDINATES"), ped, &o[0], &o[1], &o[2]);
    o[2] += 1.0f;  // above Niko's head so he doesn't block the rays
    fpvlog::write("autotest: probe sweep from %.2f %.2f %.2f", o[0], o[1], o[2]);
    for (int i = 0; i < 8; ++i) {
        float a = float(i) * 0.785398f;
        float dir[3] = {std::cos(a), std::sin(a), 0};
        fpv_raycast_hit hit{};
        int r = world::raycast(nullptr, o, dir, 60.0f, &hit);
        fpvlog::write("autotest: ray %d dir %.2f %.2f -> %s d %.2f n %.2f %.2f %.2f", i, dir[0], dir[1],
                      r ? "hit" : "miss", hit.distance, hit.normal[0], hit.normal[1], hit.normal[2]);
    }
    float down[3] = {0, 0, -1};
    fpv_raycast_hit hit{};
    int r = world::raycast(nullptr, o, down, 100.0f, &hit);
    fpvlog::write("autotest: ray down -> %s d %.2f n %.2f %.2f %.2f", r ? "hit" : "miss", hit.distance, hit.normal[0],
                  hit.normal[1], hit.normal[2]);
}

float g_hold_z = 0;

// AETR + AUX1 arm, AUX2 angle mode, AUX3 beeper, AUX4 turtle. A crude
// altitude hold keeps it about 4 m up while it cruises forward.
void sticks(float t) {
    const fpv_frame &f = drone::last_frame();
    if (g_hold_z == 0 && t > 0.5f) g_hold_z = f.position[2] + 4.0f;  // after the first fresh frame
    float ch[8] = {0, 0, -1, 0, -1, 1, -1, -1};
    if (t > 2.0f) ch[4] = 1;  // arm
    if (t > 3.0f) {
        float err = g_hold_z - f.position[2];
        float thr = -0.45f + 0.08f * err - 0.06f * f.velocity[2];
        ch[2] = thr < -0.9f ? -0.9f : thr > 0.3f ? 0.3f : thr;
    }
    if (t > 7.0f) ch[1] = 0.25f;  // nose down: cruise forward
    drone::inject_channels(ch);
}

}  // namespace

void tick() {
    static int poll = 0;
    if (g_stage == Stage::Idle) {
        if (++poll % 30 != 0 || !triggered()) return;
        g_stage = Stage::Waiting;
        QueryPerformanceCounter(&g_t0);
        fpvlog::write("autotest: triggered, starting in 10 s");
        return;
    }
    float t = seconds_since(g_t0);
    switch (g_stage) {
        case Stage::Waiting:
            if (t < 10.0f || sh::call<bool>(N("IS_PAUSE_MENU_ACTIVE"))) return;
            if (drone::active()) drone::toggle();
            move_to_road();
            QueryPerformanceCounter(&g_t0);
            g_stage = Stage::Settling;
            return;
        case Stage::Settling:
            if (t < 3.0f) return;
            probe_sweep();
            drone::toggle();
            if (!drone::active()) {
                g_stage = Stage::Idle;
                return;
            }
            fpvlog::write("autotest: flying");
            g_hold_z = 0;
            QueryPerformanceCounter(&g_t0);
            g_stage = Stage::Flying;
            return;
        case Stage::Flying:
            sticks(t);
            if (t > 20.0f || !drone::active()) {
                if (drone::active()) drone::toggle();
                g_stage = Stage::Idle;
                fpvlog::write("autotest: done");
            }
            return;
        default:
            return;
    }
}

}  // namespace autotest
