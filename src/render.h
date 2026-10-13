// Draws libfpv's OSD and F8 overlay over the game (D3D9, render thread).
#pragma once

#include <d3d9.h>

namespace render {

// After the game finished its frame; called by the loader's D3D hook.
void frame(IDirect3DDevice9 *dev);

// Releases textures (device lost, core unload). Render thread only.
void release();

}  // namespace render
