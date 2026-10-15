#include "drone.h"

#include <windows.h>

#include <cmath>
#include <mutex>
#include "audio.h"
#include "effects.h"
#include "fpvapi.h"
#include "log.h"
#include "render.h"
#include "scripthook.h"
#include "world.h"

namespace drone {
namespace {

constexpr float kRadToDeg = 57.2957795f;
constexpr float kNearClip = 0.2f;      // with clear space in front of the lens
constexpr float kMinNearClip = 0.02f;  // when the lens almost touches something
constexpr float kWidestAspect = 2.4f;  // frustum probes cover up to 21:9
constexpr float kAnchorStart = 80.0f;  // drone this far from Niko: he starts following it
constexpr float kAnchorStop = 40.0f;   // back this close to his spot: he goes home
constexpr float kAnchorStep = 10.0f;   // re-place him once the drone is this far from him

using fpvapi::fpv;

enum class View { Fpv, Chase, Los, Count };
constexpr uint32_t kMoveNikoAction = 1;  // F8 > Fly button id

fpv_drone *g_drone = nullptr;
uint32_t g_cam = 0;
uint32_t g_player = 0;
View g_view = View::Fpv;
float g_spawn_pos[3];
float g_spawn_rot[4];
LARGE_INTEGER g_last_tick;
double g_first_sim_time = -1;  // sim clock at the first frame of this flight
fpv_frame g_frame{};
float g_near_clip = 0, g_fov = 0;  // last values sent to the game, for the trace
std::mutex g_osd_mutex;  // guards g_osd_frame / g_osd_visible for the render thread
fpv_frame g_osd_frame{};
bool g_osd_visible = false;

// The game streams map detail, LODs, collision and traffic around the player,
// so far from Niko the drone would fly through a low-detail, empty city with
// no collision. Far away, Niko becomes a hidden, frozen streaming anchor that
// rides along with the drone; he goes back to his spot (and car) afterwards.
struct Anchor {
    bool on = false;
    uint32_t ped = 0;
    uint32_t car = 0;  // the car he was in, 0 on foot
    float home[3];
    float heading;
    float at[3];  // where he was last placed
};
Anchor g_anchor;

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
// arrows pitch/roll, Enter arms, T turtle, B beeper) and the F8 overlay's keys.
void forward_keys() {
    struct Key { int vk; int fpv; };
    static const Key keys[] = {
        {'W', 'W'}, {'A', 'A'}, {'S', 'S'}, {'D', 'D'}, {'T', 'T'}, {'B', 'B'},
        {VK_RETURN, FPV_KEY_ENTER}, {VK_LEFT, FPV_KEY_LEFT}, {VK_RIGHT, FPV_KEY_RIGHT},
        {VK_UP, FPV_KEY_UP}, {VK_DOWN, FPV_KEY_DOWN}, {VK_F8, FPV_KEY_F8}, {VK_ESCAPE, FPV_KEY_ESCAPE},
        {VK_TAB, FPV_KEY_TAB}, {VK_BACK, FPV_KEY_BACKSPACE}, {VK_SPACE, FPV_KEY_SPACE},
    };
    static bool was_down[sizeof(keys) / sizeof(keys[0])];
    static bool primed = false;  // a key already held when we start is not a press
    bool focus = game_has_focus();
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        bool down = focus && (GetAsyncKeyState(keys[i].vk) & 0x8000) != 0;
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

// Game cameras take Euler degrees: x pitch, y roll, z heading (0 = north,
// counter-clockwise positive).
// The FPV camera rides a few cm above the road: a fixed near plane cuts the
// ground away under the lens and the game shows whatever was left in that
// part of the frame. Probe the view frustum (centre, edge midpoints and
// corners) and pull the near plane in front of the closest geometry.
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

// libfpv widens the FOV for its fisheye so the top and bottom edges of the
// distorted image show the FOV the player chose, but for the camera's
// aspect_hint (4:3 analog). Redo it for the game's real aspect.
float lens_edge(const fpv_camera &c, float aspect) {
    float r2 = 1 / (1 + aspect * aspect);  // squared radius of the top edge centre
    float zoom = c.distortion_zoom > 0 ? c.distortion_zoom : 1;
    return (1 + c.distortion_k1 * r2 + c.distortion_k2 * r2 * r2) / zoom;
}
float lens_fov(const fpv_camera &c) {
    if ((c.distortion_k1 == 0 && c.distortion_k2 == 0) || c.aspect_hint <= 0) return c.fov_y_degrees;
    float chosen = std::tan(c.fov_y_degrees * 0.5f / kRadToDeg) * lens_edge(c, c.aspect_hint);
    float fov = 2 * std::atan(chosen / lens_edge(c, render::aspect())) * kRadToDeg;
    return fov > 170 ? 170 : fov;
}

void apply_camera(const fpv_camera &c, bool fpv_view) {
    float pitch, roll, heading;
    basis_to_euler(c.forward, c.up, c.right, &pitch, &roll, &heading);
    sh::call(N("SET_CAM_POS"), g_cam, c.position[0], c.position[1], c.position[2]);
    sh::call(N("SET_CAM_ROT"), g_cam, pitch, roll, heading);
    float fov = fpv_view && tuning().fov > 0 ? tuning().fov : fpv_view ? lens_fov(c) : c.fov_y_degrees;
    sh::call(N("SET_CAM_FOV"), g_cam, fov);
    float near_clip = tuning().near_clip > 0 ? tuning().near_clip : fpv_view ? near_clip_for(c, fov) : kNearClip;
    sh::call(N("SET_CAM_NEAR_CLIP"), g_cam, near_clip);
    g_near_clip = near_clip;
    g_fov = fov;
}

float dist(const float a[3], const float b[3]) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Hidden, frozen, untouchable: nothing should notice a man floating with the drone.
void set_anchor_state(uint32_t ped, bool anchored) {
    sh::call(N("FREEZE_CHAR_POSITION"), ped, anchored);
    sh::call(N("SET_CHAR_VISIBLE"), ped, !anchored);
    sh::call(N("SET_CHAR_COLLISION"), ped, !anchored);
    sh::call(N("SET_CHAR_INVINCIBLE"), ped, anchored);
    sh::call(N("SET_EVERYONE_IGNORE_PLAYER"), g_player, anchored);
    sh::call(N("SET_POLICE_IGNORE_PLAYER"), g_player, anchored);
}

void place_anchor(const float pos[3]) {
    sh::call(N("SET_CHAR_COORDINATES"), g_anchor.ped, pos[0], pos[1], pos[2]);
    for (int i = 0; i < 3; ++i) g_anchor.at[i] = pos[i];
}

void anchor_begin(const float pos[3]) {
    uint32_t ped = 0;
    sh::call(N("GET_PLAYER_CHAR"), g_player, &ped);
    g_anchor = Anchor{};
    g_anchor.ped = ped;
    sh::call(N("GET_CHAR_COORDINATES"), ped, &g_anchor.home[0], &g_anchor.home[1], &g_anchor.home[2]);
    sh::call(N("GET_CHAR_HEADING"), ped, &g_anchor.heading);
    if (sh::call<bool>(N("IS_CHAR_IN_ANY_CAR"), ped)) {
        sh::call(N("GET_CAR_CHAR_IS_USING"), ped, &g_anchor.car);
        sh::call(N("WARP_CHAR_FROM_CAR_TO_COORD"), ped, pos[0], pos[1], pos[2]);
    }
    set_anchor_state(ped, true);
    place_anchor(pos);
    g_anchor.on = true;
    fpvlog::write("anchor: Niko follows the drone from %.2f %.2f %.2f (car %u)", g_anchor.home[0], g_anchor.home[1],
                  g_anchor.home[2], g_anchor.car);
}

// Puts Niko back where the flight left him: his car if it still exists,
// otherwise his spot. go_home false leaves him where he is (Move Niko here).
void anchor_end(bool go_home) {
    if (!g_anchor.on) return;
    g_anchor.on = false;
    uint32_t ped = g_anchor.ped;
    set_anchor_state(ped, false);
    if (!go_home) return;
    const float *h = g_anchor.home;
    sh::call(N("LOAD_SCENE"), h[0], h[1], h[2]);
    if (g_anchor.car && sh::call<bool>(N("DOES_VEHICLE_EXIST"), g_anchor.car)) {
        sh::call(N("WARP_CHAR_INTO_CAR"), ped, g_anchor.car);
        fpvlog::write("anchor: Niko back in car %u", g_anchor.car);
    } else {
        sh::call(N("SET_CHAR_COORDINATES"), ped, h[0], h[1], h[2]);
        sh::call(N("SET_CHAR_HEADING"), ped, g_anchor.heading);
        fpvlog::write("anchor: Niko back at %.2f %.2f %.2f%s", h[0], h[1], h[2],
                      g_anchor.car ? " (his car is gone)" : "");
    }
}

void update_anchor(const float drone_pos[3]) {
    if (!g_anchor.on) {
        if (dist(drone_pos, g_spawn_pos) > kAnchorStart) anchor_begin(drone_pos);
    } else if (dist(drone_pos, g_anchor.home) < kAnchorStop) {
        anchor_end(true);
    } else if (dist(drone_pos, g_anchor.at) > kAnchorStep) {
        place_anchor(drone_pos);
    }
}

// Betaflight's home arrow, home distance and GPS Rescue lead back to Niko,
// even if he was pushed or his car rolled away. While he rides along as the
// streaming anchor, home is the spot he goes back to.
void update_home() {
    float home[3];
    if (g_anchor.on) {
        for (int i = 0; i < 3; ++i) home[i] = g_anchor.home[i];
    } else {
        uint32_t ped = 0;
        sh::call(N("GET_PLAYER_CHAR"), g_player, &ped);
        sh::call(N("GET_CHAR_COORDINATES"), ped, &home[0], &home[1], &home[2]);
    }
    fpv.fpv_gps_set_home(home);
}

// The pilot's OSD disable switch asks for nothing on top of the video. The
// HUD and radar are already hidden in flight; help boxes and subtitles are
// not. Subtitles have no per-frame hide, so the ones printed meanwhile are dropped.
void hide_game_text() {
    sh::call(N("HIDE_HELP_TEXT_THIS_FRAME"));
    sh::call(N("CLEAR_PRINTS"));
}

void start() {
    if (!fpvapi::load()) {
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
    world::ignore_ped(ped);
    fpv.fpv_world_set_raycast_callback(&world::raycast, nullptr);
    float home[3] = {x, y, z};
    fpv.fpv_gps_set_home(home);

    sh::call(N("SET_PLAYER_CONTROL"), g_player, false);
    sh::call(N("CREATE_CAM"), 14u, &g_cam);
    sh::call(N("SET_CAM_ACTIVE"), g_cam, true);
    sh::call(N("SET_CAM_PROPAGATE"), g_cam, true);
    sh::call(N("ACTIVATE_SCRIPTED_CAMS"), true, true);
    QueryPerformanceCounter(&g_last_tick);
    g_first_sim_time = -1;
    g_view = View::Fpv;
    audio::start();
    fpv.fpv_overlay_set_action(kMoveNikoAction, "Move Niko here (N)");

    fpv_input_status st{};
    st.struct_size = sizeof st;
    fpv.fpv_input_get_status(&st);
    fpvlog::write("drone launched at %.2f %.2f %.2f heading %.1f, cam %u, input %d '%s'%s", g_spawn_pos[0],
                  g_spawn_pos[1], g_spawn_pos[2], heading, g_cam, int(st.source), st.device_name,
                  st.needs_setup ? " (needs setup)" : "");
}

void stop(bool go_home = true) {
    fpv.fpv_overlay_set_action(kMoveNikoAction, nullptr);
    audio::stop();  // before the drone: the audio thread reads libfpv's synth
    if (g_cam) {
        sh::call(N("ACTIVATE_SCRIPTED_CAMS"), false, false);
        sh::call(N("SET_CAM_ACTIVE"), g_cam, false);
        sh::call(N("DESTROY_CAM"), g_cam);
        g_cam = 0;
    }
    anchor_end(go_home);
    sh::call(N("SET_PLAYER_CONTROL"), g_player, true);
    if (g_drone) {
        fpv.fpv_world_set_raycast_callback(nullptr, nullptr);
        world::ignore_ped(0);
        fpv.fpv_drone_delete(g_drone);
        g_drone = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_osd_mutex);
        g_osd_visible = false;
    }
    fpvlog::write("drone stopped");
}

}  // namespace

Tuning &tuning() {
    static Tuning t;
    return t;
}

// The game builds a camera as Rz(heading) Ry(roll) Rx(pitch) (measured with
// the autotest's cam test), so the bottom row of that matrix is
// (-sin roll, cos roll sin pitch, cos roll cos pitch) across right, forward, up.
void basis_to_euler(const float f[3], const float u[3], const float r[3], float *pitch, float *roll, float *heading) {
    float rz = r[2] < -1 ? -1 : r[2] > 1 ? 1 : r[2];
    *roll = std::asin(-rz) * kRadToDeg;
    if (std::fabs(rz) < 0.9999f) {
        *pitch = std::atan2(f[2], u[2]) * kRadToDeg;
        *heading = std::atan2(r[1], r[0]) * kRadToDeg;
    } else {  // knife edge: pitch and heading share an axis, put it all in heading
        *pitch = 0;
        *heading = std::atan2(-f[0], f[1]) * kRadToDeg;
    }
}

void inject_channels(const float channels[FPV_CHANNEL_COUNT]) {
    if (fpvapi::loaded()) fpv.fpv_input_set_channels(channels, FPV_CHANNEL_COUNT);
}

bool active() { return g_drone != nullptr; }

// Niko faces the drone's heading.
void move_niko_here() {
    float pos[3] = {g_frame.position[0], g_frame.position[1], g_frame.position[2]};
    const float *f = g_frame.camera.forward;
    float heading = (f[0] * f[0] + f[1] * f[1] > 1e-6f) ? std::atan2(-f[0], f[1]) * kRadToDeg : 0;
    // Stand on whatever is under the drone (roof, road, floor); the world
    // probe already ignores Niko. Fall back to the drone's own height.
    float origin[3] = {pos[0], pos[1], pos[2] + 0.5f}, down[3] = {0, 0, -1};
    fpv_raycast_hit hit{};
    bool ground = world::raycast(nullptr, origin, down, 300.0f, &hit) != 0;
    float z = ground ? origin[2] - hit.distance : pos[2];
    stop(false);
    uint32_t ped = 0;
    sh::call(N("GET_PLAYER_CHAR"), g_player, &ped);
    sh::call(N("LOAD_SCENE"), pos[0], pos[1], z);  // the anchor may lag behind, and near clips stream little
    if (sh::call<bool>(N("IS_CHAR_IN_ANY_CAR"), ped))
        sh::call(N("WARP_CHAR_FROM_CAR_TO_COORD"), ped, pos[0], pos[1], z + 1.0f);
    else
        sh::call(N("SET_CHAR_COORDINATES"), ped, pos[0], pos[1], z + 1.0f);
    sh::call(N("SET_CHAR_HEADING"), ped, heading);
    sh::call(N("SET_CAM_BEHIND_PED"), ped);
    fpvlog::write("moved Niko to the drone: %.2f %.2f %.2f (ground %s) heading %.1f", pos[0], pos[1], z,
                  ground ? "hit" : "missed", heading);
}

const fpv_frame &last_frame() { return g_frame; }

bool osd_frame(fpv_frame *out) {
    std::lock_guard<std::mutex> lock(g_osd_mutex);
    if (!g_osd_visible) return false;
    *out = g_osd_frame;
    return true;
}

void toggle() {
    if (active())
        stop();
    else
        start();
}

// While the overlay is open on foot, Niko must not walk around as you type.
void hold_player_for_overlay() {
    static bool held = false;
    bool open = fpv.fpv_overlay_wants_input() != 0;
    if (g_drone || open == held) return;
    sh::call(N("SET_PLAYER_CONTROL"), player_index(), !open);
    held = open;
}

void tick() {
    if (!fpvapi::loaded()) {
        if (key_pressed(VK_F8)) {  // F8 opens the overlay before the first flight too
            fpvlog::write("F8 pressed (state 0x%04x), loading libfpv", unsigned(GetAsyncKeyState(VK_F8)) & 0xffffu);
            fpvapi::load();
        }
        return;
    }
    forward_keys();
    hold_player_for_overlay();
    if (!g_drone) return;

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = float(now.QuadPart - g_last_tick.QuadPart) / float(freq.QuadPart);
    g_last_tick = now;
    if (dt > 0.1f) dt = 0.1f;

    sh::call(N("HIDE_HUD_AND_RADAR_THIS_FRAME"));  // the radar and HUD would cover the FPV feed

    bool move_niko = false;
    for (uint32_t id; (id = fpv.fpv_overlay_poll_action()) != 0;) move_niko |= id == kMoveNikoAction;
    bool camera_key = key_pressed('C'), reset_key = key_pressed('R'), niko_key = key_pressed('N');
    if (!fpv.fpv_overlay_wants_input()) {  // otherwise the keys belong to the overlay
        move_niko |= niko_key;
        if (camera_key) g_view = View((int(g_view) + 1) % int(View::Count));
        if (reset_key) {
            anchor_end(true);  // Niko home first: the spawn's collision must be streamed in
            fpv.fpv_drone_reset(g_drone, g_spawn_pos, g_spawn_rot);
            fpv.fpv_drone_repair(g_drone);
        }
    }
    if (move_niko) {
        move_niko_here();
        return;
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
    // stale snapshot (the world origin, or the last drone) and interpolates
    // from it: the camera would jump there and the anchor would drag Niko and
    // the streamed world away from the spawn. libfpv's clock is not reset per
    // drone, so wait until it moved 50 ms past its first reading.
    if (g_first_sim_time < 0) g_first_sim_time = frame.sim_time;
    if (frame.sim_time < g_first_sim_time + 0.05) return;
    g_frame = frame;
    update_anchor(frame.position);
    update_home();
    if (frame.flags & FPV_DRONE_OSD_DISABLED) hide_game_text();
    {
        std::lock_guard<std::mutex> lock(g_osd_mutex);
        g_osd_frame = frame;
        g_osd_visible = g_view == View::Fpv;
    }
    apply_camera(g_view == View::Fpv ? frame.camera : g_view == View::Chase ? frame.chase_camera : frame.los_camera,
                 g_view == View::Fpv);

    for (uint32_t i = 0; i < frame.impact_count && i < FPV_MAX_IMPACTS; ++i) {
        const fpv_impact &im = frame.impacts[i];
        if (im.speed < 1.0f) continue;  // resting contact
        fpvlog::write("impact at %.2f %.2f %.2f n %.2f %.2f %.2f speed %.1f impulse %.2f motor %d object %08x",
                      im.position[0], im.position[1], im.position[2], im.normal[0], im.normal[1], im.normal[2], im.speed,
                      im.impulse, int(im.motor_index), im.object_id);
    }
    effects::on_frame(frame);

    // Sparse trace for debugging the axes and the camera mapping.
    static float log_timer = 0;
    log_timer += dt;
    if (log_timer > 1.0f) {
        log_timer = 0;
        fpvlog::write("drone pos %.2f %.2f %.2f flags 0x%x alt %.2f thr %.2f fov %.1f near %.3f arm '%s'",
                      frame.position[0], frame.position[1], frame.position[2], frame.flags, frame.altitude,
                      frame.channels[2], g_fov, g_near_clip, frame.arming_disable_text);
    }
}

}  // namespace drone
