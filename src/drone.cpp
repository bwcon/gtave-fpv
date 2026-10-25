#include "drone.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#include "audio.h"
#include "fpvapi.h"
#include "log.h"
#include "natives.h"
#include "radio.h"
#include "shv.h"
#include "world.h"

namespace drone {
namespace {

constexpr float kRadToDeg = 57.2957795f;
constexpr float kNearClip = 0.2f;      // with clear space in front of the lens
constexpr float kMinNearClip = 0.02f;  // when the lens almost touches something
constexpr float kWidestAspect = 2.4f;  // frustum probes cover up to 21:9
constexpr float kMaxFov = 130.0f;      // GTA V clamps camera FOV here
constexpr int kRotationOrder = 2;      // what game scripts pass to SET_CAM_ROT (see basis_to_euler)

using fpvapi::fpv;

enum class View { Fpv, Chase, Los, Count };
constexpr uint32_t kMovePlayerAction = 1;  // F8 > Fly button id

fpv_drone *g_drone = nullptr;
int g_cam = 0;
int g_player = 0;
int g_ped = 0;
View g_view = View::Fpv;
float g_spawn_pos[3];
float g_spawn_rot[4];
LARGE_INTEGER g_last_tick;
double g_first_sim_time = -1;  // sim clock at the first frame of this flight
fpv_frame g_frame{};
float g_near_clip = 0, g_fov = 0;  // last values sent to the game, for the trace

std::mutex g_status_mutex;
Status g_status;

std::mutex g_osd_mutex;  // guards g_osd_frame / g_osd_visible for the render thread
fpv_frame g_osd_frame{};
bool g_osd_visible = false;

// The visible quad: a game prop that follows libfpv's pose. No collision, so
// the world probes never hit it.
int g_prop = 0;
float g_prop_yaw[4] = {0, 0, 0, 1};  // model-to-drone yaw correction (GTAVE.FPV.ini)

// Candidate drone models, tried in order; the first one the game has wins
// (the log lists which exist). Override with [Drone] Model= in GTAVE.FPV.ini.
// Prop names from DurtyFree's gta-v-data-dumps ObjectList; the Terrorbyte
// drone comes first. "rcdrone" is not in that dump: maybe a newer vehicle.
const char *const kPropCandidates[] = {
    "ba_prop_battle_drone_quad", "rcdrone",                  "ch_prop_arcade_drone_01a",
    "ba_prop_battle_drone_hornet", "ch_prop_casino_drone_01a", "ch_prop_casino_drone_02a",
    "xs_prop_arena_drone_01",    "xs_prop_arena_drone_02",   "reh_prop_reh_drone_02a",
    "m24_2_prop_m42_drone_01a",  "m24_2_prop_m42_battle_drone_01a",
};

std::string ini_path() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path, n);
    return dir.substr(0, dir.find_last_of("\\/")) + "\\GTAVE.FPV.ini";
}

struct DroneSettings {
    std::string model;        // [Drone] Model: empty = the candidate closest to TargetSize
    float yaw_offset = 0;     // [Drone] YawOffset, degrees
    float target_size = 0.30f;  // [Drone] TargetSize, metres across with props (a 5" quad)
};

std::string ini_get(const std::string &ini, const char *key, const char *fallback) {
    char buf[128];
    GetPrivateProfileStringA("Drone", key, fallback, buf, sizeof buf, ini.c_str());
    return buf;
}

DroneSettings read_settings() {
    const std::string ini = ini_path();
    DroneSettings s;
    // Writes any missing key with its default, so the file documents itself.
    auto value = [&](const char *key, const char *fallback) {
        std::string v = ini_get(ini, key, "\x01");
        if (v == "\x01") {
            WritePrivateProfileStringA("Drone", key, fallback, ini.c_str());
            v = fallback;
        }
        return v;
    };
    s.model = value("Model", "");
    s.yaw_offset = float(std::atof(value("YawOffset", "0").c_str()));
    s.target_size = float(std::atof(value("TargetSize", "0.30").c_str()));
    return s;
}

// Largest horizontal extent of a model, metres.
float model_span(uint32_t hash) {
    shv::Vec3 min{}, max{};
    shv::call(nat::GET_MODEL_DIMENSIONS, hash, &min, &max);
    float x = max.x - min.x, y = max.y - min.y;
    return x > y ? x : y;
}

