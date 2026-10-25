#include <windows.h>  // before ReShade's headers: MinGW's __uuidof needs it

#define ImTextureID ImU64  // ReShade passes resource views as 64-bit texture IDs
#include <imgui.h>

#include "tab.h"

#include <atomic>

#include "drone.h"
#include "fpvapi.h"
#include "log.h"

namespace tab {
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

// ReShade is built with MSVC, which returns ImVec2 (it has constructors)
// through a hidden pointer passed in RCX. MinGW expects it in RAX, so calling
// ImGui::GetContentRegionAvail() directly lets ReShade write to a junk
// address. Call such table entries the MSVC way. ImGui functions returning
// void, bool, float or references are fine.
ImVec2 msvc_vec2(ImVec2 (*fn)()) {
    ImVec2 out;
    reinterpret_cast<ImVec2 *(*)(ImVec2 *)>(reinterpret_cast<void *>(fn))(&out);
    return out;
}

std::atomic<bool> g_load_requested{false};
std::atomic<DWORD> g_last_draw{0};  // GetTickCount of the last tab frame, 0 = never

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
        fpvlog::write("tab: cannot create the %ux%u texture %llu", src.width, src.height, (unsigned long long)id);
        destroy(*t);
        return 0;
    }
    t->revision = src.revision;
    return t->view.handle;
}

// Copies libfpv's draw lists into the tab's ImGui draw list. libfpv's vertex
// is ImDrawVert (pos, uv, IM_COL32 colour).
// ponytail: one vertex per index, no sharing; plenty for an overlay-sized mesh.
void draw_overlay(api::effect_runtime *runtime, const ImVec2 &origin, float dt) {
    const fpv_draw_data *dd = fpv.fpv_overlay_render(dt);
    if (!dd) return;
    ImDrawList *dl = ImGui::GetWindowDrawList();
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

void append_utf8(char *&out, unsigned c) {
    if (c < 0x80) {
        *out++ = char(c);
    } else if (c < 0x800) {
        *out++ = char(0xC0 | c >> 6);
        *out++ = char(0x80 | (c & 0x3F));
    } else {
        *out++ = char(0xE0 | c >> 12);
        *out++ = char(0x80 | (c >> 6 & 0x3F));
        *out++ = char(0x80 | (c & 0x3F));
    }
}

// Mouse, wheel, keys and typed text over the overlay area go to libfpv.
void forward_input(const ImVec2 &origin, bool hovered, bool focused) {
    ImGuiIO &io = ImGui::GetIO();
    if (hovered) fpv.fpv_overlay_mouse_move(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
    static bool buttons[3];
    for (int b = 0; b < 3; ++b) {
        bool down = io.MouseDown[b] && (hovered || buttons[b]);  // a drag may leave the area
        if (down != buttons[b]) fpv.fpv_overlay_mouse_button(b, down);
        buttons[b] = down;
    }
    if (hovered && (io.MouseWheel != 0 || io.MouseWheelH != 0)) fpv.fpv_overlay_mouse_wheel(io.MouseWheelH, io.MouseWheel);

    struct Key { ImGuiKey imgui; int fpv; };
    static const Key keys[] = {
        {ImGuiKey_Tab, FPV_KEY_TAB}, {ImGuiKey_Enter, FPV_KEY_ENTER}, {ImGuiKey_KeypadEnter, FPV_KEY_ENTER},
        {ImGuiKey_Escape, FPV_KEY_ESCAPE}, {ImGuiKey_Space, FPV_KEY_SPACE},
        {ImGuiKey_LeftArrow, FPV_KEY_LEFT}, {ImGuiKey_RightArrow, FPV_KEY_RIGHT},
        {ImGuiKey_UpArrow, FPV_KEY_UP}, {ImGuiKey_DownArrow, FPV_KEY_DOWN},
        {ImGuiKey_PageUp, FPV_KEY_PAGE_UP}, {ImGuiKey_PageDown, FPV_KEY_PAGE_DOWN},
        {ImGuiKey_Home, FPV_KEY_HOME}, {ImGuiKey_End, FPV_KEY_END}, {ImGuiKey_Insert, FPV_KEY_INSERT},
        {ImGuiKey_Delete, FPV_KEY_DELETE}, {ImGuiKey_Backspace, FPV_KEY_BACKSPACE},
        {ImGuiKey_LeftShift, FPV_KEY_LSHIFT}, {ImGuiKey_RightShift, FPV_KEY_RSHIFT},
        {ImGuiKey_LeftCtrl, FPV_KEY_LCTRL}, {ImGuiKey_RightCtrl, FPV_KEY_RCTRL},
        {ImGuiKey_LeftAlt, FPV_KEY_LALT}, {ImGuiKey_RightAlt, FPV_KEY_RALT},
    };
    static bool was_down[sizeof(keys) / sizeof(keys[0])];
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        bool down = focused && ImGui::IsKeyDown(keys[i].imgui);
        if (down != was_down[i]) fpv.fpv_input_key(keys[i].fpv, down);
        was_down[i] = down;
    }

    if (!focused || io.InputQueueCharacters.Size == 0) return;
    char text[256], *out = text;
    for (int i = 0; i < io.InputQueueCharacters.Size && out < text + sizeof text - 4; ++i) {
        unsigned c = io.InputQueueCharacters[i];
        if (c >= 0x20 && c != 0x7F && (c < 0xD800 || c > 0xDFFF)) append_utf8(out, c);  // no controls, no lone surrogates
    }
    *out = 0;
    if (text[0]) fpv.fpv_overlay_text(text);
}

void draw_status() {
    drone::Status s = drone::status();
    if (s.flying) {
        ImGui::Text("Flying   %.0f km/h   %.1f %.1f %.1f", s.speed_kmh, s.position[0], s.position[1], s.position[2]);
        if (s.arming[0]) ImGui::Text("Arming blocked: %s", s.arming);
    } else {
        ImGui::Text("Input: %s%s", s.device[0] ? s.device : "keyboard", s.needs_setup ? " (needs setup)" : "");
    }
    ImGui::TextUnformatted("F9 launch / land   C camera   R respawn   Y move player here");
    ImGui::Separator();
}

}  // namespace

