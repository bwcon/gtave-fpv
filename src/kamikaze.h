// Kamikaze builds of libfpv (warhead, bomb arm and explode switches): the
// game's side of a detonation. Everything here is a no-op with a libfpv.dll
// built without LIBFPV_KAMIKAZE.
#pragma once

#include <libfpv.h>

namespace kamikaze {

// What happens after the blast. The player picks it in libfpv's overlay
// (F8 > Kamikaze), libfpv saves it; the mod reads it at the detonation.
struct Ending {
    fpv_kamikaze_ending_view view = FPV_KAMIKAZE_VIEW_SIGNAL_LOST;
    float seconds = 3.0f;  // how long the view shows; 0 = skip it
    fpv_kamikaze_ending_then then = FPV_KAMIKAZE_THEN_RESPAWN;
};

// The player's current choice (defaults with an older libfpv). Script thread.
Ending ending();

// Once libfpv is loaded: tells libfpv which endings GTA IV supports. Script thread.
void init();

// The game's explosion at the detonation point. Script thread.
void explode(const fpv_detonation &d);

const char *view_name(fpv_kamikaze_ending_view v);
const char *then_name(fpv_kamikaze_ending_then t);

}  // namespace kamikaze
