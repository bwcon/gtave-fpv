#include "lens.h"

#include <atomic>
#include <cmath>
#include <cstring>

#include "drone.h"
#include "fpvapi.h"
#include "log.h"

namespace lens {
namespace {

namespace api = reshade::api;
using fpvapi::fpv;

constexpr const char *kEffect = "GTAVE_FPV_Lens.fx";

enum Var {
    Active, LensK1, LensK2, Noise, Breakup, Digital, RectMin, RectMax,
    Goggles, BezelSize, Fov, SignalLook,  // the player's settings in the effect's ReShade panel
    VarCount
};
const char *const kNames[VarCount] = {"Active",  "LensK1",  "LensK2",    "Noise",     "Breakup", "Digital",
                                      "RectMin", "RectMax", "Goggles", "BezelSize", "Fov",     "SignalLook"};
enum Look { LookAuto, LookAnalog, LookDigital, LookClean };

// Uniform handles come from enumerate_uniform_variables, not find_uniform_variable:
// that one returns a handle struct by value, which MSVC (ReShade) and MinGW
// (this mod) return differently.
api::effect_runtime *g_runtime = nullptr;
api::effect_uniform_variable g_vars[VarCount];
bool g_found = false;
// The technique, switched on only in the FPV view so it costs nothing otherwise.
api::effect_technique g_technique = {0};
int g_refind = 0;  // frames until looking the technique and uniforms up again
std::atomic<bool> g_toggle_goggles{false};
float g_fov_seen = -1;  // the FOV slider's value last frame; -1: take libfpv's

void find(api::effect_runtime *runtime) {
    g_runtime = runtime;
    g_found = false;
    g_technique = {0};
    g_fov_seen = -1;
    runtime->enumerate_techniques(kEffect, [](api::effect_runtime *, api::effect_technique tech) { g_technique = tech; });
    for (auto &v : g_vars) v = {0};
    runtime->enumerate_uniform_variables(kEffect, [](api::effect_runtime *rt, api::effect_uniform_variable var) {
        char name[64] = "";
        size_t size = sizeof name;
        rt->get_uniform_variable_name(var, name, &size);
        for (int i = 0; i < VarCount; ++i)
            if (std::strcmp(name, kNames[i]) == 0) g_vars[i] = var;
    });
    g_found = g_vars[Active].handle != 0;
    if (g_technique.handle) return;
    // ReShade compiles effects in the background after start and after a
    // reload, so the effect can appear later: keep looking every 2 s.
    g_refind = 120;
    static bool reported = false;
    if (!reported) {
        reported = true;
        fpvlog::write("lens: %s is not in ReShade yet (copy it to reshade-shaders\\Shaders)", kEffect);
    }
}

// Always enabled; Active = 0 passes the image through outside the FPV view.
// A technique switched off gets saved in the preset as disabled, and with
// "load only enabled effects" ReShade then skips the effect on the next
// start, where neither its menu nor this add-on can see it.
void keep_enabled(api::effect_runtime *runtime) {
    if (!g_technique.handle || runtime->get_technique_state(g_technique)) return;
    runtime->set_technique_state(g_technique, true);
    if (!g_found) g_refind = 30;
}

void set(api::effect_runtime *rt, Var v, float x, float y = 0, size_t count = 1) {
    const float values[2] = {x, y};
    if (g_vars[v].handle) rt->set_uniform_value_float(g_vars[v], values, count, 0);
}

float get_float(api::effect_runtime *rt, Var v, float fallback) {
    float x = fallback;
    if (g_vars[v].handle) rt->get_uniform_value_float(g_vars[v], &x, 1, 0);
    return x;
}
int get_int(api::effect_runtime *rt, Var v, int fallback) {
    int x = fallback;
    if (g_vars[v].handle) rt->get_uniform_value_int(g_vars[v], &x, 1, 0);
    return x;
}
bool get_bool(api::effect_runtime *rt, Var v, bool fallback) {
    bool x = fallback;
    if (g_vars[v].handle) rt->get_uniform_value_bool(g_vars[v], &x, 1, 0);
    return x;
}

// The panel's FOV slider and libfpv's own (overlay Camera page) stay equal:
// whichever moved last wins. libfpv saves its value; ReShade saves the slider's.
void sync_fov(api::effect_runtime *rt) {
    if (!g_vars[Fov].handle || !fpv.fpv_camera_get_fov || !fpvapi::loaded()) return;
    float slider = get_float(rt, Fov, 0), lib = fpv.fpv_camera_get_fov();
    if (g_fov_seen >= 0 && slider != g_fov_seen) {
        fpv.fpv_camera_set_fov(slider);
    } else if (std::fabs(slider - lib) > 0.01f) {
        set(rt, Fov, lib);
        slider = lib;
    }
    g_fov_seen = slider;
}

}  // namespace

void begin_effects(api::effect_runtime *runtime) {
    if (runtime != g_runtime || (g_refind > 0 && --g_refind == 0)) find(runtime);
    drone::LensState s = drone::lens_state();
    keep_enabled(runtime);
    if (!g_found) {
        render::set_bezel(false);  // nothing draws it
        return;
    }
    bool goggles = get_bool(runtime, Goggles, true);
    if (g_toggle_goggles.exchange(false)) {
        goggles = !goggles;
        runtime->set_uniform_value_bool(g_vars[Goggles], &goggles, 1, 0);
        runtime->save_current_preset();
    }
    render::set_bezel(goggles);
    render::set_bezel_scale(get_float(runtime, BezelSize, 1.0f));
    sync_fov(runtime);

    set(runtime, Active, s.active ? 1.0f : 0.0f);
    if (!s.active) return;
    const int look = get_int(runtime, SignalLook, LookAuto);
    const bool clean = look == LookClean;
    set(runtime, LensK1, s.k1);
    set(runtime, LensK2, s.k2);
    set(runtime, Noise, clean ? 0.0f : s.noise);
    set(runtime, Breakup, clean ? 0.0f : s.breakup);
    set(runtime, Digital, (look == LookAuto ? s.digital : look == LookDigital) ? 1.0f : 0.0f);

    // The picture rectangle, as fractions of the window.
    const render::Rect r = render::picture_rect(s.digital);
    const render::Rect screen = render::picture_rect_of_window();
    set(runtime, RectMin, r.x0 / screen.w(), r.y0 / screen.h(), 2);
    set(runtime, RectMax, r.x1 / screen.w(), r.y1 / screen.h(), 2);
}

void toggle_goggles() { g_toggle_goggles = true; }

void forget(api::effect_runtime *runtime) {
    if (runtime == g_runtime) g_runtime = nullptr;  // look the handles up again
}

}  // namespace lens
