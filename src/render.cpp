#include "render.h"

#include <windows.h>

#include <vector>

#include "drone.h"
#include "fpvapi.h"

namespace render {
namespace {

using fpvapi::fpv;

struct Vertex {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};
constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct Texture {
    IDirect3DTexture9 *tex = nullptr;
    uint32_t revision = 0;
};
Texture g_textures[3];  // indexed by FPV_TEXTURE_* (1 overlay font, 2 OSD font)
std::vector<Vertex> g_vertices;
LARGE_INTEGER g_last;

IDirect3DTexture9 *texture(IDirect3DDevice9 *dev, uint64_t id) {
    if (id >= 3) return nullptr;
    fpv_texture t{};
    if (fpv.fpv_texture_get(id, &t) != FPV_OK || !t.rgba) return nullptr;
    Texture &slot = g_textures[id];
    if (slot.tex && slot.revision == t.revision) return slot.tex;
    if (slot.tex) slot.tex->Release();
    slot.tex = nullptr;
    if (FAILED(dev->CreateTexture(t.width, t.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &slot.tex, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(slot.tex->LockRect(0, &lr, nullptr, 0))) {
        for (uint32_t y = 0; y < t.height; ++y) {
            const uint8_t *src = t.rgba + size_t(y) * t.width * 4;
            uint8_t *dst = static_cast<uint8_t *>(lr.pBits) + size_t(y) * lr.Pitch;
            for (uint32_t x = 0; x < t.width; ++x, src += 4, dst += 4) {  // RGBA -> BGRA
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
            }
        }
        slot.tex->UnlockRect(0);
    }
    slot.revision = t.revision;
    return slot.tex;
}

// Fixed-function state for alpha-blended screen-space quads, as in Dear
// ImGui's DX9 backend.
void setup_state(IDirect3DDevice9 *dev, float w, float h) {
    D3DVIEWPORT9 vp = {0, 0, DWORD(w), DWORD(h), 0.0f, 1.0f};
    dev->SetViewport(&vp);
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetFVF(kFvf);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPING, TRUE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
}

void draw(IDirect3DDevice9 *dev, const fpv_draw_data &dd) {
    for (uint32_t l = 0; l < dd.list_count; ++l) {
        const fpv_draw_list &list = dd.lists[l];
        g_vertices.resize(list.vertex_count);
        for (uint32_t i = 0; i < list.vertex_count; ++i) {
            const fpv_draw_vertex &s = list.vertices[i];
            uint32_t c = s.color;  // ImGui ABGR -> D3D ARGB
            c = (c & 0xff00ff00u) | ((c & 0xffu) << 16) | ((c >> 16) & 0xffu);
            g_vertices[i] = {s.pos[0] - 0.5f, s.pos[1] - 0.5f, 0.0f, 1.0f, c, s.uv[0], s.uv[1]};
        }
        for (uint32_t c = 0; c < list.command_count; ++c) {
            const fpv_draw_cmd &cmd = list.commands[c];
            if (!cmd.index_count || cmd.vertex_offset >= list.vertex_count) continue;
            RECT clip = {LONG(cmd.clip_rect[0]), LONG(cmd.clip_rect[1]), LONG(cmd.clip_rect[2]), LONG(cmd.clip_rect[3])};
            if (clip.right <= clip.left || clip.bottom <= clip.top) continue;
            dev->SetScissorRect(&clip);
            dev->SetTexture(0, texture(dev, cmd.texture_id));
            dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, list.vertex_count - cmd.vertex_offset,
                                        cmd.index_count / 3, list.indices + cmd.index_offset, D3DFMT_INDEX32,
                                        g_vertices.data() + cmd.vertex_offset, sizeof(Vertex));
        }
    }
}

// Mouse for the overlay, in back buffer pixels.
void feed_mouse(IDirect3DDevice9 *dev, float w, float h) {
    D3DDEVICE_CREATION_PARAMETERS cp;
    if (FAILED(dev->GetCreationParameters(&cp)) || !cp.hFocusWindow) return;
    POINT p;
    RECT rc;
    if (!GetCursorPos(&p) || !ScreenToClient(cp.hFocusWindow, &p) || !GetClientRect(cp.hFocusWindow, &rc)) return;
    float sx = rc.right > 0 ? w / float(rc.right) : 1.0f, sy = rc.bottom > 0 ? h / float(rc.bottom) : 1.0f;
    fpv.fpv_overlay_mouse_move(float(p.x) * sx, float(p.y) * sy);
    static bool was[3];
    const int vk[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
    for (int b = 0; b < 3; ++b) {
        bool down = (GetAsyncKeyState(vk[b]) & 0x8000) != 0;
        if (down != was[b]) fpv.fpv_overlay_mouse_button(b, down);
        was[b] = down;
    }
}

}  // namespace

void frame(IDirect3DDevice9 *dev) {
    if (!fpvapi::loaded()) return;

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = g_last.QuadPart ? float(now.QuadPart - g_last.QuadPart) / float(freq.QuadPart) : 0.016f;
    g_last = now;

    IDirect3DSurface9 *backbuffer = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) return;
    D3DSURFACE_DESC desc;
    backbuffer->GetDesc(&desc);
    float w = float(desc.Width), h = float(desc.Height);

    fpv.fpv_overlay_set_display(w, h, h > 1080.0f ? h / 1080.0f : 1.0f);
    if (fpv.fpv_overlay_is_open()) feed_mouse(dev, w, h);

    static fpv_frame snapshot;
    const fpv_draw_data *osd = nullptr;
    if (drone::osd_frame(&snapshot)) osd = fpv.fpv_osd_draw_data(&snapshot, w, h, FPV_OSD_STYLE_ANALOG);
    const fpv_draw_data *overlay = fpv.fpv_overlay_render(dt);
    if ((!osd || !osd->list_count) && (!overlay || !overlay->list_count)) {
        backbuffer->Release();
        return;
    }

    IDirect3DStateBlock9 *state = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &state))) {
        backbuffer->Release();
        return;
    }
    IDirect3DSurface9 *old_rt = nullptr, *old_ds = nullptr;
    dev->GetRenderTarget(0, &old_rt);
    dev->GetDepthStencilSurface(&old_ds);
    dev->SetRenderTarget(0, backbuffer);
    dev->SetDepthStencilSurface(nullptr);

    setup_state(dev, w, h);
    if (osd) draw(dev, *osd);
    if (overlay) draw(dev, *overlay);

    dev->SetRenderTarget(0, old_rt);
    dev->SetDepthStencilSurface(old_ds);
    if (old_rt) old_rt->Release();
    if (old_ds) old_ds->Release();
    state->Apply();
    state->Release();
    backbuffer->Release();
}

void release() {
    for (Texture &t : g_textures) {
        if (t.tex) t.tex->Release();
        t = Texture{};
    }
}

}  // namespace render
