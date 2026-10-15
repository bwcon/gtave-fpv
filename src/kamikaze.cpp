#include "kamikaze.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "fpvapi.h"
#include "log.h"
#include "scripthook.h"

namespace kamikaze {
namespace {

using fpvapi::fpv;

constexpr const char *kSettingsFile = "fpv-data\\gtaiv-fpv.ini";
constexpr uint32_t kEndingAction = 2;  // F8 > Fly button id (1 is Move Niko here)

// A 5" strike quad carries a few hundred grams of explosive: grenade-sized.
constexpr uint32_t kExplosionGrenade = 0;
constexpr float kExplosionRadius = 5.0f;
constexpr float kCamShake = 1.0f;

Ending g_ending = Ending::SignalLost;
bool g_ready = false;

void load_setting() {
    char buf[256] = {};
    if (FILE *f = std::fopen(kSettingsFile, "r")) {
        std::fread(buf, 1, sizeof buf - 1, f);
        std::fclose(f);
    }
    g_ending = std::strstr(buf, "after_blast=cutscene") ? Ending::Cutscene : Ending::SignalLost;
}

void save_setting() {
    if (FILE *f = std::fopen(kSettingsFile, "w")) {
        std::fprintf(f, "after_blast=%s\n", g_ending == Ending::Cutscene ? "cutscene" : "signal_lost");
        std::fclose(f);
    }
}

void update_button() {
    fpv.fpv_overlay_set_action(kEndingAction, g_ending == Ending::Cutscene ? "After the blast: Cutscene (click to change)"
                                                                           : "After the blast: Signal lost (click to change)");
}

}  // namespace

Ending ending() { return g_ending; }

void init() {
    if (g_ready || !fpvapi::kamikaze()) return;
    g_ready = true;
    load_setting();
    update_button();
    fpvlog::write("kamikaze: after the blast: %s", g_ending == Ending::Cutscene ? "cutscene" : "signal lost");
}

bool on_action(uint32_t id) {
    if (id != kEndingAction || !g_ready) return false;
    g_ending = g_ending == Ending::Cutscene ? Ending::SignalLost : Ending::Cutscene;
    save_setting();
    update_button();
    fpvlog::write("kamikaze: after the blast now: %s", g_ending == Ending::Cutscene ? "cutscene" : "signal lost");
    return true;
}

void explode(const fpv_detonation &d) {
    static const char *causes[] = {"impact", "explode switch", "game"};
    sh::call(N("ADD_EXPLOSION"), d.position[0], d.position[1], d.position[2], kExplosionGrenade, kExplosionRadius,
             true, false, kCamShake);
    fpvlog::write("kamikaze: detonation (%s) at %.2f %.2f %.2f speed %.1f object %08x",
                  causes[unsigned(d.cause) < 3 ? d.cause : 2], d.position[0], d.position[1], d.position[2],
                  std::sqrt(d.velocity[0] * d.velocity[0] + d.velocity[1] * d.velocity[1] +
                             d.velocity[2] * d.velocity[2]),
                  d.object_id);
}

}  // namespace kamikaze