uint32_t pick_prop_model(const DroneSettings &s) {
    if (!s.model.empty()) {
        uint32_t hash = shv::call<uint32_t>(nat::GET_HASH_KEY, s.model.c_str());
        if (shv::call<bool>(nat::IS_MODEL_IN_CDIMAGE, hash)) return hash;
        fpvlog::write("drone model '%s' from GTAVE.FPV.ini is not in the game", s.model.c_str());
    }
    // GTA cannot scale an entity, so pick the model whose size fits best.
    uint32_t chosen = 0;
    float best = 1e9f;
    for (const char *name : kPropCandidates) {
        uint32_t hash = shv::call<uint32_t>(nat::GET_HASH_KEY, name);
        if (!shv::call<bool>(nat::IS_MODEL_IN_CDIMAGE, hash)) {
            fpvlog::write("drone model candidate %s: missing", name);
            continue;
        }
        float span = model_span(hash), miss = std::fabs(span - s.target_size);
        fpvlog::write("drone model candidate %s: %.2f m across", name, span);
        if (miss < best) best = miss, chosen = hash;
    }
    return chosen;
}

void spawn_prop(const float pos[3]) {
    const DroneSettings s = read_settings();
    const float yaw_offset = s.yaw_offset;
    float half = yaw_offset * 0.5f / kRadToDeg;
    g_prop_yaw[0] = 0, g_prop_yaw[1] = 0, g_prop_yaw[2] = std::sin(half), g_prop_yaw[3] = std::cos(half);

    uint32_t model = pick_prop_model(s);
    if (!model) {
        fpvlog::write("no drone model found: the drone flies invisible");
        return;
    }
    shv::call(nat::REQUEST_MODEL, model);
    for (int i = 0; i < 200 && !shv::call<bool>(nat::HAS_MODEL_LOADED, model); ++i) shv::wait(0);
    if (!shv::call<bool>(nat::HAS_MODEL_LOADED, model)) {
        fpvlog::write("drone model %08x did not load", model);
        return;
    }
    // Vehicle models (RC vehicles) cannot be spawned as objects.
    const bool vehicle = shv::call<bool>(nat::IS_MODEL_A_VEHICLE, model);
    g_prop = vehicle ? shv::call<int>(nat::CREATE_VEHICLE, model, pos[0], pos[1], pos[2], 0.0f, false, false, false)
                     : shv::call<int>(nat::CREATE_OBJECT_NO_OFFSET, model, pos[0], pos[1], pos[2], false, false, false, 0);
    shv::call(nat::SET_MODEL_AS_NO_LONGER_NEEDED, model);
    if (!g_prop) return;
    shv::call(nat::SET_ENTITY_COLLISION, g_prop, false, false);
    shv::call(nat::FREEZE_ENTITY_POSITION, g_prop, true);  // libfpv moves it, not GTA's physics
    shv::call(nat::SET_ENTITY_INVINCIBLE, g_prop, true, false);
    if (vehicle) shv::call(nat::SET_VEHICLE_ENGINE_ON, g_prop, false, true, true);
    fpvlog::write("drone model %08x spawned as %s %d (yaw offset %.0f)", model, vehicle ? "vehicle" : "object",
                  g_prop, yaw_offset);
}

void delete_prop() {
    if (!g_prop) return;
    if (shv::call<bool>(nat::DOES_ENTITY_EXIST, g_prop)) {
        shv::call(nat::SET_ENTITY_AS_MISSION_ENTITY, g_prop, true, true);
        shv::call(nat::DELETE_ENTITY, &g_prop);  // objects and vehicles alike
    }
    g_prop = 0;
}

// q * r for {x, y, z, w} quaternions.
void quat_mul(const float q[4], const float r[4], float out[4]) {
    out[0] = q[3] * r[0] + q[0] * r[3] + q[1] * r[2] - q[2] * r[1];
    out[1] = q[3] * r[1] - q[0] * r[2] + q[1] * r[3] + q[2] * r[0];
    out[2] = q[3] * r[2] + q[0] * r[1] - q[1] * r[0] + q[2] * r[3];
    out[3] = q[3] * r[3] - q[0] * r[0] - q[1] * r[1] - q[2] * r[2];
}

// Hidden in the FPV view, where the camera sits inside it.
void update_prop(const fpv_frame &frame, bool fpv_view) {
    if (!g_prop) return;
    float q[4];
    quat_mul(frame.rotation, g_prop_yaw, q);
    shv::call(nat::SET_ENTITY_COORDS_NO_OFFSET, g_prop, frame.position[0], frame.position[1], frame.position[2],
              false, false, false);
    shv::call(nat::SET_ENTITY_QUATERNION, g_prop, q[0], q[1], q[2], q[3]);
    shv::call(nat::SET_ENTITY_VISIBLE, g_prop, !fpv_view, false);
}

