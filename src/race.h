// Checkpoint races: gates recorded in flight, drawn as rings, timed per lap.
//
// N starts recording a new race and N again saves it. Each press of M, or
// each flick of the mark switch ([Race] MarkSwitch, AUX3 by default), drops a
// gate where the drone is, facing the way it flies. U removes the last gate.
// E instead of N drops a last gate there and saves a point to point race,
// which finishes at that gate. L cycles through the saved races in
// <game>\fpv-data\races and off. The first gate is the start. On a circuit it
// is also the finish line: a lap is every gate in order and back to it, and
// the race is [Race] Laps laps (0: endless practice).
#pragma once

#include <cstddef>

#include "libfpv.h"

namespace race {

// Script thread, every flight tick: keys, the mark switch, gate crossings and
// the gate markers. `keys` is false while the overlay owns the keyboard.
void tick(const fpv_frame &frame, bool keys);

// R (respawn): the lap starts again at the start gate.
void restart();

// The race status for the HUD (render thread); false when there is none.
bool hud(char *line, size_t size);

// [Race] Laps, shown in the tab. Any thread.
int laps();
void set_laps(int laps);

}  // namespace race
