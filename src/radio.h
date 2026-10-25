// GTA's radio while flying, like in a car: the game's mobile radio plays on
// foot, and , / . step through the stations (GTA's own vehicle keys).
#pragma once

#include <cstddef>

namespace radio {

// Script fiber only.
void start();  // drone launched: radio on, at the last station
void stop();   // drone landed: radio off
void tick(bool keys);  // keys: false while the overlay takes the keyboard

// The station name for a short on-screen notice; false when there is none.
// Any thread.
bool notice(char *out, size_t size);

}  // namespace radio
