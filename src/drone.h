// The FPV drone session: libfpv drone + scripted game camera.
#pragma once

#include <libfpv.h>

namespace drone {

// Launches the drone from the player, or returns to the player. Script thread only.
void toggle();

// Once per script tick while the drone is active. Script thread only.
void tick();

bool active();

// The frame from the latest tick (struct_size 0 before the first one).
// Script thread only.
const fpv_frame &last_frame();

// Copies the latest frame for the OSD; false when no OSD should show (no drone,
// or a third-person camera). Any thread.
bool osd_frame(fpv_frame *out);

// Host-injected radio channels (AETR, AUX1..4, -1..1), overriding devices for 0.5 s.
void inject_channels(const float channels[8]);

}  // namespace drone
