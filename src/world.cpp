#include "world.h"

#include "log.h"
#include "natives.h"
#include "shv.h"

namespace world {
namespace {

// Shape-test types: 1 map, 2 vehicles, 4 ped capsules, 8 ragdolls, 16 objects.
// Foliage (256) is left out so bushes stay fly-through. A mask of -1 missed
// vehicles in the Phase 0 spike (the ray went through a car to the road).
constexpr int kTypes = 1 | 2 | 4 | 8 | 16;
// Option bits: 1 ignore glass, 2 ignore see-through, 4 ignore no-collision.
// Only 4: with 7, chain-link fences and railings (see-through) were fly-through.
constexpr int kOptions = 4;

int g_ignore = 0;

bool usable_normal(const shv::Vec3 &n, const float dir[3]) {
    float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
    return len2 > 0.81f && len2 < 1.21f && n.x * dir[0] + n.y * dir[1] + n.z * dir[2] < 0;
}

}  // namespace

void ignore_entity(int handle) { g_ignore = handle; }

int raycast(void *, const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit) {
    float to[3];
    for (int i = 0; i < 3; ++i) to[i] = origin[i] + dir[i] * max_distance;
    int test = shv::call<int>(nat::START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE, origin[0], origin[1],
                              origin[2], to[0], to[1], to[2], kTypes, g_ignore, kOptions);
    int hit_flag = 0, entity = 0;
    shv::Vec3 end{}, n{};
    if (shv::call<int>(nat::GET_SHAPE_TEST_RESULT, test, &hit_flag, &end, &n, &entity) != 2 || !hit_flag) return 0;

    float distance = (end.x - origin[0]) * dir[0] + (end.y - origin[1]) * dir[1] + (end.z - origin[2]) * dir[2];
    if (distance < 0 || distance > max_distance) return 0;

    // Static map hits come back with a handle too, but of type 0.
    int type = entity ? shv::call<int>(nat::GET_ENTITY_TYPE, entity) : 0;
    uint32_t object_id = type > 0 ? uint32_t(type) << 28 | (uint32_t(entity) & 0x0fffffffu) : 0;

    static uint32_t last_id = ~0u;
    static int traced = 0;
    if (traced < 10 || (object_id != last_id && traced < 60)) {
        ++traced;
        last_id = object_id;
        fpvlog::write("world: hit d %.2f kind %d handle %d n %.2f %.2f %.2f", distance, type, entity, n.x, n.y, n.z);
    }

    if (usable_normal(n, dir)) {
        hit->normal[0] = n.x;
        hit->normal[1] = n.y;
        hit->normal[2] = n.z;
    } else {  // no trustworthy normal: treat it as facing straight back at the ray
        hit->normal[0] = -dir[0];
        hit->normal[1] = -dir[1];
        hit->normal[2] = -dir[2];
    }
    hit->distance = distance;
    hit->material = 0;
    hit->object_id = object_id;
    return 1;
}

}  // namespace world
