#include "radio.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

#include "log.h"
#include "natives.h"
#include "shv.h"

namespace radio {
namespace {

constexpr int kRadioOff = 255;
constexpr DWORD kNoticeMs = 2500;

bool g_on = false;
int g_station = -1;  // last station picked while flying, kept across flights

std::mutex g_notice_mutex;
char g_notice[96];
DWORD g_notice_until = 0;

bool game_has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Edge-detected key press; the first reading of each key is only a baseline.
bool key_pressed(int vk) {
    static bool was_down[256], seen[256];
    bool down = game_has_focus() && (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = seen[vk] && down && !was_down[vk];
    was_down[vk] = down;
    seen[vk] = true;
    return pressed;
}

void show_station() {
    int index = shv::call<int>(nat::GET_PLAYER_RADIO_STATION_INDEX);
    const char *text = "Radio off";
    if (index != kRadioOff) {
        // Internal name ("RADIO_01_CLASS_ROCK") -> its label ("Los Santos Rock Radio").
        const char *name = shv::call<const char *>(nat::GET_RADIO_STATION_NAME, index);
        const char *label = name ? shv::call<const char *>(nat::GET_FILENAME_FOR_AUDIO_CONVERSATION, name) : nullptr;
        text = label && *label ? label : name ? name : "Radio";
    }
    std::lock_guard<std::mutex> lock(g_notice_mutex);
    std::snprintf(g_notice, sizeof g_notice, "%s", text);
    g_notice_until = GetTickCount() + kNoticeMs;
}

// Steps through the unlocked stations and "radio off", like GTA's wheel.
void step(int dir) {
    int count = shv::call<int>(nat::GET_NUM_UNLOCKED_RADIO_STATIONS);
    if (count <= 0) return;
    int index = shv::call<int>(nat::GET_PLAYER_RADIO_STATION_INDEX);
    int slot = index == kRadioOff || index >= count ? count : index;  // slot `count` = off
    slot = (slot + dir + count + 1) % (count + 1);
    if (slot == count) {
        shv::call(nat::SET_RADIO_TO_STATION_NAME, "OFF");
    } else {
        shv::call(nat::SET_RADIO_TO_STATION_INDEX, slot);
        g_station = slot;
    }
    show_station();
}

void set_mobile_radio(bool on) {
    shv::call(nat::SET_AUDIO_FLAG, "MobileRadioInGame", on);
    shv::call(nat::SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY, on);
    shv::call(nat::SET_MOBILE_PHONE_RADIO_STATE, on);
}

}  // namespace

void start() {
    set_mobile_radio(true);
    if (g_station >= 0) shv::call(nat::SET_RADIO_TO_STATION_INDEX, g_station);
    g_on = true;
    show_station();
    fpvlog::write("radio on (station %d)", shv::call<int>(nat::GET_PLAYER_RADIO_STATION_INDEX));
}

void stop() {
    if (!g_on) return;
    set_mobile_radio(false);
    g_on = false;
    std::lock_guard<std::mutex> lock(g_notice_mutex);
    g_notice_until = 0;
}

void tick(bool keys) {
    if (!g_on) return;
    // The game turns the mobile radio off after some events (phone, cutscene):
    // keep it on while flying.
    if (!shv::call<bool>(nat::IS_MOBILE_PHONE_RADIO_ACTIVE)) set_mobile_radio(true);
    bool prev = key_pressed(VK_OEM_COMMA), next = key_pressed(VK_OEM_PERIOD);
    if (!keys) return;
    if (prev) step(-1);
    if (next) step(+1);
}

bool notice(char *out, size_t size) {
    std::lock_guard<std::mutex> lock(g_notice_mutex);
    if (!g_notice_until || GetTickCount() > g_notice_until) return false;
    std::snprintf(out, size, "%s", g_notice);
    return true;
}

}  // namespace radio
