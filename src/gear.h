// The pilot's gear while the drone flies: FPV goggles on the player's head and
// the radio in both hands (a looping two-handed hold). A hat or glasses come
// off for the flight and go back on after.
#pragma once

namespace gear {

enum Item { Goggles, Radio, ItemCount };

// Script thread: at launch, every flight tick, and at landing.
void start(int ped);
void tick(int ped);
void stop(int ped);

// [Goggles] and [Radio] in GTAVE.FPV.ini, shown in the tab. Any thread.
struct Placement {
    bool show = true;
    float offset[3] = {0, 0, 0};    // metres, in the bone's axes
    float rotation[3] = {0, 0, 0};  // degrees
};
const char *label(Item item);
Placement placement(Item item);
void set_placement(Item item, const Placement &p, bool save);

}  // namespace gear
