#include "dvr.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "log.h"

namespace dvr {
namespace {

constexpr DWORD kStopDelayMs = 2000;  // keep the crash or landing on the clip

std::atomic<bool> g_recording{false};
DWORD g_disarmed_at = 0;
int g_mode = -1;  // -1 unread, 0 off, 1 ShadowPlay

std::string ini_path() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path, n);
    return dir.substr(0, dir.find_last_of("\\/")) + "\\GTAVE.FPV.ini";
}

// [DVR] Mode = shadowplay (default) or off.
int mode() {
    if (g_mode >= 0) return g_mode;
    const std::string ini = ini_path();
    char buf[32];
    GetPrivateProfileStringA("DVR", "Mode", "", buf, sizeof buf, ini.c_str());
    if (!buf[0]) {
        WritePrivateProfileStringA("DVR", "Mode", "shadowplay", ini.c_str());
        std::snprintf(buf, sizeof buf, "shadowplay");
    }
    g_mode = lstrcmpiA(buf, "off") == 0 ? 0 : 1;
    fpvlog::write("dvr: mode %s", g_mode ? "ShadowPlay (Alt+F9)" : "off");
    return g_mode;
}

void key(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = WORD(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof in);
}

// ShadowPlay's record toggle. ponytail: a toggle the mod cannot read back; if
// the player also presses Alt+F9 by hand, the two get out of step.
void press_record() {
    key(VK_MENU, false);
    key(VK_F9, false);
    key(VK_F9, true);
    key(VK_MENU, true);
    g_recording = !g_recording;
    fpvlog::write("dvr: recording %s", g_recording ? "started" : "stopped");
}

}  // namespace

void tick(bool armed) {
    if (mode() == 0) return;
    if (armed) {
        g_disarmed_at = 0;
        if (!g_recording) press_record();
        return;
    }
    if (!g_recording) return;
    if (!g_disarmed_at) g_disarmed_at = GetTickCount() | 1;
    if (GetTickCount() - g_disarmed_at >= kStopDelayMs) {
        press_record();
        g_disarmed_at = 0;
    }
}

void stop() {
    if (g_recording) press_record();
    g_disarmed_at = 0;
}

bool recording() { return g_recording; }

}  // namespace dvr