void publish_osd(const fpv_frame *frame, bool visible) {
    std::lock_guard<std::mutex> lock(g_osd_mutex);
    if (frame) g_osd_frame = *frame;
    g_osd_visible = visible;
}

bool game_has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Keyboard fallback for players without a radio (libfpv: W/S throttle, A/D yaw,
// arrows pitch/roll, Enter arms, T turtle, B beeper). The overlay's own keys
// come from the ReShade tab (tab.cpp), so F8 is not forwarded here.
void forward_keys() {
    // letters by scan code (physical position, as libfpv wants)
    struct Key { int vk; int fpv; UINT sc; };
    static const Key keys[] = {
        {0, 'W', 0x11}, {0, 'A', 0x1E}, {0, 'S', 0x1F}, {0, 'D', 0x20}, {0, 'T', 0x14}, {0, 'B', 0x30},
        {VK_RETURN, FPV_KEY_ENTER, 0}, {VK_LEFT, FPV_KEY_LEFT, 0}, {VK_RIGHT, FPV_KEY_RIGHT, 0},
        {VK_UP, FPV_KEY_UP, 0}, {VK_DOWN, FPV_KEY_DOWN, 0},
    };
    static bool was_down[sizeof(keys) / sizeof(keys[0])];
    static bool primed = false;  // a key already held when we start is not a press
    bool focus = game_has_focus();
    // the layout is per thread: take the game window's, not this script fiber's
    HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        int vk = keys[i].sc ? (int)MapVirtualKeyExW(keys[i].sc, MAPVK_VSC_TO_VK, layout) : keys[i].vk;
        bool down = focus && vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (primed && down != was_down[i]) fpv.fpv_input_key(keys[i].fpv, down);
        was_down[i] = down;
    }
    primed = true;
}

