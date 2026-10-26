// libfpv.dll, loaded at runtime on the first launch so a missing or broken DLL
// never stops the game (or the rest of the mod) from starting.
#pragma once

#include <libfpv.h>

namespace fpvapi {

#define FPV_FUNCTIONS(X)                                                                         \
    X(fpv_global_config_defaults) X(fpv_global_init) X(fpv_last_error) X(fpv_result_string)      \
    X(fpv_version_string) X(fpv_world_set_raycast_callback) X(fpv_drone_desc_defaults)           \
    X(fpv_drone_create) X(fpv_drone_delete) X(fpv_drone_reset) X(fpv_drone_repair)               \
    X(fpv_drone_tick) X(fpv_input_key) X(fpv_input_get_status) X(fpv_input_set_channels)         \
    X(fpv_texture_get) X(fpv_overlay_is_open) X(fpv_overlay_wants_input) X(fpv_overlay_set_display) \
    X(fpv_overlay_mouse_move) X(fpv_overlay_mouse_button) X(fpv_overlay_mouse_wheel)             \
    X(fpv_overlay_render) X(fpv_osd_draw_data) X(fpv_audio_configure) X(fpv_audio_read)          \
    X(fpv_overlay_set_action) X(fpv_overlay_poll_action) X(fpv_gps_set_home) X(fpv_overlay_set_open) \
    X(fpv_overlay_text)

// Only in bwcon/libfpv builds; null with an upstream libfpv.dll.
#define FPV_FORK_FUNCTIONS(X) X(fpv_shared_data_dir) X(fpv_osd_get_style) X(fpv_osd_set_style) X(fpv_osd_set_logo) \
    X(fpv_camera_get_fov) X(fpv_camera_set_fov)

struct Api {
#define FPV_FN(name) decltype(&::name) name = nullptr;
    FPV_FUNCTIONS(FPV_FN)
    FPV_FORK_FUNCTIONS(FPV_FN)
#undef FPV_FN
};

extern Api fpv;

// Loads and initialises libfpv once; later calls return the first result.
// Script fiber only.
bool load();
// Any thread: true once every function in `fpv` is set.
bool loaded();

// The player's OSD look (the overlay's OSD tab), analog with upstream builds.
fpv_osd_style osd_style();
// Analog <-> HD (hotkey). No-op with upstream builds.
void toggle_osd_style();

}  // namespace fpvapi
