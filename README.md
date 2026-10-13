# GTA IV FPV

![FPV view over a Broker street, with the fisheye lens and the Betaflight OSD](docs/fpv-view.jpg)

Fly an FPV drone through Liberty City with your own radio. Press F7 and Niko
launches a quad from the street in front of him. You fly it from the goggles
view. It is simulated by [libfpv](https://github.com/hammerill/libfpv), which
runs real Betaflight firmware, so rates, angle mode, arming and turtle mode
behave as they do on a real quad.

## Features

- **Real Betaflight** in the loop, with your rates, modes and tuning. Set up
  your radio once in the F8 overlay's wizard. Betaflight Configurator can
  connect too.
- **Three cameras**: FPV (the goggles view, with a fisheye lens), chase and
  line of sight from where Niko stands.
- **Betaflight OSD** in the FPV view: battery, cell voltage, current, flight
  timer and mode.
- **Collides with the world**: buildings, the road, cars, peds and props, all
  through the engine's own physics probe. Crashes break props and burn
  motors.
- **Crash effects**: a fast hit knocks a ped over (ragdoll and damage), dents
  a car or shoves a prop.
- **Motor sound** from libfpv's synthesizer.
- **Clean view**: the minimap and HUD are hidden while you fly.
- **Move Niko here**: ends the flight with Niko standing where the drone is,
  facing the way it faced.

## Controls

| Key | Action |
|---|---|
| F7 | Launch the drone / return to Niko |
| F8 | Settings overlay (radio setup, rates, OSD, camera, fisheye strength) |
| C | Cycle cameras: FPV, chase, line of sight |
| R | Respawn the drone at its launch point, repaired |
| N | Move Niko here (also a button in F8 > Fly) |

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

Copy into the `GTAIV/` folder, next to `GTAIV.exe`:

| File | What it is |
|---|---|
| `GTAIV.FPV.asi` | Loader that the ASI loader picks up |
| `GTAIV.FPV.core.dll` | The mod itself (hot-reloaded by the loader) |
| `libfpv.dll` | libfpv, i686 build |

Settings, radio mappings and Betaflight EEPROMs go in `GTAIV/fpv-data/`. The
log is `GTAIV/GTAIV.FPV.log`.

Launch the game with `GTAIV/PlayGTAIV.exe`. If Steam updates the game, it may
replace modded files.

## Building

The build needs CMake 3.24 or newer and the i686 MinGW-w64 toolchain
(`i686-w64-mingw32-g++-posix`). It cross-compiles from Linux or WSL.

```sh
# libfpv, 32-bit (from the libfpv checkout)
cmake -S . -B build/win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake
cmake --build build/win32 -j

# this mod
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake
cmake --build build
./deploy.sh   # copies the asi, the core and libfpv.dll into ../GTAIV
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
`near=`, `osd=`. **Delete the file afterwards.** Otherwise every game start
runs the test.

## Credits

- [libfpv](https://github.com/hammerill/libfpv): the drone, Betaflight
  (through SimITL), OSD, overlay and sound.
- Aru: the GTA IV C++ ScriptHook. ThirteenAG: FusionFix and the ASI loader.
- Written with Claude (Anthropic).
