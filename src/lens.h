// Drives the GTAVE_FPV_Lens.fx ReShade effect: the fisheye lens and the
// video look, set from the latest flight frame before ReShade runs its effects.
#pragma once

#include "render.h"

namespace lens {

// reshade_begin_effects, render thread.
void begin_effects(reshade::api::effect_runtime *runtime);

// reshade_reloaded_effects / init: the uniform handles changed.
void forget(reshade::api::effect_runtime *runtime);

// G: flips the effect's Goggles setting and saves the ReShade preset. Any thread.
void toggle_goggles();

}  // namespace lens