// Edge-detected key press. The first reading of each key is only a baseline,
// so a key that is already held (or stuck) never counts as a press.
bool key_pressed(int vk) {
    static bool was_down[256], seen[256];
    bool down = game_has_focus() && (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = seen[vk] && down && !was_down[vk];
    was_down[vk] = down;
    seen[vk] = true;
    return pressed;
}

// The FPV camera rides a few cm above the road: a fixed near plane cuts the
// ground away under the lens. Probe the view frustum (centre, edge midpoints
// and corners) and pull the near plane in front of the closest geometry.
float near_clip_for(const fpv_camera &c, float fov_y) {
    const float ty = std::tan(fov_y * 0.5f / kRadToDeg), tx = ty * kWidestAspect;
    float nearest = kNearClip / 0.8f;  // depth along the view axis
    for (int sy = -1; sy <= 1; ++sy)
        for (int sx = -1; sx <= 1; ++sx) {
            float d[3], len = 0;
            for (int i = 0; i < 3; ++i) {
                d[i] = c.forward[i] + c.right[i] * tx * float(sx) + c.up[i] * ty * float(sy);
                len += d[i] * d[i];
            }
            len = std::sqrt(len);
            for (float &v : d) v /= len;
            fpv_raycast_hit hit{};
            // depth = distance * cos(angle to the axis) = distance / len
            if (world::raycast(nullptr, c.position, d, nearest * len, &hit) && hit.distance / len < nearest)
                nearest = hit.distance / len;
        }
    float n = nearest * 0.8f;
    return n < kMinNearClip ? kMinNearClip : n > kNearClip ? kNearClip : n;
}

// ponytail: plain rectilinear FOV until the fisheye lens pass (Phase 3) exists.
void apply_camera(const fpv_camera &c, bool fpv_view) {
    float pitch, roll, heading;
    basis_to_euler(c.forward, c.up, c.right, &pitch, &roll, &heading);
    shv::call(nat::SET_CAM_COORD, g_cam, c.position[0], c.position[1], c.position[2]);
    shv::call(nat::SET_CAM_ROT, g_cam, pitch, roll, heading, kRotationOrder);
    float fov = c.fov_y_degrees > kMaxFov ? kMaxFov : c.fov_y_degrees;
    shv::call(nat::SET_CAM_FOV, g_cam, fov);
    float near_clip = fpv_view ? near_clip_for(c, fov) : kNearClip;
    shv::call(nat::SET_CAM_NEAR_CLIP, g_cam, near_clip);
    g_near_clip = near_clip;
    g_fov = fov;
}

shv::Vec3 ped_position() { return shv::call<shv::Vec3>(nat::GET_ENTITY_COORDS, g_ped, true); }

void hold_player(bool hold) {
    shv::call(nat::SET_PLAYER_CONTROL, g_player, !hold, 0);
    shv::call(nat::FREEZE_ENTITY_POSITION, g_ped, hold);
    shv::call(nat::SET_ENTITY_INVINCIBLE, g_ped, hold, false);
}

void publish_status(bool flying) {
    fpv_input_status st{};
    st.struct_size = sizeof st;
    fpv.fpv_input_get_status(&st);
    std::lock_guard<std::mutex> lock(g_status_mutex);
    g_status.loaded = true;
    g_status.flying = flying;
    g_status.needs_setup = st.needs_setup != 0;
    std::snprintf(g_status.device, sizeof g_status.device, "%s", st.device_name);
    if (!flying) return;
    for (int i = 0; i < 3; ++i) g_status.position[i] = g_frame.position[i];
    g_status.speed_kmh = g_frame.speed_kmh;
    g_status.fov = g_fov;
    g_status.near_clip = g_near_clip;
    std::snprintf(g_status.arming, sizeof g_status.arming, "%s", g_frame.arming_disable_text);
}

void start() {
    if (!fpvapi::load()) {
        fpvlog::write("libfpv failed to load, no flight");
        return;
    }
    g_player = shv::call<int>(nat::PLAYER_ID);
    g_ped = shv::call<int>(nat::PLAYER_PED_ID);
    shv::Vec3 p = ped_position();
    float heading = shv::call<float>(nat::GET_ENTITY_HEADING, g_ped);

    // Two metres in front of the player, resting on whatever is there.
    world::ignore_entity(g_ped);
    float h = heading / kRadToDeg;
    g_spawn_pos[0] = p.x - std::sin(h) * 2.0f;
    g_spawn_pos[1] = p.y + std::cos(h) * 2.0f;
    float origin[3] = {g_spawn_pos[0], g_spawn_pos[1], p.z + 1.0f}, down[3] = {0, 0, -1};
    fpv_raycast_hit hit{};
    g_spawn_pos[2] = world::raycast(nullptr, origin, down, 5.0f, &hit) ? origin[2] - hit.distance : p.z - 1.0f;
    g_spawn_rot[0] = 0;
    g_spawn_rot[1] = 0;
    g_spawn_rot[2] = std::sin(h / 2);
    g_spawn_rot[3] = std::cos(h / 2);

    fpv_drone_desc desc;
    fpv.fpv_drone_desc_defaults(&desc);
    for (int i = 0; i < 3; ++i) desc.position[i] = g_spawn_pos[i];
    for (int i = 0; i < 4; ++i) desc.rotation[i] = g_spawn_rot[i];
    desc.pilot_position[0] = p.x;
    desc.pilot_position[1] = p.y;
    desc.pilot_position[2] = p.z;
    fpv_result r = fpv.fpv_drone_create(&desc, &g_drone);
    if (r != FPV_OK) {
        fpvlog::write("fpv_drone_create: %s (%s)", fpv.fpv_result_string(r), fpv.fpv_last_error());
        g_drone = nullptr;
        world::ignore_entity(0);
        return;
    }
    fpv.fpv_world_set_raycast_callback(&world::raycast, nullptr);
    float home[3] = {p.x, p.y, p.z};
    fpv.fpv_gps_set_home(home);
    spawn_prop(g_spawn_pos);

    hold_player(true);
    g_cam = shv::call<int>(nat::CREATE_CAM, "DEFAULT_SCRIPTED_CAMERA", false);
    shv::call(nat::SET_CAM_ACTIVE, g_cam, true);
    shv::call(nat::RENDER_SCRIPT_CAMS, true, false, 0, true, false, 0);
    QueryPerformanceCounter(&g_last_tick);
    g_first_sim_time = -1;
    g_view = View::Fpv;
    audio::start();
    radio::start();
    fpv.fpv_overlay_set_action(kMovePlayerAction, "Move player here (Y)");

    fpv_input_status st{};
    st.struct_size = sizeof st;
    fpv.fpv_input_get_status(&st);
    fpvlog::write("drone launched at %.2f %.2f %.2f heading %.1f, cam %d, input %d '%s'%s", g_spawn_pos[0],
                  g_spawn_pos[1], g_spawn_pos[2], heading, g_cam, int(st.source), st.device_name,
                  st.needs_setup ? " (needs setup)" : "");
}

void stop() {
    fpv.fpv_overlay_set_action(kMovePlayerAction, nullptr);
    audio::stop();  // before the drone: the audio thread reads libfpv's synth
    radio::stop();
    if (g_cam) {
        shv::call(nat::RENDER_SCRIPT_CAMS, false, false, 0, true, false, 0);
        shv::call(nat::SET_CAM_ACTIVE, g_cam, false);
        shv::call(nat::DESTROY_CAM, g_cam, false);
        g_cam = 0;
    }
    shv::call(nat::CLEAR_FOCUS);
    delete_prop();
    publish_osd(nullptr, false);
    hold_player(false);
    if (g_drone) {
        fpv.fpv_world_set_raycast_callback(nullptr, nullptr);
        world::ignore_entity(0);
        fpv.fpv_drone_delete(g_drone);
        g_drone = nullptr;
    }
    publish_status(false);
    fpvlog::write("drone stopped");
}

// A new drone at the launch spot (R).
void respawn() {
    fpv.fpv_drone_repair(g_drone);
    fpv.fpv_drone_reset(g_drone, g_spawn_pos, g_spawn_rot);
    g_first_sim_time = -1;
}

}  // namespace

