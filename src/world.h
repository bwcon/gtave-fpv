// World collision for libfpv through GTA V's synchronous shape-test probe.
#pragma once
#include <cstdint>

#include <libfpv.h>

namespace world {

enum class Kind : uint32_t { None = 0, Ped = 1, Vehicle = 2, Object = 3 };

// What a probe hit, as libfpv's fpv_impact.object_id: GET_ENTITY_TYPE in the
// top 4 bits, the script handle below. 0 for the static map.
inline Kind kind_of(uint32_t object_id) { return Kind(object_id >> 28); }
inline uint32_t handle_of(uint32_t object_id) { return object_id & 0x0fffffffu; }

// Makes probes pass through this entity (the frozen player). 0 clears it.
void ignore_entity(int handle);

// fpv_raycast_fn. Script fiber only (it is called inside fpv_drone_tick).
int raycast(void *user, const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit);

}  // namespace world
