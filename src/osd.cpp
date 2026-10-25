#include "osd.h"

#include "drone.h"
#include "fpvapi.h"

namespace osd {

void draw(reshade::api::effect_runtime *runtime) {
    fpv_frame frame;
    if (!fpvapi::loaded() || !drone::osd_frame(&frame)) return;
    const ImGuiIO &io = ImGui::GetIO();
    using fpvapi::fpv;
    // libfpv centres the grid at 4:3 (analog) or 16:9 (HD) inside the screen.
    const fpv_draw_data *dd = fpv.fpv_osd_draw_data(&frame, io.DisplaySize.x, io.DisplaySize.y, fpvapi::osd_style());
    // The background list sits under ReShade's own windows, so its menu stays readable.
    render::draw(ImGui::GetBackgroundDrawList(nullptr), dd, ImVec2(0, 0), runtime);
}

}  // namespace osd
