// Kamikaze builds of libfpv (warhead, bomb arm and explode switches): the
// game's side of a detonation. Everything here is a no-op with a libfpv.dll
// built without LIBFPV_KAMIKAZE.
#pragma once

#include <libfpv.h>

namespace kamikaze {

// What the pilot sees after the blast, before control goes back to Niko.
enum class Ending { SignalLost, Cutscene };

// The player's choice (F8 > Fly button), saved in fpv-data/gtaiv-fpv.ini.
Ending ending();

// Once libfpv is loaded: reads the setting and adds the F8 button. Script thread.
void init();

// Handles a click on an overlay button; false when it is not ours. Script thread.
bool on_action(uint32_t id);

// The game's explosion at the detonation point. Script thread.
void explode(const fpv_detonation &d);

}  // namespace kamikaze