void draw(api::effect_runtime *runtime) {
    g_last_draw = GetTickCount() | 1;
    if (!fpvapi::loaded()) {
        ImGui::TextUnformatted(g_load_requested ? "Loading libfpv..." : "libfpv loads on the first F9, or here.");
        if (!g_load_requested && ImGui::Button("Load libfpv", ImVec2(0, 0))) g_load_requested = true;
        return;
    }
    draw_status();

    ImVec2 avail = msvc_vec2(imgui_function_table_instance()->GetContentRegionAvail);
    if (avail.x < 400) avail.x = 400;
    if (avail.y < 300) avail.y = 300;
    const ImVec2 origin = msvc_vec2(imgui_function_table_instance()->GetCursorScreenPos);
    fpv.fpv_overlay_set_open(1);
    fpv.fpv_overlay_set_display(avail.x, avail.y, 1.0f);
    // Takes the mouse over the area, so ReShade does not drag its window instead.
    ImGui::InvisibleButton("##libfpv", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    forward_input(origin, ImGui::IsItemHovered(0), ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    draw_overlay(runtime, origin, ImGui::GetIO().DeltaTime);
}

void release_device(api::device *device) {
    if (device == g_device) destroy_all(false);  // the device is going away: nothing to wait for
}

void release_all() { destroy_all(true); }

void tick() {
    if (g_load_requested.exchange(false)) fpvapi::load();
    DWORD last = g_last_draw;
    // ReShade's menu closed or another tab: hand the keyboard back to the flight.
    if (last && GetTickCount() - last > 300 && fpvapi::loaded() && fpv.fpv_overlay_is_open()) {
        fpv.fpv_overlay_set_open(0);
        g_last_draw = 0;
    }
}

}  // namespace tab
