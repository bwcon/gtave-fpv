#include "shv.h"

#include "log.h"

namespace shv {
namespace {

using RegisterFn = void (*)(HMODULE module, ScriptFn main);
using UnregisterFn = void (*)(HMODULE module);
using WaitFn = void (*)(DWORD ms);
using InitFn = void (*)(uint64_t hash);
using PushFn = void (*)(uint64_t value);
using CallFn = uint64_t *(*)();

// Export names checked against ScriptHookV 3889.0 (GTA V Enhanced 1.0.1158.13).
RegisterFn g_register;
UnregisterFn g_unregister;
WaitFn g_wait;
InitFn g_init;
PushFn g_push;
CallFn g_call;

template <typename F>
F export_fn(HMODULE module, const char *name) {
    return reinterpret_cast<F>(reinterpret_cast<void *>(GetProcAddress(module, name)));
}

bool resolve() {
    if (g_register) return true;
    HMODULE hook = GetModuleHandleA("ScriptHookV.dll");
    if (!hook) hook = LoadLibraryA("ScriptHookV.dll");
    if (!hook) {
        fpvlog::write("ScriptHookV.dll not found");
        return false;
    }
    g_register = export_fn<RegisterFn>(hook, "?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z");
    g_unregister = export_fn<UnregisterFn>(hook, "?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z");
    g_wait = export_fn<WaitFn>(hook, "?scriptWait@@YAXK@Z");
    g_init = export_fn<InitFn>(hook, "?nativeInit@@YAX_K@Z");
    g_push = export_fn<PushFn>(hook, "?nativePush64@@YAX_K@Z");
    g_call = export_fn<CallFn>(hook, "?nativeCall@@YAPEA_KXZ");
    if (!g_register || !g_unregister || !g_wait || !g_init || !g_push || !g_call) {
        fpvlog::write("ScriptHookV.dll is missing an export (register=%p wait=%p init=%p push=%p call=%p)",
                      (void *)g_register, (void *)g_wait, (void *)g_init, (void *)g_push,
                      (void *)g_call);
        g_register = nullptr;
        return false;
    }
    return true;
}

}  // namespace

bool register_script(HMODULE module, ScriptFn main) {
    if (!resolve()) return false;
    g_register(module, main);
    fpvlog::write("script registered with ScriptHookV");
    return true;
}

void unregister_script(HMODULE module) {
    if (g_unregister) g_unregister(module);
}

void wait(DWORD ms) { g_wait(ms); }
void native_init(uint64_t hash) { g_init(hash); }
void native_push(uint64_t value) { g_push(value); }
uint64_t *native_call() { return g_call(); }

}  // namespace shv
