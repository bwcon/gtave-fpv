// The FPV drone session: libfpv drone + scripted game camera.
#pragma once

#include <libfpv.h>

namespace drone {

// Launches the drone in front of the player, or returns to the player.
// Script fiber only.
void toggle();

// Once per script tick. Script fiber only.
void tick();

bool active();

// Ends the flight with the player standing under the drone (Y key). Script fiber only.
void move_player_here();

// A snapshot for the ReShade tab. Any thread.
struct Status {
    bool loaded = false;
    bool flying = false;
    bool needs_setup = false;
    char device[128] = {};  // fpv_input_status.device_name size
    float position[3] = {};
    float speed_kmh = 0;
    float fov = 0;
    float near_clip = 0;
    char arming[96] = {};  // fpv_frame.arming_disable_text size
};
Status status();

// Copies the latest frame for the OSD; false when no drone is flying, a
// third-person camera is on, or the pilot's OSD switch hides it. Any thread.
bool osd_frame(fpv_frame *out);

// Seconds since this flight's launch, for the goggles' recording timer. Any thread.
double flight_seconds();

// Values for GTAVE_FPV_Lens.fx. Any thread.
struct LensState {
    bool active = false;  // FPV view of a flying drone
    float k1 = 0, k2 = 0;
    float noise = 0, breakup = 0;
    bool digital = false;  // HD OSD mode: a digital link's look
};
LensState lens_state();

// The game camera's Euler angles (degrees) for a view basis in game coords.
void basis_to_euler(const float forward[3], const float up[3], const float right[3], float *pitch, float *roll,
                    float *heading);

}  // namespace drone
