# GTA IV FPV

![GTA IV FPV GIF](gtaiv-fpv.gif)

Fly an FPV drone through Liberty City with your own radio. Press F7 and Niko
launches a quad from the street in front of him. It is simulated by 
[libfpv](https://github.com/hammerill/libfpv), which
runs real Betaflight firmware, so rates, angle mode, air mode, arming and
turtle mode behave as they do on a real quad.

## Features

- **Real Betaflight** in the loop, with your rates, modes and tuning. Set up
  your radio once in the F8 overlay's wizard. Betaflight Configurator can
  connect too.
- **Three cameras**: FPV (the goggles view, with a fisheye lens), chase and
  line of sight from where Niko stands.
- **Betaflight OSD** in the FPV view: battery, cell voltage, current, flight
  timer and mode. The GPS elements work too (speed, altitude, vario, distance
  flown, home arrow and distance); turn them on in Configurator's OSD tab.
  Home is Niko, so the arrow always points back to him.
- **Collides with the world**: buildings, the road, cars, peds and props, all
  through the engine's own physics probe. Crashes break props and burn
  motors.
- **Crash effects**: a fast hit knocks a ped over (ragdoll and damage), dents
  a car or shoves a prop.
- **Motor sound** from libfpv's synthesizer.
- **Clean view**: the minimap and HUD are hidden while you fly. Put
  Betaflight's OSD disable on a switch (the radio wizard asks for one) to
  hide the OSD, help boxes and subtitles too, for recording. Subtitles that
  appear while the switch is on are skipped. Mission markers and other
  things drawn in the world stay.
- **Move Niko here**: ends the flight with Niko standing where the drone is,
  facing the way it faced.
- **Kamikaze** (only with a kamikaze build of libfpv, see below): the drone
  carries a warhead. A detonation sets off a grenade-sized GTA explosion
  where it happened. Then either the goggles show static with "SIGNAL LOST"
  for 4 seconds, or an outside camera shows the blast for 5 seconds
  ("Cutscene"). After that you control Niko again. Pick the ending with the
  "After the blast" button in F8 > Fly.

## Kamikaze

Build libfpv with `-DLIBFPV_KAMIKAZE=ON` and put that `libfpv.dll` in
`GTAIV/`. The mod finds the warhead functions at runtime, so the same
`GTAIV.FPV.core.dll` works with both builds. With a normal libfpv the kamikaze
parts are switched off. The log says which build is loaded: "kamikaze build"
or "no warhead". The release ZIP ships the normal build.

The rules are libfpv's. Arm the motors with the bomb switch (AUX7) off. Take
off and flip the switch, and after the arming delay (OSD `ARMING 3`, then
`STEADY`) the warhead is live. Once it is live, any touch sets it off, and so
does the explode switch (AUX8). Without a radio, K toggles the bomb switch
and X held is the explode switch. On a gamepad, X/Square toggles the bomb
switch and the right shoulder button is the explode switch. Payload, arming
delay and fuse sensitivity are in F8 > Fly. The ending choice is saved in
`GTAIV/fpv-data/gtaiv-fpv.ini`.

## Controls

| Key | Action |
|---|---|
| F7 | Launch the drone / return to Niko |
| F8 | Settings overlay (radio setup, rates, OSD, camera, fisheye strength) |
| C | Cycle cameras: FPV, chase, line of sight |
| R | Respawn the drone at its launch point, repaired |
| Y | Move Niko here (also a button in F8 > Fly) |

Without a radio, the keyboard works as a fallback. Enter arms, W/S is
throttle, A/D is yaw, and the arrow keys are pitch and roll. A gamepad works
too.

Fisheye strength and FOV are in F8 > Camera & sound. A strength of 0 turns
the lens off.

## Requirements

- GTA IV: The Complete Edition, version 1.2.0.59 (Steam). This is the only
  version that has been tested.
- [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) (its ASI
  loader, `dinput8.dll`).
- Aru's C++ ScriptHook (`ScriptHook.dll`), for example from Tomasak's .NET
  ScriptHook pack.
- Windows 10 or 11. The game is 32-bit, so libfpv must be built for i686.

## Installing

**Players: follow the [install guide on the latest release](https://github.com/hammerill/gtaiv-fpv/releases/latest).**
It links everything you need and takes about five minutes.

Every push to `main` builds a new release (`.github/workflows/release.yml`)
with the latest libfpv, so `gtaiv-fpv.zip` is always current. The release
text comes from `docs/INSTALL.md`. To rebuild with a newer libfpv without a
mod change, run the Release workflow by hand from the Actions tab.

The ZIP holds these files, which go into the `GTAIV/` folder next to
`GTAIV.exe`:

| File | What it is |
|---|---|
| `GTAIV.FPV.asi` | Loader that the ASI loader picks up |
| `GTAIV.FPV.core.dll` | The mod itself (hot-reloaded by the loader) |
| `libfpv.dll` | libfpv, i686 build |
| `libfpv.LICENSE.txt` | libfpv's license (GPL-3.0-or-later) |

Settings, radio mappings and Betaflight EEPROMs go in `GTAIV/fpv-data/`. The
log is `GTAIV/GTAIV.FPV.log`.

## Building

The build needs CMake 3.24 or newer and the i686 MinGW-w64 toolchain
(`i686-w64-mingw32-g++-posix`). It cross-compiles from Linux or WSL.

```sh
# libfpv, 32-bit (from the libfpv checkout); add -DLIBFPV_KAMIKAZE=ON for the warhead
cmake -S . -B build/win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake
cmake --build build/win32 -j

# this mod
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake
cmake --build build
./deploy.sh   # copies the asi, the core and libfpv.dll into ../GTAIV
# or with a kamikaze libfpv built in build/win32-kamikaze:
LIBFPV_DLL=~/Projects/libfpv/build/win32-kamikaze/libfpv.dll ./deploy.sh
```

`LIBFPV_DIR` (default `~/Projects/libfpv`) and `LIBFPV_BUILD` point the
build at libfpv. Only the header is used at build time, because the core
loads `libfpv.dll` at runtime.

If you run `./deploy.sh` while the game is running, the new core reloads
within about a second. The `.asi` and `libfpv.dll` stay locked until the
game exits.

## How it works

- **ScriptHook from MinGW.** `ScriptHook.dll` exports only MSVC C++ symbols.
  `src/scripthook.cpp` finds them by their mangled names and builds the
  `ScriptThread` vtable by hand. Natives are called by joaat hash.
- **Loader and core.** The `.asi` owns the script thread, the log and the
  D3D hook. It forwards every tick and frame to the core DLL, and swaps in a
  new core when the file changes.
- **Camera.** A scripted camera follows libfpv's camera. The rotation is
  converted to the game's Euler order (heading, then roll, then pitch). A
  near plane is chosen from frustum probes so that the road stays visible
  under a low lens.
- **World collision.** libfpv's raycast callback goes to the engine's physics
  probe (found by reverse engineering 1.2.0.59). A hit is mapped back to its
  ped, vehicle or object through the entity pools.
- **Drawing.** ScriptHook's D3D9 hook calls `src/render.cpp` after the game
  finishes a frame. For the fisheye, the back buffer is copied to a texture
  and drawn back through a 64x36 grid warped with libfpv's Brown-Conrady
  parameters, so no shader compiler is needed. The FOV is widened for the
  real screen aspect so that the edges still show the FOV you chose. The OSD
  and the overlay are drawn on top as textured triangles.
- **Audio.** A waveOut thread plays `fpv_audio_read`.

`MODLOG.md` is the development journal. It records addresses, decisions and
what was verified in game.

## Testing without hands

`GTAIV/fpv-data/autotest.txt` turns on a scripted test. It moves Niko to a
road, launches the drone and flies a fixed stick sequence, logging
everything. Knobs: `car`/`ped` (crash target), `hover`, `still`, `cam`
(camera-axis test), `niko` (end with Move Niko here), `secs=`, `fov=`,
`near=`, `osd=`, `osdoff` (fly with the OSD disable switch on), and with a
kamikaze libfpv `boom` (arm the warhead in flight and hit the target),
`boomsw` (air burst with the explode switch), and `signal`/`cutscene` (force
an ending). **Delete the file afterwards.** Otherwise every game start
runs the test.

Creating `GTAIV/fpv-data/shot.txt` saves the next finished frame, with the
mod's own drawing, to `GTAIV/fpv-data/shots/<time>.bmp`. This works when
window capture can't see the game.

## Credits

- [libfpv](https://github.com/hammerill/libfpv): the drone, Betaflight
  (through SimITL), OSD, overlay and sound.
- Aru: the GTA IV C++ ScriptHook. ThirteenAG: FusionFix and the ASI loader.
- Written with Claude (Anthropic).

## License

GPL-3.0-or-later, the same as libfpv (which embeds Betaflight). See
[LICENSE](LICENSE) for the full text.
