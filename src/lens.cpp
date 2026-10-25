#include "lens.h"

#include <cstring>

#include "drone.h"
#include "fpvapi.h"
#include "log.h"

namespace lens {
namespace {

namespace api = reshade::api;

constexpr const char *kEffect = "GTAVE_FPV_Lens.fx";

enum Var { Active, LensK1, LensK2, Noise, Breakup, Digital, RectMin, RectMax, Bezel, VarCount };
const char *const kNames[VarCount] = {"Active",  "LensK1",  "LensK2",  "Noise", "Breakup",
                                      "Digital", "RectMin", "RectMax", "Bezel"};

// Uniform handles come from enumerate_uniform_variables, not find_uniform_variable:
// that one returns a handle struct by value, which MSVC (ReShade) and MinGW
// (this mod) return differently.
api::effect_runtime *g_runtime = nullptr;
api::effect_uniform_variable g_vars[VarCount];
bool g_found = false;
// The technique, switched on only in the FPV view so it costs nothing otherwise.
api::effect_technique g_technique = {0};
int g_technique_on = -1;  // -1 unknown: set on the first frame
int g_refind = 0;         // frames until looking the uniforms up again

void find(api::effect_runtime *runtime) {
    g_runtime = runtime;
    g_found = false;
    g_technique = {0};
    g_technique_on = -1;
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
    static bool reported = false;
    if (!g_technique.handle && !reported) {
        reported = true;
        fpvlog::write("lens: %s is not in ReShade (copy it to reshade-shaders\\Shaders)", kEffect);
    }
}

// On in the FPV view, off otherwise. Enabling an effect ReShade skipped at
// startup ("load only enabled effects") queues it for loading, so its
// uniforms can appear a few frames later.
void switch_technique(api::effect_runtime *runtime, bool on) {
    if (!g_technique.handle || g_technique_on == int(on)) return;
    runtime->set_technique_state(g_technique, on);
    g_technique_on = on;
    if (on && !g_found) g_refind = 30;
}

void set(api::effect_runtime *rt, Var v, float x, float y = 0, size_t count = 1) {
    const float values[2] = {x, y};
    if (g_vars[v].handle) rt->set_uniform_value_float(g_vars[v], values, count, 0);
}

}  // namespace

void begin_effects(api::effect_runtime *runtime) {
    if (runtime != g_runtime || (g_refind > 0 && --g_refind == 0)) find(runtime);
    drone::LensState s = drone::lens_state();
    switch_technique(runtime, s.active);
    if (!g_found) return;
    set(runtime, Active, s.active ? 1.0f : 0.0f);
    if (!s.active) return;
    set(runtime, LensK1, s.k1);
    set(runtime, LensK2, s.k2);
    set(runtime, Noise, s.noise);
    set(runtime, Breakup, s.breakup);
    set(runtime, Digital, s.digital ? 1.0f : 0.0f);

    // The picture rectangle, as fractions of the window.
    const render::Rect r = render::picture_rect(s.digital);
    const render::Rect screen = render::picture_rect_of_window();
    set(runtime, RectMin, r.x0 / screen.w(), r.y0 / screen.h(), 2);
    set(runtime, RectMax, r.x1 / screen.w(), r.y1 / screen.h(), 2);
    set(runtime, Bezel, render::bezel() ? 1.0f : 0.0f);
}

void forget(api::effect_runtime *runtime) {
    if (runtime == g_runtime) g_runtime = nullptr;  // look the handles up again
}

}  // namespace lens
