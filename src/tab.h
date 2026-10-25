// ReShade's "GTAVE.FPV" tab: the mod's status and libfpv's own overlay (setup
// wizard, rates, OSD layout...), drawn inside the tab with ReShade's ImGui.
#pragma once

#include "render.h"

namespace tab {

// Render thread, while the tab is visible.
void draw(reshade::api::effect_runtime *runtime);

// Script fiber, every tick: loads libfpv when the tab asked for it, and
// closes the libfpv overlay once the tab is no longer drawn.
void tick();

}  // namespace tab
