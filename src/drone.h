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
const fpv_frame &last_frame();

// Host-injected radio channels (AETR, AUX1..4, -1..1), overriding devices for 0.5 s.
void inject_channels(const float channels[8]);

}  // namespace drone
