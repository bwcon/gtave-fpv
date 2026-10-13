// The FPV drone session: libfpv drone + scripted game camera.
#pragma once

namespace drone {

// Launches the drone from the player, or returns to the player. Script thread only.
void toggle();

// Once per script tick while the drone is active. Script thread only.
void tick();

bool active();

}  // namespace drone
