// Bridge to Alexander Blade's ScriptHookV (GTA V and GTA V Enhanced).
// ScriptHookV exports MSVC-mangled free functions, and MinGW cannot link its
// import library, so this file resolves the exports with GetProcAddress.
#pragma once
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace shv {

using ScriptFn = void (*)();

// Vector3 as ScriptHookV passes it: every float sits in an 8-byte slot.
struct Vec3 {
    alignas(8) float x;
    alignas(8) float y;
    alignas(8) float z;
};
static_assert(sizeof(Vec3) == 24, "ScriptHookV Vector3 is 24 bytes");

// Registers `main` as a script fiber of `module`. `main` loops forever and
// calls wait() to yield. Returns false (and logs why) when ScriptHookV.dll is
// missing or does not export what we need.
bool register_script(HMODULE module, ScriptFn main);
void unregister_script(HMODULE module);

// Yields the script fiber for at least `ms` milliseconds (0 = next frame).
void wait(DWORD ms);

void native_init(uint64_t hash);
void native_push(uint64_t value);
uint64_t *native_call();

// Every native argument occupies one 64-bit slot, value in the low bytes.
template <typename T>
inline void push(T value) {
    static_assert(sizeof(T) <= 8, "native arguments are 64-bit slots");
    uint64_t slot = 0;
    std::memcpy(&slot, &value, sizeof(T));
    native_push(slot);
}

// Natives may only be called from a script fiber (between wait() calls).
template <typename R = void, typename... Args>
inline R call(uint64_t hash, Args... args) {
    native_init(hash);
    (push(args), ...);
    uint64_t *result = native_call();
    if constexpr (!std::is_void_v<R>) return *reinterpret_cast<R *>(result);
}

}  // namespace shv
