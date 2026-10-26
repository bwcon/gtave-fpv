// The Betaflight OSD over the FPV view, drawn every frame from ReShade's
// reshade_overlay event, so it sits on top of the post-processed game image.
#pragma once

#include "render.h"

namespace osd {

// Render thread, every frame.
void draw(reshade::api::effect_runtime *runtime);

// [View] StickOverlay: the radio's sticks drawn at the bottom of the goggles
// view, as in FPV videos. Any thread.
bool stick_overlay();
void set_stick_overlay(bool on);

}  // namespace osd
