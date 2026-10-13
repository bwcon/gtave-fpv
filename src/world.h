// World collision for libfpv through the game's own physics probe.
#pragma once
#include <cstdint>

#include <libfpv.h>

namespace world {

// Probe type flags (IV-SDK names): 2 static, 4 buildings, 8 vehicles, 64 ped
// collision, 128 objects.
constexpr uint32_t kTypeAll = 2 | 4 | 8 | 64 | 128;

enum class Kind : uint32_t { None = 0, Ped = 1, Vehicle = 2, Object = 3, Other = 4 };

// What a probe hit, as libfpv's fpv_impact.object_id: kind in the top 4 bits,
// the script handle below (0 for buildings and other non-script entities).
inline Kind kind_of(uint32_t object_id) { return Kind(object_id >> 28); }
inline uint32_t handle_of(uint32_t object_id) { return object_id & 0x0fffffffu; }

// Makes probes pass through this ped (the frozen player). 0 clears it.
void ignore_ped(uint32_t ped_handle);

// fpv_raycast_fn. Script thread only (it is called inside fpv_drone_tick).
int raycast(void *user, const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit);

}  // namespace world
