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

void find(api::effect_runtime *runtime) {
    g_runtime = runtime;
    g_found = false;
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
    if (!g_found && !reported) {
        reported = true;
        fpvlog::write("lens: %s is not loaded (add GTAVE_FPV_Lens@%s to the ReShade preset)", kEffect, kEffect);
    }
}

void set(api::effect_runtime *rt, Var v, float x, float y = 0, size_t count = 1) {
    const float values[2] = {x, y};
    if (g_vars[v].handle) rt->set_uniform_value_float(g_vars[v], values, count, 0);
}

}  // namespace

void begin_effects(api::effect_runtime *runtime) {
    if (runtime != g_runtime) find(runtime);
    if (!g_found) return;
    drone::LensState s = drone::lens_state();
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