// SET_CAM_ROT with order 2 builds the camera as Rz(yaw) Rx(pitch) Ry(roll) in
// GTA's frame (X right, Y forward, Z up): roll about the view axis first, then
// pitch, then heading. Its bottom row is (-cos p sin r, sin p, cos p cos r)
// across right, forward, up, and forward is (-sin yaw cos p, cos yaw cos p, sin p).
// ponytail: derived, not measured. If a banked drone tilts the horizon the
// wrong way in game, swap the roll sign or try order 5 here.
void basis_to_euler(const float f[3], const float u[3], const float r[3], float *pitch, float *roll, float *heading) {
    float fz = f[2] < -1 ? -1 : f[2] > 1 ? 1 : f[2];
    *pitch = std::asin(fz) * kRadToDeg;
    if (std::fabs(fz) < 0.9999f) {
        *roll = std::atan2(-r[2], u[2]) * kRadToDeg;
        *heading = std::atan2(-f[0], f[1]) * kRadToDeg;
    } else {  // straight up or down: heading and roll share an axis, put it all in heading
        *roll = 0;
        *heading = std::atan2(r[1], r[0]) * kRadToDeg;
    }
}

bool active() { return g_drone != nullptr; }

Status status() {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    return g_status;
}

bool osd_frame(fpv_frame *out) {
    std::lock_guard<std::mutex> lock(g_osd_mutex);
    if (!g_osd_visible) return false;
    *out = g_osd_frame;
    return true;
}

// The player faces the drone's heading.
void move_player_here() {
    float pos[3] = {g_frame.position[0], g_frame.position[1], g_frame.position[2]};
    const float *f = g_frame.camera.forward;
    float heading = (f[0] * f[0] + f[1] * f[1] > 1e-6f) ? std::atan2(-f[0], f[1]) * kRadToDeg : 0;
    // Stand on whatever is under the drone (roof, road, floor); the probe
    // already ignores the player. Fall back to the drone's own height.
    float origin[3] = {pos[0], pos[1], pos[2] + 0.5f}, down[3] = {0, 0, -1};
    fpv_raycast_hit hit{};
    bool ground = world::raycast(nullptr, origin, down, 300.0f, &hit) != 0;
    float z = ground ? origin[2] - hit.distance : pos[2];
    stop();
    shv::call(nat::SET_ENTITY_COORDS, g_ped, pos[0], pos[1], z + 1.0f, false, false, false, false);
    shv::call(nat::SET_ENTITY_HEADING, g_ped, heading);
    shv::call(nat::SET_GAMEPLAY_CAM_RELATIVE_HEADING, 0.0f);
    fpvlog::write("moved the player to the drone: %.2f %.2f %.2f (ground %s) heading %.1f", pos[0], pos[1], z,
                  ground ? "hit" : "missed", heading);
}

void toggle() {
    if (active())
        stop();
    else
        start();
}

