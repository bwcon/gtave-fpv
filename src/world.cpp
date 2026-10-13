#include "world.h"

#include <windows.h>

#include <cstddef>
#include <cstring>

#include "log.h"
#include "scripthook.h"

namespace world {
namespace {

// GTA IV 1.2.0.59 addresses as RVAs (taken from a dump loaded at 0x700000,
// see MODLOG). GET_GROUND_Z_FOR_3D_COORD -> 0xe962d0 ->
// 0xd523f0 -> vertical probe 0xd54510, which calls the level's segment test.
constexpr uint32_t kProbeRva = 0xa38880 - 0x700000;        // phLevel::TestProbe-like, thiscall, ret 0x20
constexpr uint32_t kVerticalProbeRva = 0xd54510 - 0x700000; // reads the level pointer at +0x60
constexpr uint32_t kLevelPtrRva = 0x15b9c78 - 0x700000;

const uint8_t kProbePrologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0, 0xb8, 0xcc, 0x49, 0x00, 0x00};

struct alignas(16) Vec4 {
    float x, y, z, w;
};

struct alignas(16) Segment {
    Vec4 a, b;
};

// rage intersection record, 0x54 bytes (layout matches IV-SDK's tLineOfSightResults).
struct alignas(16) Intersection {
    void *inst;        // 0x00 phInst of what was hit
    uint8_t pad[0xc];  // 0x04
    Vec4 position;     // 0x10
    Vec4 normal_a;     // 0x20
    Vec4 normal_b;     // 0x30
    float t;           // 0x40
    float depth;       // 0x44
    uint32_t unk48;    // 0x48
    uint32_t part;     // 0x4c, 0xffff when unset
    uint8_t material;  // 0x50
    uint8_t pad2[0xf];
};
static_assert(offsetof(Intersection, position) == 0x10, "layout");
static_assert(offsetof(Intersection, t) == 0x40, "layout");
static_assert(offsetof(Intersection, material) == 0x50, "layout");

using ProbeFn = int(__attribute__((thiscall)) *)(void *level, const Segment *seg, Intersection *out, void *exclude,
                                                 uint32_t type_flags, int32_t include_flags, uint32_t unk7,
                                                 uint32_t max_hits, uint32_t unk0);

ProbeFn g_probe = nullptr;
void **g_level = nullptr;
bool g_checked = false;

bool resolve() {
    if (g_checked) return g_probe != nullptr;
    g_checked = true;
    auto base = reinterpret_cast<uint8_t *>(GetModuleHandleA(nullptr));
    uint8_t *probe = base + kProbeRva;
    uint8_t *vprobe = base + kVerticalProbeRva;
    // The vertical probe loads the level with `mov ecx, [abs]` at +0x60.
    uint8_t *mov = vprobe + 0x60;
    uint32_t level_abs = 0;
    std::memcpy(&level_abs, mov + 2, 4);
    if (std::memcmp(probe, kProbePrologue, sizeof kProbePrologue) != 0 || mov[0] != 0x8b || mov[1] != 0x0d ||
        level_abs != uint32_t(reinterpret_cast<uintptr_t>(base + kLevelPtrRva))) {
        fpvlog::write("world: probe signature mismatch (not GTA IV 1.2.0.59?), using ground-only collision");
        return false;
    }
    g_probe = reinterpret_cast<ProbeFn>(probe);
    g_level = reinterpret_cast<void **>(base + kLevelPtrRva);
    fpvlog::write("world: engine probe at %p, level %p", static_cast<void *>(probe), *g_level);
    return true;
}

int ground_only(const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit) {
    if (dir[2] > -0.5f) return 0;
    float ground = 0;
    sh::call(N("GET_GROUND_Z_FOR_3D_COORD"), origin[0], origin[1], origin[2], &ground);
    if (ground == 0 || ground > origin[2]) return 0;
    float distance = (origin[2] - ground) / -dir[2];
    if (distance > max_distance) return 0;
    hit->distance = distance;
    hit->normal[0] = 0;
    hit->normal[1] = 0;
    hit->normal[2] = 1;
    hit->material = 0;
    hit->object_id = 0;
    return 1;
}

bool usable_normal(const Vec4 &n, const float dir[3]) {
    float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
    return len2 > 0.81f && len2 < 1.21f && n.x * dir[0] + n.y * dir[1] + n.z * dir[2] < 0;
}

}  // namespace

int raycast(void *, const float origin[3], const float dir[3], float max_distance, fpv_raycast_hit *hit) {
    if (!resolve() || !*g_level) return ground_only(origin, dir, max_distance, hit);

    Segment seg{};
    seg.a = {origin[0], origin[1], origin[2], 0};
    seg.b = {origin[0] + dir[0] * max_distance, origin[1] + dir[1] * max_distance,
             origin[2] + dir[2] * max_distance, 0};
    Intersection is{};
    is.part = 0xffff;
    // Same trailing arguments as the game's own ground probe; types: static,
    // buildings, vehicles, peds, objects.
    if (!g_probe(*g_level, &seg, &is, nullptr, kTypeAll, -1, 7, 1, 0)) return 0;

    float dx = is.position.x - origin[0], dy = is.position.y - origin[1], dz = is.position.z - origin[2];
    float distance = dx * dir[0] + dy * dir[1] + dz * dir[2];
    if (distance < 0 || distance > max_distance) return 0;

    const Vec4 &n = usable_normal(is.normal_a, dir) ? is.normal_a : is.normal_b;
    static int traced = 0;
    if (traced < 10) {
        ++traced;
        fpvlog::write("world: hit d %.2f inst %p pos %.2f %.2f %.2f nA %.2f %.2f %.2f nB %.2f %.2f %.2f t %.3f mat %u",
                      distance, is.inst, is.position.x, is.position.y, is.position.z, is.normal_a.x, is.normal_a.y,
                      is.normal_a.z, is.normal_b.x, is.normal_b.y, is.normal_b.z, is.t, is.material);
    }
    if (!usable_normal(n, dir)) {
        // No trustworthy normal: treat it as facing straight back at the ray.
        hit->normal[0] = -dir[0];
        hit->normal[1] = -dir[1];
        hit->normal[2] = -dir[2];
    } else {
        hit->normal[0] = n.x;
        hit->normal[1] = n.y;
        hit->normal[2] = n.z;
    }
    hit->distance = distance;
    hit->material = 0;
    hit->object_id = 0;
    return 1;
}

}  // namespace world
