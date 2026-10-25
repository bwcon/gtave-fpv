# GTA V Enhanced FPV: roadmap

This fork ports [gtaiv-fpv](https://github.com/hammerill/gtaiv-fpv) from GTA IV
to GTA V Enhanced. The drone is simulated by
[libfpv](https://github.com/hammerill/libfpv), which runs real Betaflight
firmware. The fork of libfpv used for the HD OSD work is
[bwcon/libfpv](https://github.com/bwcon/libfpv).

## What carries over from gtaiv-fpv

- The two-module layout: a thin `.asi` loader owns the script thread. If the
  core DLL file changes, the loader hot-reloads it.
- `src/fpvapi.cpp` loads `libfpv.dll` at runtime through a function table. The
  host and libfpv can therefore use different compilers (MinGW for libfpv,
  MSVC or MinGW for the mod).
- Drone, camera and world logic in `src/drone.cpp` and `src/world.cpp`, adapted
  to GTA V natives.

## What changes

| Area | GTA IV | GTA V Enhanced |
|---|---|---|
| Architecture | 32-bit (i686) | 64-bit (x86_64) |
| Script host | ScriptHook (IV), found by mangled names | ScriptHookV SDK |
| Collision probe | Reverse-engineered engine probe | `START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE` |
| Graphics | D3D9 hook from ScriptHook | D3D12, drawn through a ReShade addon |
| Fisheye | Back buffer copied and warped on a grid | ReShade `.fx` shader |

## Phase 0: spikes

1. Build libfpv for Windows x86_64 and run `examples/standalone` with a radio
   or gamepad.
2. Make sure that Betaflight settings persist after a restart.
3. Build a ScriptHookV ASI that runs one synchronous probe under the player and
   logs the hit.
4. Make the same ASI register as a ReShade addon and draw one ImGui text box.

Exit check: all four work in the game.

## Phase 1: first flight

1. Port the loader and core to 64-bit and ScriptHookV.
2. Initialize libfpv with a Z-up coordinate preset in metres.
3. Answer the libfpv raycast callback with the synchronous probe. Call
   `fpv_drone_tick` from the script tick, because natives are legal only on the
   script thread.
4. Make a scripted camera follow `frame.camera`.
5. Call `SET_FOCUS_POS_AND_VEL` on the drone every tick, so that collision and
   the map stream around the drone, not the player.

Exit check: the drone arms, flies and bounces off a wall.

Keys: F9 launch and land (F7 belongs to AddonSpawner), C camera, R respawn,
Y move the player to the drone. The radio setup comes from `fpv-data` in the
game folder, copied from the standalone example's `data` folder.

## Phase 1b: libfpv settings in the ReShade tab

The ReShade tab "GTAVE.FPV" hosts the full libfpv overlay (setup wizard,
rates, OSD layout), so that no settings live in a second interface.

1. Upload the libfpv font textures through the ReShade device and use the
   resource views as ImGui texture IDs.
2. Copy `fpv_overlay_render` draw lists into the tab's ImGui draw list. The
   libfpv vertex layout matches `ImDrawVert`.
3. Forward the tab's mouse, wheel, keys and text to the `fpv_overlay_*` input
   functions.
4. Re-enable the overlay keys (F8, Esc, Tab) in `forward_keys`.

## Phase 2: visible drone and OSD

1. Spawn a drone prop. Turn off its collision and exclude it from probes.
2. Add FPV, chase and line-of-sight cameras.
3. Draw the Betaflight OSD with libfpv draw data in the ReShade overlay.
4. Add `OsdMode=Analog|DJI` with a hotkey. Analog uses the 4:3 analog style.
   DJI uses the 16:9 HD style.
5. Ship OSD layouts as Betaflight CLI scripts, applied with `fpv_cli_exec`.
   Betaflight Configurator connects with a Manual connection to
   `ws://localhost:5761`.

## Phase 3: feel and look

1. Fisheye lens through a ReShade `.fx` shader.
2. DJI goggles HUD: signal bars, bitrate, latency, recording dot and timer.
3. Video look for each mode: analog noise and breakup, or digital freeze and
   pixelation.
4. Turn off DLSS frame generation while flying, because it adds latency.
5. A custom drone model in an onigiri dlcpack, so that it can change without an
   RPF repack.

## Phase 4: collision extras

1. libfpv already casts 15 rays per tick: 6 axes, 8 diagonals and one along
   the velocity that reaches `max(2 m, speed x 0.3 s)` ahead
   (`refreshRaycastProxies` in libfpv `src/frame.cpp`). If tests show the
   drone passing through thin geometry, such as poles beside the flight path,
   add extra rays.
2. Dynamic collision objects for vehicles, so that the drone can land on
   moving cars.
3. Optional: export `.ybn` collision for one area with CodeWalker.Core and load
   it as a fixed race track.
4. Slatted fences collide as one flat panel. The probe sees the mover bounds
   that peds and vehicles use, and those are often a solid box. First test
   whether a bullet passes through a gap. If it does, GTA has more detailed
   weapon bounds for that fence, and the export in item 3 can use them. If it
   does not, no collision source in the game has the gaps.

## Phase 5: full HD OSD (libfpv fork)

The libfpv OSD grid is fixed at 16x30, the analog PAL canvas. DJI O3 and O4
with Betaflight use MSP DisplayPort on a 53x20 HD canvas.

1. In bwcon/libfpv, make the OSD grid size follow the Betaflight video system.
2. Turn on the HD canvas (`vcd_video_system = HD`) in the embedded Betaflight
   build.
3. Custom OSD fonts, loaded from the same files that go on the goggles' SD
   card, without changes. Each package keeps its own folder in
   `%APPDATA%\libfpv\fonts`, and the overlay lists them.
   - WTFOS (msp-osd) fonts: `font_bf.bin`, `font_bf_2.bin`,
     `font_bf_hd.bin`, `font_bf_hd_2.bin`. These are raw RGBA with no header,
     256 glyphs per file, and the `_2` file holds glyphs 256 to 511. The SD
     glyphs are 36x54 px (1,990,656 bytes per file). The HD glyphs are
     24x36 px (884,736 bytes per file).
   - DJI O3 native fonts: PNG sheets such as `BTFL_CLASH_SD.png`,
     `BTFL_CLASH_HD.png` and `BTFL_CLASH_O3.png`. Decode them with
     `stb_image`. Make sure of the glyph layout from Ben's files first:
     `BTFL_CLASH_HD.png` is 96x9216.
   - Use the HD files on the 53x20 canvas and the SD files on 30x16. Color
     fonts keep their colors, because libfpv draws fonts as RGBA textures.
   - Ben's fonts: `a personal folder`. His favourites are the SNEAKY_FPV O3 sets (Blinder,
     Clash, Conthrax, Europa, Nexus, in BTFL and ARDU variants).
4. Offer the change upstream to hammerill/libfpv.

## Phase 6: GTA world telemetry in the OSD

The game data goes into the Betaflight OSD. A stock sim dongle such as the
FrSky XSR-SIM only sends its own RSSI and RxBt back to the radio. Telemetry
on the radio screen therefore needs new hardware (see the backlog).

1. Area name: `GET_NAME_OF_ZONE` gives the zone code, and `GET_LABEL_TEXT`
   gives its display name ("Vinewood Hills", "Mount Chiliad"). Show it in a
   Betaflight "Custom message" OSD element. libfpv already writes Custom
   message 1 for kamikaze builds. The fork exposes this as an API.
2. Real-looking GPS: libfpv places the world at 0 N 0 E, so latitude and
   longitude are hidden. Add a GPS origin to the libfpv fork, and anchor Los
   Santos on Los Angeles coordinates.
3. Checkpoint races: GTA checkpoints and blips as gates, timed with
   Betaflight's GPS lap timer.
4. Flight plans: Betaflight 2025.12 (the version in libfpv) has no waypoint
   missions. The autopilot flight plan first appears in Betaflight 2026.6
   release candidates, so this waits for libfpv to update Betaflight.

## Backlog (after the base mod works fully)

Shelved on 2026-10-25. When Phases 2 to 5 are done, start these.

1. Live font uploads from the Betaflight Configurator Font Manager: catch
   `MSP_OSD_CHAR_WRITE` (Betaflight `msp.c`) and save the font. Do this after
   the Betaflight 2026.6 upgrade. Goggle font files are part of Phase 5.
2. Telemetry on the radio screen. A stock FrSky XSR-SIM has no path from the
   PC to the radio. Options:
   - A Raspberry Pi Pico between the PC and a FrSky receiver with SBUS and
     SmartPort: a USB joystick plus cached SmartPort sensors fed by the mod
     (the TeleJoy design). An EdgeTX Lua script turns a zone number into the
     area name, because SmartPort carries numbers only.
   - A custom sim dongle on an ESP32 running ExpressLRS. CRSF telemetry can
     carry text, such as the area name, without a Lua script.
   - A trainer-port cable into the XSR-SIM. This gives wired sticks, but the
     trainer signal is one-way, so it adds no telemetry.
   - Custom XSR-SIM firmware. Hardest: the board is undocumented, flashing
     can need SWD pads, and the FrSky ACCST D16 receiver side must be
     reimplemented.
