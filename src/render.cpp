#include "render.h"

#include <atomic>

#include "fpvapi.h"
#include "log.h"

namespace render {
namespace {

using fpvapi::fpv;
namespace api = reshade::api;

// libfpv's font atlases as GPU textures, re-uploaded when their revision changes.
struct Texture {
    uint64_t id;
    uint32_t revision = ~0u;
    api::resource resource{0};
    api::resource_view view{0};
};
Texture g_textures[] = {{FPV_TEXTURE_OVERLAY_FONT}, {FPV_TEXTURE_OSD_FONT}};
api::device *g_device = nullptr;
api::command_queue *g_queue = nullptr;

void destroy(Texture &t) {
    if (t.view.handle) g_device->destroy_resource_view(t.view);
    if (t.resource.handle) g_device->destroy_resource(t.resource);
    t = Texture{t.id};
}

void destroy_all(bool wait) {
    if (!g_device) return;
    if (wait && g_queue) g_queue->wait_idle();  // the last frame may still sample them
    for (Texture &t : g_textures) destroy(t);
    g_device = nullptr;
    g_queue = nullptr;
}

ImTextureID texture(uint64_t id, api::effect_runtime *runtime) {
    Texture *t = nullptr;
    for (Texture &c : g_textures)
        if (c.id == id) t = &c;
    fpv_texture src{};
    if (!t || fpv.fpv_texture_get(id, &src) != FPV_OK || !src.rgba || !src.width || !src.height) return 0;
    if (g_device != runtime->get_device()) destroy_all(true);
    if (t->view.handle && t->revision == src.revision) return t->view.handle;
    g_device = runtime->get_device();
    g_queue = runtime->get_command_queue();
    if (t->view.handle) {
        g_queue->wait_idle();
        destroy(*t);
    }

    api::subresource_data data{};
    data.data = const_cast<uint8_t *>(src.rgba);
    data.row_pitch = src.width * 4;
    data.slice_pitch = src.width * src.height * 4;
    const api::resource_desc desc(src.width, src.height, 1, 1, api::format::r8g8b8a8_unorm, 1,
                                  api::memory_heap::default_, api::resource_usage::shader_resource);
    if (!g_device->create_resource(desc, &data, api::resource_usage::shader_resource, &t->resource) ||
        !g_device->create_resource_view(t->resource, api::resource_usage::shader_resource,
                                        api::resource_view_desc(api::format::r8g8b8a8_unorm, 0, 1, 0, 1), &t->view)) {
        fpvlog::write("render: cannot create the %ux%u texture %llu", src.width, src.height, (unsigned long long)id);
        destroy(*t);
        return 0;
    }
    t->revision = src.revision;
    return t->view.handle;
}

}  // namespace

// libfpv's vertex is ImDrawVert (pos, uv, IM_COL32 colour).
// ponytail: one vertex per index, no sharing; plenty for overlay- and OSD-sized meshes.
void draw(ImDrawList *dl, const fpv_draw_data *dd, const ImVec2 &origin, api::effect_runtime *runtime) {
    if (!dl || !dd) return;
    constexpr uint32_t kChunk = 3 * 10000;  // whole triangles, well under 64K vertices
    for (uint32_t l = 0; l < dd->list_count; ++l) {
        const fpv_draw_list &list = dd->lists[l];
        for (uint32_t c = 0; c < list.command_count; ++c) {
            const fpv_draw_cmd &cmd = list.commands[c];
            ImTextureID tex = texture(cmd.texture_id, runtime);
            if (!tex || !cmd.index_count) continue;
            dl->PushClipRect(ImVec2(origin.x + cmd.clip_rect[0], origin.y + cmd.clip_rect[1]),
                             ImVec2(origin.x + cmd.clip_rect[2], origin.y + cmd.clip_rect[3]), true);
            dl->PushTexture(ImTextureRef(tex));
            for (uint32_t start = 0; start < cmd.index_count; start += kChunk) {
                int n = int(cmd.index_count - start < kChunk ? cmd.index_count - start : kChunk);
                dl->PrimReserve(n, n);
                for (int i = 0; i < n; ++i) {
                    const fpv_draw_vertex &v = list.vertices[cmd.vertex_offset + list.indices[cmd.index_offset + start + i]];
                    dl->PrimVtx(ImVec2(origin.x + v.pos[0], origin.y + v.pos[1]), ImVec2(v.uv[0], v.uv[1]), v.color);
                }
            }
            dl->PopTexture();
            dl->PopClipRect();
        }
    }
}

void release_device(api::device *device) {
    if (device == g_device) destroy_all(false);  // the device is going away: nothing to wait for
}

void release_all() { destroy_all(true); }

namespace {
std::atomic<float> g_aspect{16.0f / 9.0f};
}
float aspect() { return g_aspect; }
void set_aspect(float aspect) {
    if (aspect > 0.1f) g_aspect = aspect;
}

ImVec2 msvc_vec2(ImVec2 (*fn)()) {
    ImVec2 out;
    reinterpret_cast<ImVec2 *(*)(ImVec2 *)>(reinterpret_cast<void *>(fn))(&out);
    return out;
}

}  // namespace render
