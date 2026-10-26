// The pilot's FPV goggles: the gtavefpv_goggles prop on the player's head
// while the drone flies (the hat and glasses come off and go back on after).
#pragma once

namespace goggles {

// Script thread: at launch, every flight tick, and at landing.
void start(int ped);
void tick(int ped);
void stop(int ped);

// [Goggles] in GTAVE.FPV.ini, shown in the tab. Any thread.
struct Placement {
    bool show = true;
    float offset[3] = {0.06f, 0.04f, 0.0f};  // metres, in the head bone's axes
    float rotation[3] = {0.0f, 90.0f, 0.0f};  // degrees
};
Placement placement();
void set_placement(const Placement &p, bool save);

}  // namespace goggles
