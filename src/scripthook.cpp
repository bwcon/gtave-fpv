#include "scripthook.h"

#include <windows.h>

#include <cstring>

#include "log.h"

namespace sh {
namespace {

// MSVC member functions are __thiscall; static members are __cdecl.
using CtorFn = void *(__attribute__((thiscall)) *)(void *self);
using RegisterFn = void(__cdecl *)(void *thread);
using InvokeFn = void(__cdecl *)(NativeContext *ctx, uint32_t hash);

// ScriptThread vtable (checked against ??_7ScriptThread@@6B@ in 0.5.1):
// [-1] RTTI locator, [0] RunTick, [1] RunScript, [2] OnStart, [3] OnKill,
// [4] scalar deleting destructor.
constexpr int kVtableSlots = 5;

struct Thread {
    void **vtable;
    void *base_thread;  // ScriptThread::m_pBaseThread
    uint8_t reserved[56];
};

template <typename F>
F export_fn(HMODULE module, const char *name) {
    return reinterpret_cast<F>(reinterpret_cast<void *>(GetProcAddress(module, name)));
}

Thread g_thread;
void *g_vtable[1 + kVtableSlots];
TickFn g_tick;
InvokeFn g_invoke;

void __attribute__((thiscall)) run_tick(void *) {
    if (g_tick) g_tick();
}

using OnKillFn = void(__attribute__((thiscall)) *)(void *self);
OnKillFn g_base_on_kill;
volatile LONG g_kills = 0;

void __attribute__((thiscall)) on_kill(void *self) {
    InterlockedIncrement(&g_kills);
    if (g_base_on_kill) g_base_on_kill(self);
}

void *__attribute__((thiscall)) deleting_dtor(void *self, unsigned) {
    return self;  // g_thread is static: nothing to free
}

}  // namespace

unsigned kill_count() { return unsigned(g_kills); }

void invoke(NativeContext *ctx, uint32_t hash) {
    if (!g_invoke)  // a hot-reloaded core resolves it on first use
        g_invoke = export_fn<InvokeFn>(GetModuleHandleA("ScriptHook.dll"),
                                       "?SpeedyInvoke@NativeInvoke@@CAXPAVNativeContext@@I@Z");
    g_invoke(ctx, hash);
}

bool register_thread(TickFn tick) {
    HMODULE hook = GetModuleHandleA("ScriptHook.dll");
    if (!hook) hook = LoadLibraryA("ScriptHook.dll");
    if (!hook) {
        fpvlog::write("ScriptHook.dll not found");
        return false;
    }
    auto ctor = export_fn<CtorFn>(hook, "??0ScriptThread@@QAE@XZ");
    auto reg = export_fn<RegisterFn>(
        hook, "?RegisterThread@ScriptHookManager@@SAXPAVScriptThread@@@Z");
    g_invoke = export_fn<InvokeFn>(
        hook, "?SpeedyInvoke@NativeInvoke@@CAXPAVNativeContext@@I@Z");
    if (!ctor || !reg || !g_invoke) {
        fpvlog::write("ScriptHook.dll is missing an export (ctor=%p register=%p invoke=%p)",
                   (void *)ctor, (void *)reg, (void *)g_invoke);
        return false;
    }

    ctor(&g_thread);
    // Derive from ScriptThread the way MSVC would: copy its vtable (and RTTI
    // slot) and override RunTick, OnKill and the destructor.
    std::memcpy(g_vtable, g_thread.vtable - 1, sizeof(g_vtable));
    g_vtable[1 + 0] = reinterpret_cast<void *>(&run_tick);
    g_base_on_kill = reinterpret_cast<OnKillFn>(g_vtable[1 + 3]);
    g_vtable[1 + 3] = reinterpret_cast<void *>(&on_kill);
    g_vtable[1 + 4] = reinterpret_cast<void *>(&deleting_dtor);
    g_thread.vtable = g_vtable + 1;

    g_tick = tick;
    reg(&g_thread);
    fpvlog::write("ScriptThread registered");
    return true;
}

}  // namespace sh