void tick() {
    if (!fpvapi::loaded()) return;
    if (!fpv.fpv_overlay_is_open()) forward_keys();  // otherwise the keys are typing in the ReShade tab
    for (uint32_t id; (id = fpv.fpv_overlay_poll_action()) != 0;) {
        if (id == kMovePlayerAction && g_drone) {
            move_player_here();
            return;
        }
    }
    if (!g_drone) {
        publish_status(false);
        return;
    }

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = float(now.QuadPart - g_last_tick.QuadPart) / float(freq.QuadPart);
    g_last_tick = now;
    if (dt > 0.1f) dt = 0.1f;

    // The HUD would cover the FPV feed, and the game must not read the flight
    // keys (or a gamepad) as walking, the phone or weapons. Esc still pauses.
    shv::call(nat::HIDE_HUD_AND_RADAR_THIS_FRAME);
    shv::call(nat::DISABLE_ALL_CONTROL_ACTIONS, 0);
    shv::call(nat::ENABLE_CONTROL_ACTION, 0, nat::INPUT_FRONTEND_PAUSE, true);
    shv::call(nat::ENABLE_CONTROL_ACTION, 0, nat::INPUT_FRONTEND_PAUSE_ALTERNATE, true);

    if (shv::call<bool>(nat::IS_ENTITY_DEAD, g_ped, false)) {
        fpvlog::write("player died, drone stopped");
        stop();
        return;
    }
    radio::tick(!fpv.fpv_overlay_wants_input());
    if (!fpv.fpv_overlay_wants_input()) {  // otherwise the keys belong to the overlay
        if (key_pressed('C')) g_view = View((int(g_view) + 1) % int(View::Count));
        if (key_pressed('R')) respawn();
        if (key_pressed('O')) fpvapi::toggle_osd_style();
        if (key_pressed('Y')) {
            move_player_here();
            return;
        }
    }

    fpv_frame frame{};
    frame.struct_size = sizeof frame;
    fpv_result r = fpv.fpv_drone_tick(g_drone, dt, &frame);
    if (r != FPV_OK) {
        static int reported = 0;
        if (reported++ < 5) fpvlog::write("fpv_drone_tick: %s", fpv.fpv_result_string(r));
        return;
    }
    // Until libfpv's sim thread has stepped the new drone, the frame shows a
    // stale snapshot and interpolates from it: the camera would jump there.
    // libfpv's clock is not reset per drone, so wait 50 ms past its first reading.
    if (g_first_sim_time < 0) g_first_sim_time = frame.sim_time;
    if (frame.sim_time < g_first_sim_time + 0.05) return;
    g_frame = frame;

    // Stream map detail, collision and traffic around the drone, not the
    // player standing at the launch spot.
    shv::call(nat::SET_FOCUS_POS_AND_VEL, frame.position[0], frame.position[1], frame.position[2],
              frame.velocity[0], frame.velocity[1], frame.velocity[2]);
    shv::Vec3 p = ped_position();
    float home[3] = {p.x, p.y, p.z};
    fpv.fpv_gps_set_home(home);

    const bool fpv_view = g_view == View::Fpv;
    apply_camera(fpv_view ? frame.camera : g_view == View::Chase ? frame.chase_camera : frame.los_camera, fpv_view);
    update_prop(frame, fpv_view);
    publish_osd(&frame, fpv_view && !(frame.flags & FPV_DRONE_OSD_DISABLED));
    publish_status(true);

    for (uint32_t i = 0; i < frame.impact_count && i < FPV_MAX_IMPACTS; ++i) {
        const fpv_impact &im = frame.impacts[i];
        if (im.speed < 1.0f) continue;  // resting contact
        fpvlog::write("impact at %.2f %.2f %.2f n %.2f %.2f %.2f speed %.1f impulse %.2f motor %d object %08x",
                      im.position[0], im.position[1], im.position[2], im.normal[0], im.normal[1], im.normal[2], im.speed,
                      im.impulse, int(im.motor_index), im.object_id);
    }

    // Sparse trace for debugging the axes and the camera mapping.
    static float log_timer = 0;
    log_timer += dt;
    if (log_timer > 1.0f) {
        log_timer = 0;
        float pitch, roll, heading;
        basis_to_euler(frame.camera.forward, frame.camera.up, frame.camera.right, &pitch, &roll, &heading);
        fpvlog::write("drone pos %.2f %.2f %.2f rot p %.1f r %.1f h %.1f flags 0x%x alt %.2f thr %.2f fov %.1f near %.3f "
                      "arm '%s'",
                      frame.position[0], frame.position[1], frame.position[2], pitch, roll, heading, frame.flags,
                      frame.altitude, frame.channels[2], g_fov, g_near_clip, frame.arming_disable_text);
    }
}

}  // namespace drone
