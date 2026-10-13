#include "drone.h"

#include <windows.h>

#include <cmath>
#include <string>

#include <libfpv.h>

#include "log.h"
#include "scripthook.h"
#include "world.h"

namespace drone {
namespace {

constexpr float kRadToDeg = 57.2957795f;

// libfpv.dll is loaded on the first launch so a missing or broken DLL never
// stops the game (or the rest of the mod) from starting.
struct Api {
#define FPV_FN(name) decltype(&::name) name = nullptr;
    FPV_FN(fpv_global_config_defaults)
    FPV_FN(fpv_global_init)
    FPV_FN(fpv_last_error)
    FPV_FN(fpv_result_string)
    FPV_FN(fpv_version_string)
    FPV_FN(fpv_world_set_raycast_callback)
    FPV_FN(fpv_drone_desc_defaults)
    FPV_FN(fpv_drone_create)
    FPV_FN(fpv_drone_delete)
    FPV_FN(fpv_drone_reset)
    FPV_FN(fpv_drone_repair)
    FPV_FN(fpv_drone_tick)
    FPV_FN(fpv_input_key)
    FPV_FN(fpv_input_get_status)
#undef FPV_FN
};

Api fpv;
bool fpv_loaded = false;
bool fpv_failed = false;

std::string game_dir() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path, n);
    return dir.substr(0, dir.find_last_of("\\/"));
}

void on_fpv_log(void *, fpv_log_level level, const char *message) {
    static const char *names[] = {"debug", "info", "warn", "error"};
    fpvlog::write("libfpv %s: %s", names[level & 3], message);
}

