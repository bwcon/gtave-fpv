// Phase 0 spike: proves that ScriptHookV scripts and the synchronous LOS probe
// work in GTA V Enhanced, and measures what one probe costs. Every 2 seconds
// it casts a ray 50 m straight down from the player and logs the hit to
// GTAVE.FPV.log. Once, it times a burst of 64 rays.
#include <windows.h>

#include <cmath>

#include "log.h"
#include "shv.h"

namespace {

constexpr uint64_t PLAYER_PED_ID = 0xD80958FC74E988A6;
constexpr uint64_t GET_ENTITY_COORDS = 0x3FEF770D40960D5A;
constexpr uint64_t GET_ENTITY_TYPE = 0x8ACD366038D14505;
constexpr uint64_t START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE = 0x377906D8A31E5586;
constexpr uint64_t GET_SHAPE_TEST_RESULT = 0x3D87450E15D98694;

constexpr int kTraceAll = -1;  // map, vehicles, peds, objects, foliage...
constexpr int kProbeOptions = 7;

struct Hit {
    int status = 0;  // 0 failed, 1 pending, 2 done
    int hit = 0;
    shv::Vec3 end{};
    shv::Vec3 normal{};
    int entity = 0;
};

Hit probe(const shv::Vec3 &from, const shv::Vec3 &to, int ignore) {
    int handle = shv::call<int>(START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE, from.x, from.y,
                                from.z, to.x, to.y, to.z, kTraceAll, ignore, kProbeOptions);
    Hit h;
    h.status = shv::call<int>(GET_SHAPE_TEST_RESULT, handle, &h.hit, &h.end, &h.normal, &h.entity);
    return h;
}

double now_us() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) * 1e6 / double(freq.QuadPart);
}

void log_down_probe() {
    int ped = shv::call<int>(PLAYER_PED_ID);
    shv::Vec3 p = shv::call<shv::Vec3>(GET_ENTITY_COORDS, ped, true);
    shv::Vec3 below{p.x, p.y, p.z - 50.0f};

    double t0 = now_us();
    Hit h = probe(p, below, ped);
    double us = now_us() - t0;

    int type = h.entity ? shv::call<int>(GET_ENTITY_TYPE, h.entity) : 0;
    fpvlog::write("player %.2f %.2f %.2f | status %d hit %d at %.2f %.2f %.2f normal %.2f %.2f %.2f"
                  " entity %d type %d | %.1f us",
                  p.x, p.y, p.z, h.status, h.hit, h.end.x, h.end.y, h.end.z, h.normal.x,
                  h.normal.y, h.normal.z, h.entity, type, us);
}

// libfpv casts several rays per tick; this tells us how many we can afford.
void log_burst_cost() {
    int ped = shv::call<int>(PLAYER_PED_ID);
    shv::Vec3 p = shv::call<shv::Vec3>(GET_ENTITY_COORDS, ped, true);
    constexpr int kRays = 64;
    int hits = 0;
    double t0 = now_us();
    for (int i = 0; i < kRays; ++i) {
        float angle = 6.2831853f * float(i) / kRays;
        shv::Vec3 to{p.x + 30.0f * cosf(angle), p.y + 30.0f * sinf(angle), p.z};
        hits += probe(p, to, ped).hit ? 1 : 0;
    }
    double us = now_us() - t0;
    fpvlog::write("burst: %d horizontal rays of 30 m, %d hits, %.1f us total, %.2f us per ray",
                  kRays, hits, us, us / kRays);
}

void script_main() {
    fpvlog::write("probe spike running");
    DWORD next = 0;
    bool burst_done = false;
    for (;;) {
        DWORD tick = GetTickCount();
        if (tick >= next) {
            log_down_probe();
            if (!burst_done) {
                log_burst_cost();
                burst_done = true;
            }
            next = tick + 2000;
        }
        shv::wait(0);
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        shv::register_script(instance, script_main);
    else if (reason == DLL_PROCESS_DETACH)
        shv::unregister_script(instance);
    return TRUE;
}
