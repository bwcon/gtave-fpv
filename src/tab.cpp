#include "tab.h"

#include <atomic>
#include <cstdio>
#include <string>

#include "drone.h"
#include "gear.h"
#include "osd.h"
#include "race.h"
#include "fpvapi.h"
#include "log.h"
#include "render.h"

namespace tab {
namespace {

using fpvapi::fpv;
namespace api = reshade::api;

std::atomic<bool> g_load_requested{false};
std::atomic<DWORD> g_last_draw{0};  // GetTickCount of the last tab frame, 0 = never

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

// The loaded track: its name, personal best and ghost, and sharing it as text.
void draw_tracks() {
    static char name[64], author[64];
    static std::string shown_id, message;
    static bool author_read = false;
    if (!author_read) {
        std::snprintf(author, sizeof author, "%s", race::author().c_str());
        author_read = true;
    }
    const race::TrackInfo t = race::track();
    if (t.loaded) {
        if (shown_id != t.id) {  // another track: show its name
            std::snprintf(name, sizeof name, "%s", t.name);
            shown_id = t.id;
        }
        char pb[32] = "none";
        if (t.best_lap > 0) {
            const int m = int(t.best_lap / 60);
            std::snprintf(pb, sizeof pb, "%d:%05.2f%s", m, t.best_lap - m * 60, t.has_ghost ? " (ghost)" : "");
        }
        ImGui::Text("Track %s by %s   %d gates, %s   id %s   PB %s", t.name, t.author[0] ? t.author : "?", t.gates,
                    t.point_to_point ? "point to point" : "circuit", t.id, pb);
        if (ImGui::InputText("track name (Enter saves)", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue,
                             nullptr, nullptr))
            race::rename_track(name);
        if (ImGui::Button("Copy track", ImVec2(0, 0)))
            message = race::copy_track() ? "copied: paste it anywhere to share it" : "cannot copy to the clipboard";
        ImGui::SameLine(0.0f, -1.0f);
    } else {
        shown_id.clear();
        ImGui::TextUnformatted("No track loaded: L loads your saved races, N records a new one");
    }
    if (ImGui::Button("Paste track", ImVec2(0, 0))) message = race::paste_track();
    if (!message.empty()) {
        ImGui::SameLine(0.0f, -1.0f);
        ImGui::TextUnformatted(message.c_str());
    }
    if (ImGui::InputText("your name on new tracks (Enter saves)", author, sizeof author,
                         ImGuiInputTextFlags_EnterReturnsTrue, nullptr, nullptr))
        race::set_author(author);
    bool ghost = race::ghost();
    if (ImGui::Checkbox("Ghost: race against your best lap", &ghost)) race::set_ghost(ghost);
}

void draw_status() {
    drone::Status s = drone::status();
    if (s.flying) {
        ImGui::Text("Flying   %.0f km/h   %.1f %.1f %.1f", s.speed_kmh, s.position[0], s.position[1], s.position[2]);
        if (s.arming[0]) ImGui::Text("Arming blocked: %s", s.arming);
    } else {
        ImGui::Text("Input: %s%s", s.device[0] ? s.device : "keyboard", s.needs_setup ? " (needs setup)" : "");
    }
    ImGui::TextUnformatted("F9 launch / land   C camera   R respawn   Y move player here   O OSD analog / HD   G goggles   P new props");
    ImGui::TextUnformatted("Races: N record / save   M or AUX3 mark gate   U undo   E finish here (point to point)"
                           "   L next race / off");
    int laps = race::laps();
    if (ImGui::SliderInt("laps per race (0 endless)", &laps, 0, 20, "%d", 0)) race::set_laps(laps);
    draw_tracks();
    bool realism = !drone::option(drone::NewPropsOnDisarm);
    if (ImGui::Checkbox("Full realism: bent props stay bent until P or R (no new props on disarm)", &realism))
        drone::set_option(drone::NewPropsOnDisarm, !realism);
    bool repair = drone::option(drone::RepairOnArm);
    if (ImGui::Checkbox("Repair on arm: new props, motors and a full battery every time you arm", &repair))
        drone::set_option(drone::RepairOnArm, repair);
    bool sticks = osd::stick_overlay();
    if (ImGui::Checkbox("Stick overlay in the goggles view (mode 2)", &sticks)) osd::set_stick_overlay(sticks);
    // The pilot's gear: live while flying, saved when a slider is let go.
    for (int i = 0; i < gear::ItemCount; ++i) {
        const gear::Item item = gear::Item(i);
        gear::Placement g = gear::placement(item);
        ImGui::PushID(gear::label(item));
        bool changed = ImGui::Checkbox(gear::label(item), &g.show);
        bool save = changed;
        if (g.show && item == gear::Goggles && ImGui::BeginCombo("colour", gear::colour_label(gear::goggles_colour()), 0)) {
            for (int k = 0; k < gear::colour_count(); ++k)
                if (ImGui::Selectable(gear::colour_label(k), k == gear::goggles_colour(), 0, ImVec2(0, 0)))
                    gear::set_goggles_colour(k);
            ImGui::EndCombo();
        }
        if (g.show && item == gear::Radio && ImGui::BeginCombo("hold", gear::pose_label(gear::radio_pose()), 0)) {
            for (int k = 0; k < gear::pose_count(); ++k)
                if (ImGui::Selectable(gear::pose_label(k), k == gear::radio_pose(), 0, ImVec2(0, 0))) {
                    gear::set_radio_pose(k);
                    g = gear::placement(item);  // the pose's own placement
                }
            ImGui::EndCombo();
        }
        if (g.show) {
            changed |= ImGui::SliderFloat3("offset (m)", g.offset, -0.3f, 0.3f, "%.3f");
            save |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= ImGui::SliderFloat3("rotation (deg)", g.rotation, -180.0f, 180.0f, "%.0f");
            save |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::PopID();
        if (changed || save) gear::set_placement(item, g, save);
    }
    if (fpvapi::loaded()) {
        bool home = drone::home_on_every_arm();
        if (ImGui::Checkbox("New GPS home point at every arm (off: only the first arm)", &home))
            drone::set_home_on_every_arm(home);
    }
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

    ImVec2 avail = render::msvc_vec2(imgui_function_table_instance()->GetContentRegionAvail);
    if (avail.x < 400) avail.x = 400;
    if (avail.y < 400) avail.y = 400;
    const ImVec2 origin = render::msvc_vec2(imgui_function_table_instance()->GetCursorScreenPos);
    fpv.fpv_overlay_set_open(1);
    fpv.fpv_overlay_set_display(avail.x, avail.y, 1.0f);
    if (fpv.fpv_overlay_set_embedded) fpv.fpv_overlay_set_embedded(1);  // the tab is its window
    // Takes the mouse over the area, so ReShade does not drag its window instead.
    ImGui::InvisibleButton("##libfpv", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    forward_input(origin, ImGui::IsItemHovered(0), ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    render::draw(ImGui::GetWindowDrawList(), fpv.fpv_overlay_render(ImGui::GetIO().DeltaTime), origin, runtime);
}

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