bool load_libfpv() {
    if (fpv_loaded) return true;
    if (fpv_failed) return false;
    fpv_failed = true;  // until proven otherwise; don't retry every key press

    std::string dll = game_dir() + "\\libfpv.dll";
    HMODULE lib = LoadLibraryA(dll.c_str());
    if (!lib) {
        fpvlog::write("cannot load %s (error %lu)", dll.c_str(), GetLastError());
        return false;
    }
    bool ok = true;
#define FPV_FN(name)                                                                  \
    fpv.name = reinterpret_cast<decltype(fpv.name)>(reinterpret_cast<void *>(GetProcAddress(lib, #name))); \
    if (!fpv.name) {                                                                   \
        fpvlog::write("libfpv.dll has no %s", #name);                                  \
        ok = false;                                                                    \
    }
    FPV_FN(fpv_global_config_defaults)
    FPV_FN(fpv_global_init)
    FPV_FN(fpv_last_error)
    FPV_FN(fpv_result_string)
    FPV_FN(fpv_version_string)
    FPV_FN(fpv_world_set_raycast_callback)
    FPV_FN(fpv_drone_desc_defaults)
    FPV_FN(fpv_drone_create)
    FPV_FN(fpv_drone_delete)
    FPV_FN(fpv_drone_reset)
    FPV_FN(fpv_drone_repair)
    FPV_FN(fpv_drone_tick)
    FPV_FN(fpv_input_key)
    FPV_FN(fpv_input_get_status)
#undef FPV_FN
    if (!ok) return false;

    std::string data_dir = game_dir() + "\\fpv-data";
    fpv_global_config cfg;
    fpv.fpv_global_config_defaults(&cfg);
    cfg.data_dir = data_dir.c_str();
    // GTA IV: metres, X east, Y north, Z up.
    cfg.coords = {FPV_AXIS_POS_X, FPV_AXIS_POS_Z, FPV_AXIS_POS_Y, 1.0f};
    cfg.log = &on_fpv_log;
    cfg.log_level = FPV_LOG_INFO;
    fpv_result r = fpv.fpv_global_init(&cfg);
    if (r != FPV_OK && r != FPV_ERROR_ALREADY_INITIALIZED) {
        fpvlog::write("fpv_global_init: %s (%s)", fpv.fpv_result_string(r), fpv.fpv_last_error());
        return false;
    }
    fpvlog::write("libfpv %s initialised, data in %s", fpv.fpv_version_string(), data_dir.c_str());
    fpv_failed = false;
    fpv_loaded = true;
    return true;
}

enum class View { Fpv, Chase, Los, Count };

fpv_drone *g_drone = nullptr;
uint32_t g_cam = 0;
uint32_t g_player = 0;
View g_view = View::Fpv;
float g_spawn_pos[3];
float g_spawn_rot[4];
LARGE_INTEGER g_last_tick;

uint32_t player_index() {
    return sh::call<uint32_t>(N("CONVERT_INT_TO_PLAYERINDEX"), sh::call<uint32_t>(N("GET_PLAYER_ID")));
}

void print(const char *text, uint32_t ms = 4000) {
    sh::call(N("PRINT_STRING_WITH_LITERAL_STRING_NOW"), "STRING", text, ms, 1u);
}

bool game_has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Keyboard fallback for players without a radio (libfpv: W/S throttle, A/D yaw,
// arrows pitch/roll, Enter arms, T turtle, B beeper).
void forward_keys() {
    struct Key { int vk; int fpv; };
    static const Key keys[] = {
        {'W', 'W'}, {'A', 'A'}, {'S', 'S'}, {'D', 'D'}, {'T', 'T'}, {'B', 'B'},
        {VK_RETURN, FPV_KEY_ENTER}, {VK_LEFT, FPV_KEY_LEFT}, {VK_RIGHT, FPV_KEY_RIGHT},
        {VK_UP, FPV_KEY_UP}, {VK_DOWN, FPV_KEY_DOWN},
    };
    static bool was_down[sizeof(keys) / sizeof(keys[0])];
    bool focus = game_has_focus();
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        bool down = focus && (GetAsyncKeyState(keys[i].vk) & 0x8000) != 0;
        if (down != was_down[i]) fpv.fpv_input_key(keys[i].fpv, down);
        was_down[i] = down;
    }
}

bool key_pressed(int vk) {
    static bool was_down[256];
    bool down = game_has_focus() && (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !was_down[vk];
    was_down[vk] = down;
    return pressed;
}

// Game cameras take Euler degrees: x pitch, y roll, z heading (0 = north,
// counter-clockwise positive).
void apply_camera(const fpv_camera &c) {
    const float *f = c.forward, *u = c.up, *r = c.right;
    float heading = std::atan2(-f[0], f[1]) * kRadToDeg;
    float pitch = std::asin(f[2] < -1 ? -1 : f[2] > 1 ? 1 : f[2]) * kRadToDeg;
    float roll = std::atan2(-r[2], u[2]) * kRadToDeg;
    sh::call(N("SET_CAM_POS"), g_cam, c.position[0], c.position[1], c.position[2]);
    sh::call(N("SET_CAM_ROT"), g_cam, pitch, roll, heading);
    sh::call(N("SET_CAM_FOV"), g_cam, c.fov_y_degrees);
}

void start() {
    if (!load_libfpv()) {
        print("FPV: libfpv failed to load, see GTAIV.FPV.log");
        return;
    }
    g_player = player_index();
    uint32_t ped = 0;
    sh::call(N("GET_PLAYER_CHAR"), g_player, &ped);
    float x = 0, y = 0, z = 0, heading = 0;
    sh::call(N("GET_CHAR_COORDINATES"), ped, &x, &y, &z);
    sh::call(N("GET_CHAR_HEADING"), ped, &heading);

    // Two metres in front of Niko, resting on the ground.
    float h = heading / kRadToDeg;
    g_spawn_pos[0] = x - std::sin(h) * 2.0f;
    g_spawn_pos[1] = y + std::cos(h) * 2.0f;
    float ground = 0;
    sh::call(N("GET_GROUND_Z_FOR_3D_COORD"), g_spawn_pos[0], g_spawn_pos[1], z + 1.0f, &ground);
    g_spawn_pos[2] = (ground != 0 && ground < z + 1.0f) ? ground : z - 1.0f;
    g_spawn_rot[0] = 0;
    g_spawn_rot[1] = 0;
    g_spawn_rot[2] = std::sin(h / 2);
    g_spawn_rot[3] = std::cos(h / 2);

    fpv_drone_desc desc;
    fpv.fpv_drone_desc_defaults(&desc);
    for (int i = 0; i < 3; ++i) desc.position[i] = g_spawn_pos[i];
    for (int i = 0; i < 4; ++i) desc.rotation[i] = g_spawn_rot[i];
    desc.pilot_position[0] = x;
    desc.pilot_position[1] = y;
    desc.pilot_position[2] = z;
    fpv_result r = fpv.fpv_drone_create(&desc, &g_drone);
    if (r != FPV_OK) {
        fpvlog::write("fpv_drone_create: %s (%s)", fpv.fpv_result_string(r), fpv.fpv_last_error());
        print("FPV: drone creation failed, see GTAIV.FPV.log");
        g_drone = nullptr;
        return;
    }
    fpv.fpv_world_set_raycast_callback(&world::raycast, nullptr);

    sh::call(N("SET_PLAYER_CONTROL"), g_player, false);
    sh::call(N("CREATE_CAM"), 14u, &g_cam);
    sh::call(N("SET_CAM_ACTIVE"), g_cam, true);
    sh::call(N("SET_CAM_PROPAGATE"), g_cam, true);
    sh::call(N("ACTIVATE_SCRIPTED_CAMS"), true, true);
    QueryPerformanceCounter(&g_last_tick);
    g_view = View::Fpv;

    fpv_input_status st{};
    st.struct_size = sizeof st;
    fpv.fpv_input_get_status(&st);
    fpvlog::write("drone launched at %.2f %.2f %.2f heading %.1f, cam %u, input %d '%s'%s", g_spawn_pos[0],
                  g_spawn_pos[1], g_spawn_pos[2], heading, g_cam, int(st.source), st.device_name,
                  st.needs_setup ? " (needs setup)" : "");
    print(st.source == FPV_INPUT_SOURCE_RADIO
              ? "FPV: radio connected. C camera, R reset, F7 back to Niko"
              : "FPV: keyboard. Enter arm, W/S throttle, A/D yaw, arrows pitch/roll, C camera, R reset, F7 back",
          8000);
}

void stop() {
    if (g_cam) {
        sh::call(N("ACTIVATE_SCRIPTED_CAMS"), false, false);
        sh::call(N("SET_CAM_ACTIVE"), g_cam, false);
        sh::call(N("DESTROY_CAM"), g_cam);
        g_cam = 0;
    }
    sh::call(N("SET_PLAYER_CONTROL"), g_player, true);
    if (g_drone) {
        fpv.fpv_world_set_raycast_callback(nullptr, nullptr);
        fpv.fpv_drone_delete(g_drone);
        g_drone = nullptr;
    }
    fpvlog::write("drone stopped");
}

}  // namespace

bool active() { return g_drone != nullptr; }

void toggle() {
    if (active())
        stop();
    else
        start();
}

void tick() {
    if (!g_drone) return;

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = float(now.QuadPart - g_last_tick.QuadPart) / float(freq.QuadPart);
    g_last_tick = now;
    if (dt > 0.1f) dt = 0.1f;

    forward_keys();
    if (key_pressed('C')) g_view = View((int(g_view) + 1) % int(View::Count));
    if (key_pressed('R')) {
        fpv.fpv_drone_reset(g_drone, g_spawn_pos, g_spawn_rot);
        fpv.fpv_drone_repair(g_drone);
    }

    fpv_frame frame{};
    frame.struct_size = sizeof frame;
    fpv_result r = fpv.fpv_drone_tick(g_drone, dt, &frame);
    if (r != FPV_OK) {
        static int reported = 0;
        if (reported++ < 5) fpvlog::write("fpv_drone_tick: %s", fpv.fpv_result_string(r));
        return;
    }
    apply_camera(g_view == View::Fpv ? frame.camera : g_view == View::Chase ? frame.chase_camera : frame.los_camera);

    // Sparse trace for debugging the axes and the camera mapping.
    static float log_timer = 0;
    log_timer += dt;
    if (log_timer > 2.0f) {
        log_timer = 0;
        fpvlog::write("drone pos %.2f %.2f %.2f flags 0x%x alt %.2f thr %.2f arm '%s'", frame.position[0],
                      frame.position[1], frame.position[2], frame.flags, frame.altitude, frame.channels[2],
                      frame.arming_disable_text);
    }
}

}  // namespace drone
