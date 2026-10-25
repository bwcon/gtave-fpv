// FPV DVR: records the finished FPV picture (lens, OSD, goggles bar) while the
// quad is armed, like goggles DVR, by pressing NVIDIA ShadowPlay's record
// hotkey (Alt+F9). Game-state replays (Rockstar Editor) cannot do this: they
// re-render the scene on export, without the mod's lens and OSD.
#pragma once

namespace dvr {

// Script fiber, every flight tick.
void tick(bool armed);

// Drone landed or gone: stop a running recording now.
void stop();

// True while the mod believes ShadowPlay is recording. Any thread.
bool recording();

}  // namespace dvr
