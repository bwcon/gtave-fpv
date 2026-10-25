// Phase 0 spike: proves that ScriptHookV scripts, the synchronous LOS probe and
// a ReShade overlay all work from one ASI in GTA V Enhanced.
//
// Every frame the script fiber casts a ray 50 m straight down from the player.
// The ReShade overlay (render thread) shows the latest result, and every
// 2 seconds the result is also written to GTAVE.FPV.log. Once, the fiber times
// a burst of 64 rays to measure the per-ray cost.
#include <windows.h>

#include <cmath>
#include <mutex>

#define ImTextureID ImU64  // ReShade passes resource views as 64-bit texture IDs
#include <imgui.h>
#include <reshade.hpp>

#include "log.h"
#include "shv.h"

// Shown in ReShade's Add-ons tab.
extern "C" {
__declspec(dllexport) const char *NAME = "GTAVE.FPV";
__declspec(dllexport) const char *DESCRIPTION = "GTA V Enhanced FPV drone (Phase 0 probe spike)";
}

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

// Latest down-probe result, written by the script fiber, read by the overlay.
struct Shared {
    std::mutex mutex;
    shv::Vec3 player{};
    Hit hit;
    int type = 0;
    double us = 0;
    unsigned frames = 0;
} g_shared;

HMODULE g_module;
bool g_addon = false;

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

void down_probe(bool log) {
    int ped = shv::call<int>(PLAYER_PED_ID);
    shv::Vec3 p = shv::call<shv::Vec3>(GET_ENTITY_COORDS, ped, true);
    shv::Vec3 below{p.x, p.y, p.z - 50.0f};

    double t0 = now_us();
    Hit h = probe(p, below, ped);
    double us = now_us() - t0;
    int type = h.entity ? shv::call<int>(GET_ENTITY_TYPE, h.entity) : 0;

    {
        std::lock_guard<std::mutex> lock(g_shared.mutex);
        g_shared.player = p;
        g_shared.hit = h;
        g_shared.type = type;
        g_shared.us = us;
        ++g_shared.frames;
    }
    if (!log) return;
    fpvlog::write("player %.2f %.2f %.2f | status %d hit %d at %.2f %.2f %.2f normal %.2f %.2f %.2f"
                  " entity %d type %d | %.1f us",
                  p.x, p.y, p.z, h.status, h.hit, h.end.x, h.end.y, h.end.z, h.normal.x,
                  h.normal.y, h.normal.z, h.entity, type, us);
}

// libfpv casts 15 rays per tick; this tells us what they cost.
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

// Runs on ReShade's render thread every frame, overlay open or closed.
void draw_overlay(reshade::api::effect_runtime *) {
    shv::Vec3 p, end, normal;
    int status, hit, type;
    double us;
    unsigned frames;
    {
        std::lock_guard<std::mutex> lock(g_shared.mutex);
        p = g_shared.player;
        end = g_shared.hit.end;
        normal = g_shared.hit.normal;
        status = g_shared.hit.status;
        hit = g_shared.hit.hit;
        type = g_shared.type;
        us = g_shared.us;
        frames = g_shared.frames;
    }

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("GTAVE.FPV", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted("GTAVE.FPV overlay OK");
    ImGui::Text("player  %.1f %.1f %.1f", p.x, p.y, p.z);
    if (status == 2 && hit)
        ImGui::Text("ground  %.2f m below  normal %.2f %.2f %.2f  type %d", p.z - end.z, normal.x,
                    normal.y, normal.z, type);
    else
        ImGui::Text("ground  no hit (status %d)", status);
    ImGui::Text("probe   %.1f us   frames %u", us, frames);
    ImGui::End();
}

// ReShade.asi loads after this ASI (the loader goes alphabetically), so the
// fiber registers once ReShade is in the process.
void try_register_addon() {
    if (g_addon || !reshade::register_addon(g_module)) return;
    reshade::register_overlay(nullptr, &draw_overlay);
    g_addon = true;
    fpvlog::write("registered with ReShade as an add-on (API %d)", RESHADE_API_VERSION);
}

void script_main() {
    fpvlog::write("probe spike running");
    DWORD next_log = 0;
    bool burst_done = false;
    for (;;) {
        try_register_addon();
        DWORD tick = GetTickCount();
        bool log = tick >= next_log;
        down_probe(log);
        if (log) {
            if (!burst_done) {
                log_burst_cost();
                burst_done = true;
            }
            if (!g_addon) fpvlog::write("ReShade add-on registration still pending");
            next_log = tick + 2000;
        }
        shv::wait(0);
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        shv::register_script(instance, script_main);
    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        // Only on a real unload. At process exit ReShade may already be gone.
        if (g_addon) {
            reshade::unregister_overlay(nullptr, &draw_overlay);
            reshade::unregister_addon(instance);
        }
        shv::unregister_script(instance);
    }
    return TRUE;
}
