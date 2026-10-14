// Bridge to Aru's GTA IV ScriptHook (ScriptHook.dll, 0.5.1 in the Complete
// Edition packs). ScriptHook exports MSVC C++ classes, which MinGW cannot
// link against directly, so this file resolves the mangled exports with
// GetProcAddress and builds the ScriptThread object and vtable by hand.
#pragma once
#include <cstdint>
#include <type_traits>

namespace sh {

// Called once per game script tick, on the game's script thread. Natives may
// only be called from here.
using TickFn = void (*)();

// Registers a ScriptThread whose RunTick calls `tick`. Returns false (and logs
// why) when ScriptHook.dll is missing or does not export what we need.
bool register_thread(TickFn tick);

// How often ScriptHook killed our thread. It kills and restarts every script
// thread when a save is loaded, and restarts its services (D3D hooks) too.
unsigned kill_count();

// rage::scrNativeCallContext plus ScriptHook's NativeContext argument stack.
struct NativeContext {
    struct Vec3 { float x, y, z; };
    struct Vec4 { float x, y, z, w; };
    void *ret;
    uint32_t arg_count;
    void *args;
    uint32_t data_count;
    Vec3 *original_data[4];
    Vec4 temporary_data[4];
    uint32_t stack[16];
};

void invoke(NativeContext *ctx, uint32_t hash);

// Jenkins one-at-a-time over the lowercase native name.
constexpr uint32_t joaat(const char *s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        h += uint8_t(c);
        h += h << 10;
        h ^= h >> 6;
    }
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}

template <typename T>
inline void push(NativeContext &ctx, T value) {
    static_assert(sizeof(T) <= 4, "native arguments are 32-bit");
    ctx.stack[ctx.arg_count] = 0;
    *reinterpret_cast<T *>(&ctx.stack[ctx.arg_count]) = value;
    ++ctx.arg_count;
}

template <typename R = void, typename... Args>
inline R call(uint32_t hash, Args... args) {
    NativeContext ctx{};
    ctx.ret = ctx.stack;
    ctx.args = ctx.stack;
    (push(ctx, args), ...);
    invoke(&ctx, hash);
    while (ctx.data_count > 0) {  // copy Vector3 out-params back (NativeContext::GetResult)
        --ctx.data_count;
        NativeContext::Vec3 *dst = ctx.original_data[ctx.data_count];
        const NativeContext::Vec4 &src = ctx.temporary_data[ctx.data_count];
        dst->x = src.x;
        dst->y = src.y;
        dst->z = src.z;
    }
    if constexpr (!std::is_void_v<R>) return *reinterpret_cast<R *>(ctx.stack);
}

}  // namespace sh

// N("GET_PLAYER_ID") -> compile-time hash.
#define N(name) (std::integral_constant<uint32_t, sh::joaat(name)>::value)
