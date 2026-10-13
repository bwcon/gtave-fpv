// World collision for libfpv through the game's own physics probe.
#pragma once
#include <cstdint>

#include <libfpv.h>

namespace world {

// Probe type flags (IV-SDK names): 2 static, 4 buildings, 8 vehicles, 64 ped
// collision, 128 objects.
constexpr uint32_t kTypeAll = 2 | 4 | 8 | 64 | 128;

// fpv_raycast_fn. Script thread only (it is called inside fpv_drone_tick).
int raycast(void *user, const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit);

}  // namespace world
