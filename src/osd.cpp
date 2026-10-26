#include "osd.h"

#include <cstdio>
#include <cstring>

#include "drone.h"
#include "dvr.h"
#include "fpvapi.h"
#include "race.h"
#include "radio.h"

namespace osd {
namespace {

constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 160);

void text(ImDrawList *dl, float size, const ImVec2 &pos, ImU32 col, const char *s) {
    dl->AddText(ImGui::GetFont(), size, ImVec2(pos.x + 1, pos.y + 1), kShadow, s, nullptr, 0.0f, nullptr);
    dl->AddText(ImGui::GetFont(), size, pos, col, s, nullptr, 0.0f, nullptr);
}

// The radio station for a moment after it changes, top centre of the picture.
void draw_radio_notice(ImDrawList *dl, const render::Rect &r) {
    char s[96];
    if (!radio::notice(s, sizeof s)) return;
    // ponytail: approximate centring (ImGui's text measuring returns ImVec2, an MSVC ABI trap).
    float half = float(std::strlen(s)) * 3.5f, cx = (r.x0 + r.x1) * 0.5f, y = r.y0 + r.h() * 0.08f;
    dl->AddRectFilled(ImVec2(cx - half - 10, y - 6), ImVec2(cx + half + 10, y + 22), IM_COL32(0, 0, 0, 140), 6.0f, 0);
    dl->AddText(ImVec2(cx - half, y), kWhite, s, nullptr);
}

// The goggles' own status bar over the Betaflight OSD: link signal on the
// left with bitrate and latency (DJI, HD) or the video channel (analog), and
// the recording dot and timer on the right.
// ponytail: bitrate and latency are derived from libfpv's signal and noise, not a link model.
void draw_goggles_hud(ImDrawList *dl, const render::Rect &r, const fpv_frame &f, bool hd) {
    const float size = r.h() * 0.026f, top = r.y0 + r.h() * 0.015f, side = r.w() * 0.02f;
    const float signal = f.video.signal_strength, noise = f.video.noise;

    // Four signal bars.
    const int lit = signal > 0.75f ? 4 : signal > 0.5f ? 3 : signal > 0.25f ? 2 : signal > 0.05f ? 1 : 0;
    for (int i = 0; i < 4; ++i) {
        float h = size * (0.35f + 0.2f * float(i)), x = r.x0 + side + float(i) * size * 0.32f;
        dl->AddRectFilled(ImVec2(x, top + size - h), ImVec2(x + size * 0.22f, top + size),
                          i < lit ? kWhite : IM_COL32(255, 255, 255, 70), 1.0f, 0);
    }
    char s[64];
    float mbps = lit ? 50.0f * signal * (1.0f - 0.6f * noise) : 0.0f;
    int ms = lit ? int(24.0f + 60.0f * noise) : 0;
    if (hd) std::snprintf(s, sizeof s, "%.1fMbps  %dms", mbps, ms);
    else std::snprintf(s, sizeof s, "R1 5658MHz");  // raceband 1, the usual analog channel
    text(dl, size, ImVec2(r.x0 + side + size * 1.5f, top), kWhite, s);

    // Recording dot (blinking while the DVR records) and the flight timer.
    double t = drone::flight_seconds();
    int secs = t > 0 ? int(t) : 0;
    std::snprintf(s, sizeof s, "%02d:%02d", secs / 60, secs % 60);
    const float right = r.x1 - side - size * 3.0f;
    if (dvr::recording() && secs % 2 == 0)
        dl->AddCircleFilled(ImVec2(right - size * 0.6f, top + size * 0.5f), size * 0.28f, IM_COL32(230, 40, 40, 255), 16);
    text(dl, size, ImVec2(right, top), kWhite, s);
}

// Lap, gate and times, top centre of the picture.
void draw_race(ImDrawList *dl, const render::Rect &r) {
    char s[160];
    if (!race::hud(s, sizeof s)) return;
    const float size = r.h() * 0.03f;
    // ponytail: approximate centring, as in draw_radio_notice
    float half = float(std::strlen(s)) * size * 0.26f, cx = (r.x0 + r.x1) * 0.5f, y = r.y0 + r.h() * 0.12f;
    dl->AddRectFilled(ImVec2(cx - half - 12, y - 6), ImVec2(cx + half + 12, y + size + 6), IM_COL32(0, 0, 0, 120), 6.0f, 0);
    text(dl, size, ImVec2(cx - half, y), kWhite, s);
}

}  // namespace

void draw(reshade::api::effect_runtime *runtime) {
    if (!fpvapi::loaded()) return;
    const ImGuiIO &io = ImGui::GetIO();
    render::set_screen(io.DisplaySize.x, io.DisplaySize.y);
    const fpv_osd_style style = fpvapi::osd_style();
    const render::Rect r = render::picture_rect(style == FPV_OSD_STYLE_HD);
    // The background list sits under ReShade's own windows, so its menu stays readable.
    ImDrawList *dl = ImGui::GetBackgroundDrawList(nullptr);
    draw_radio_notice(dl, r);
    fpv_frame frame;
    if (!drone::osd_frame(&frame)) return;
    using fpvapi::fpv;
    // libfpv centres the grid at 4:3 (analog) or 16:9 (HD) inside the picture.
    render::draw(dl, fpv.fpv_osd_draw_data(&frame, r.w(), r.h(), style), ImVec2(r.x0, r.y0), runtime);
    draw_goggles_hud(dl, r, frame, style == FPV_OSD_STYLE_HD);
    draw_race(dl, r);
}

}  // namespace osd
