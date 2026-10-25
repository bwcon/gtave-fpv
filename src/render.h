// Draws libfpv draw data (the overlay and the OSD) with ReShade's ImGui.
// libfpv's font atlases become ReShade textures.
#pragma once

#include <windows.h>  // before ReShade's headers: MinGW's __uuidof needs it

#define ImTextureID ImU64  // ReShade passes resource views as 64-bit texture IDs
#include <imgui.h>
#include <libfpv.h>
#include <reshade.hpp>

namespace render {

// Copies the draw lists into `dl`, offset by `origin`. Render thread only.
void draw(ImDrawList *dl, const fpv_draw_data *dd, const ImVec2 &origin, reshade::api::effect_runtime *runtime);

// Render thread: frees the textures if they live on this device.
void release_device(reshade::api::device *device);

// Core unload: frees the textures after the GPU finished with them.
void release_all();

// ReShade is built with MSVC, which returns ImVec2 (it has constructors)
// through a hidden pointer passed in RCX. MinGW expects it in RAX, so calling
// such ImGui functions directly lets ReShade write to a junk address. Call
// their table entries through this. ImGui functions returning void, bool,
// float or references are fine.
ImVec2 msvc_vec2(ImVec2 (*fn)());

// The game window's size, from the last ReShade frame. Any thread.
void set_screen(float width, float height);

// Goggles bezel on or off (G key). Any thread.
void set_bezel(bool on);
bool bezel();

// The bezel image's opening, as window fractions ([View] BezelRect). Any thread.
void set_bezel_rect(float x0, float y0, float x1, float y1);

// Where the FPV picture goes, in window pixels: the bezel image's opening
// when the bezel is on, the whole window otherwise.
struct Rect {
    float x0, y0, x1, y1;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
};
Rect picture_rect(bool hd);
Rect picture_rect_of_window();  // the whole window
float picture_aspect(bool hd);  // width / height of picture_rect

}  // namespace render
