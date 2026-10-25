// ReShade's "GTAVE.FPV" tab: the mod's status and libfpv's own overlay (setup
// wizard, rates, OSD layout...), drawn inside the tab with ReShade's ImGui.
#pragma once

#include <reshade.hpp>

namespace tab {

// Render thread, while the tab is visible.
void draw(reshade::api::effect_runtime *runtime);

// Render thread: frees the textures if they live on this device.
void release_device(reshade::api::device *device);

// Core unload: frees the textures after the GPU finished with them.
void release_all();

// Script fiber, every tick: loads libfpv when the tab asked for it, and
// closes the libfpv overlay once the tab is no longer drawn.
void tick();

}  // namespace tab
