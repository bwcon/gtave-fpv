# GTA V Enhanced FPV

![The FPV view](docs/fpv-view.jpg)

Fly an FPV drone through Los Santos with your own radio. Press F9 and a quad
launches from the street in front of you. The drone runs on
[libfpv](https://github.com/hammerill/libfpv), which embeds real Betaflight
firmware. Rates, angle mode, air mode, arming and turtle mode work as they do
on a real quad.

This mod is a port of [gtaiv-fpv](https://github.com/hammerill/gtaiv-fpv) by
hammerill from GTA IV to GTA V Enhanced. It uses a fork of libfpv,
[bwcon/libfpv](https://github.com/bwcon/libfpv), which adds Betaflight
2026.6, the HD OSD canvas and goggle fonts. It is an early release. See
[ROADMAP.md](ROADMAP.md) for the plan and the open items.

## Features

- Real Betaflight in the loop, with your rates, modes and tuning. Set up your
  radio once in the setup wizard. Betaflight Configurator can connect too.
- Three cameras: FPV (the goggles view), chase, and line of sight from where
  the player stands.
- Betaflight OSD in two styles: the analog 30x16 canvas, or the DJI-style HD
  53x20 canvas. A goggles HUD shows signal bars, the flight timer and a
  recording dot. It adds bitrate and latency in the HD style, and the video
  channel in the analog style.
- Goggle fonts: copy the font folders from your goggles' SD card (DJI O3 PNG
  sheets or WTFOS `.bin` fonts) and pick one in the OSD settings.
- Goggles view: a fisheye lens, a goggles bezel around the picture, and a
  video look for each style. Analog has noise, tearing and breakup. Digital
  has block artifacts and freezes. The lens shader works only in the FPV view.
- Radio signal: the OSD RSSI and RSSI dBm drop with the distance to where you
  pressed F9, and behind buildings.
- GPS home is where the drone arms, as on a real quad.
- Collision with the world: buildings, the road, cars, peds and props, through
  the game's shape test probes.
- Motor sound from the libfpv synthesizer.
- The in-game radio plays while you fly. Change the station with `,` and `.`.
- FPV DVR: NVIDIA ShadowPlay records while the drone is armed, and stops 2
  seconds after you disarm.
- Move the player here: ends the flight with the player standing where the
  drone is.
- Checkpoint races: record gates in flight with a radio switch, then fly
  timed laps through them.

## Controls

| Key | Action |
|---|---|
| F9 | Launch the drone, or return to the player |
| C | Cycle cameras: FPV, chase, line of sight |
| R | Respawn the drone at its launch point, repaired |
| Y | Move the player to the drone |
| O | Switch the OSD style: analog or HD |
| G | Show or hide the goggles bezel |
| P | New propellers, where the drone is |
| `,` and `.` | Previous or next radio station |
| N | Start recording a race, or save the recorded race |
| M | Mark a gate while recording (the AUX3 switch does the same) |
| U | Remove the last gate while recording |
| L | Load the next saved race, or turn races off |

All libfpv settings (radio setup, rates, OSD layout, fonts, camera and quad)
are in ReShade. Press Home, open the Add-ons page, and expand GTAVE.FPV. The
settings show right under it.

The goggles view has its own settings in ReShade's Home page. Select the
"GTAVE.FPV lens" effect, and the settings show below the effect list:

- Goggles: the goggles frame on or off (G does the same in the game).
- Goggles size: 1 fills the window. A smaller value shows black around the
  goggles, and a larger value enlarges the picture.
- Camera FOV (vertical): the same setting as the Camera page in the GTAVE.FPV
  settings.
- Video signal: Auto (analog with the analog OSD, digital with the HD OSD),
  Analog, Digital, or Clean (no interference).

Without a radio, the keyboard works as a fallback. Enter arms, W and S are
throttle, A and D are yaw, and the arrow keys are pitch and roll. A gamepad
works too.

## Requirements

- GTA V Enhanced (the 2025 PC edition), story mode only.
- [ScriptHookV](http://www.dev-c.com/gtav/scripthookv/) with GTA V Enhanced
  support, and its ASI loader (`dinput8.dll`).
- [ReShade](https://reshade.me) 6.x with full add-on support (the "with full
  add-on support" installer), installed for GTA V Enhanced with DirectX 12.
- Windows 10 or 11.
- Optional: an FPV radio in USB joystick mode, and the NVIDIA app for the DVR.

Turn off DLSS frame generation for FPV flying. Frame generation adds input
latency, and no script function can switch it off.

The mod was tested together with the RenoDX DLSS 5 mods and the NVE
(NaturalVision Enhanced) mod. Keep the "GTAVE.FPV lens" effect last in
ReShade's effect list, after the NVE effects.

## Installing

Copy these files into the game folder, next to `GTA5_Enhanced.exe`:

| File | Where it goes |
|---|---|
| `GTAVE.FPV.asi` | The game folder. The ASI loader loads it. |
| `GTAVE.FPV.core.dll` | The game folder. This is the mod itself. |
| `libfpv.dll` | The game folder. This is the drone simulator. |
| `GTAVE_FPV_Lens.fx` | `reshade-shaders\Shaders` |
| `GTAVE_FPV_Bezel.png` | `reshade-shaders\Textures` |

Then do these steps:

1. Start the game and load story mode.
2. Press Home to open ReShade, and turn on the "GTAVE.FPV lens" effect. Keep
   it on: the mod shows the lens only in the FPV view.
3. Press F9 to launch the drone.
4. Open the GTAVE.FPV settings (ReShade's Add-ons page) and follow the radio
   setup wizard once.

If you turn the effect off and ReShade has "Load only enabled effects" set,
ReShade skips the effect on the next start, and the mod cannot turn it on.
Turn it on again in the Home page.

### Goggle fonts

1. Make the folder `%APPDATA%\libfpv\fonts`.
2. Copy each font package into it as its own folder, with the files unchanged.
   For example, `fonts\SNEAKY_FPV_O3_CLASH_BTFL\BTFL_CLASH_O3.png`.
3. In the GTAVE.FPV settings, open the OSD page and pick the font in the Font
   list.

The mod uses these files:

- DJI O3 fonts: `*_O3.png` or `*_HD.png` for the HD style, and `*_SD.png` for
  the analog style. Use the Betaflight (BTFL) variants.
- WTFOS (msp-osd) fonts: `font_bf_hd.bin` for the HD style, and `font_bf.bin`
  for the analog style.

### Files the mod writes

- `%APPDATA%\libfpv`: libfpv settings, radio mappings and Betaflight
  settings. The standalone libfpv app and other libfpv games share this
  folder.
- `GTAVE.FPV.ini` in the game folder: the drone model, the bezel opening and
  the DVR mode. A crash bends the propellers. By default the mod fits new
  ones when you disarm, and keeps the battery as it is.

The GTAVE.FPV settings have three flight options:

- Full realism: bent props stay bent until P or R (`[Drone] NewPropsOnDisarm=0`).
- Repair on arm: every arm gives new props, cool motors and a full battery
  (`[Drone] RepairOnArm=1`).
- New GPS home point at every arm: on by default. Turn it off to keep the
  first arm's home for the whole flight. This is Betaflight's
  `gps_set_home_point_once` setting. Set `[DVR] Mode=off` to turn off the ShadowPlay recording.
- `GTAVE.FPV.log` in the game folder: the mod log.

## Checkpoint races

A race is a loop of gates. The first gate is the start and finish line.

1. Fly to where the start line should be, and press N to start recording.
2. Fly the course. At each gate position, flick the AUX3 switch (the beeper
   switch) or press M. The gate faces the way the drone flies at that moment.
3. If a gate is in the wrong place, press U to remove it.
4. Press N to save the race. The race needs two gates or more.
5. Fly through the green gate to start the clock. The next gate is yellow.
   Each lap ends at the start gate, and the HUD shows the lap, the last lap
   time and the best lap time.

Races are saved as text files in the `fpv-data\races` folder in the game
folder, one gate per line. You can share them or edit them by hand. Press L to load the
next saved race, and press R to restart the lap from the start gate.

Race settings in `GTAVE.FPV.ini`, section `[Race]`:

- `MarkSwitch`: the AUX number of the gate mark switch (default 3).
- `GateRadius`: the gate radius in metres (default 2.0).
- `MarkerYaw`: degrees added to the drawn rings' rotation, if the rings do
  not face along the course.

## Tune it in Betaflight Configurator

The drone runs real Betaflight. You can set PIDs, filters, rates, modes and
the OSD layout in [Betaflight Configurator](https://app.betaflight.com).

1. In the game, launch the drone with F9.
2. In Configurator, open Options and turn on manual connection mode.
3. Select Manual Selection, enter `ws://localhost:5761` as the port, and
   connect.

For the HD OSD layout, set the video format to HD in the OSD tab. You can
place elements across all 53 columns.

## FPV DVR

If `[DVR] Mode=shadowplay` (the default), the mod presses Alt+F9 when you arm
and again 2 seconds after you disarm. Alt+F9 starts and stops a manual
recording in the NVIDIA app. Make sure that the NVIDIA app overlay is on and
that Alt+F9 is its record shortcut. The clips go to the NVIDIA app's video
folder. GTA's own Rockstar Editor cannot record the mod's camera.

## Building

The build uses an MSYS2 MINGW64 shell (x86_64 MinGW-w64 GCC, CMake and
Ninja).

```sh
# libfpv (from the bwcon/libfpv checkout next to this one)
cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-fpermissive
cmake --build build/win

# this mod
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGTAVE_DIR="C:/path/to/GTA V Enhanced"
cmake --build build
cmake --build build --target deploy
```

The `deploy` target copies the mod, `libfpv.dll`, the shader and the bezel
into the game folder. If the game is running, the core DLL reloads within
about a second. The `.asi` and `libfpv.dll` stay locked until the game exits.

## How it works

- ScriptHookV from MinGW: the mod finds the ScriptHookV exports by their
  mangled names. Natives are called by hash on the script thread.
- Loader and core: the `.asi` owns the script, the log and the ReShade
  add-on registration. It forwards every tick and frame to the core DLL. When
  the core file changes, the loader swaps in the new core.
- Camera: a scripted camera follows the libfpv camera. The focus position
  follows the drone, so the map and collision stream around it.
- World collision: the libfpv raycast callback goes to synchronous shape test
  probes. A hit is mapped back to its ped, vehicle or object.
- Drawing: the OSD and the libfpv overlay are ImGui draw lists in the ReShade
  overlay. The lens, bezel and video look are one ReShade effect,
  `GTAVE_FPV_Lens.fx`. The mod sets its values every frame.

`MODLOG.md` is the development journal. It records addresses, decisions and
what was tested in the game.

## Credits

- [hammerill](https://github.com/hammerill): libfpv and gtaiv-fpv, which this
  mod is built on.
- [Betaflight](https://github.com/betaflight/betaflight) and SimITL: the
  flight controller firmware and its simulator target.
- Alexander Blade: ScriptHookV. crosire: ReShade.
- [stb_image](https://github.com/nothings/stb) by Sean Barrett: the PNG font
  loader.
- Written with Claude (Anthropic).

## License

GPL-3.0-or-later, the same as libfpv (which embeds Betaflight). See
[LICENSE](LICENSE) for the full text.
