#include "osd.h"

#include <cstring>

#include "drone.h"
#include "fpvapi.h"
#include "radio.h"

namespace osd {
namespace {

// The radio station for a moment after it changes, top centre.
void draw_radio_notice(ImDrawList *dl, const ImVec2 &screen) {
    char text[96];
    if (!radio::notice(text, sizeof text)) return;
    // ponytail: approximate centring (ImGui's text measuring returns ImVec2, an MSVC ABI trap).
    float x = screen.x * 0.5f - float(std::strlen(text)) * 3.5f, y = screen.y * 0.06f;
    dl->AddRectFilled(ImVec2(x - 10, y - 6), ImVec2(screen.x - x + 10, y + 22), IM_COL32(0, 0, 0, 140), 6.0f, 0);
    dl->AddText(ImVec2(x, y), IM_COL32(255, 255, 255, 255), text, nullptr);
}

}  // namespace

void draw(reshade::api::effect_runtime *runtime) {
    if (!fpvapi::loaded()) return;
    const ImGuiIO &io = ImGui::GetIO();
    // The background list sits under ReShade's own windows, so its menu stays readable.
    ImDrawList *dl = ImGui::GetBackgroundDrawList(nullptr);
    draw_radio_notice(dl, io.DisplaySize);
    fpv_frame frame;
    if (!drone::osd_frame(&frame)) return;
    using fpvapi::fpv;
    // libfpv centres the grid at 4:3 (analog) or 16:9 (HD) inside the screen.
    const fpv_draw_data *dd = fpv.fpv_osd_draw_data(&frame, io.DisplaySize.x, io.DisplaySize.y, fpvapi::osd_style());
    render::draw(dl, dd, ImVec2(0, 0), runtime);
}

}  // namespace osd
