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

## Phase 5: full HD OSD (libfpv fork)

The libfpv OSD grid is fixed at 16x30, the analog PAL canvas. DJI O3 and O4
with Betaflight use MSP DisplayPort on a 53x20 HD canvas.

1. In bwcon/libfpv, make the OSD grid size follow the Betaflight video system.
2. Turn on the HD canvas (`vcd_video_system = HD`) in the embedded Betaflight
   build.
3. Add an HD OSD font atlas.
4. Offer the change upstream to hammerill/libfpv.
