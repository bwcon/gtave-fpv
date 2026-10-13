# GTA IV FPV drone — MODLOG

## Goal
FPV drone mode in GTA IV (Complete Edition, Steam 12210) powered by libfpv (~/Projects/libfpv).
Done = fly the drone in game with a radio: FPV/chase/LOS cams, OSD, motor sound, F8 overlay,
world collision, impacts that ragdoll/damage peds and vehicles.

## Environment (recon 2026-10-13)
- Game: /mnt/d/SteamLibrary/steamapps/common/Grand Theft Auto IV/GTAIV/GTAIV.exe
  - 1.2.0.59, PE32 (i386), D3D9, md5 1a47b45f9e254a8dec8a347ece43c8ac
  - No anti-cheat, no loaders installed (vanilla). Launches via Steam -> Rockstar Games Launcher.
- Profiles/saves: C:/Users/Hammerill/Documents/Rockstar Games/GTA IV/Profiles/FA15D3B0
- Settings: C:/Users/Hammerill/AppData/Local/Rockstar Games/GTA IV/Settings
- libfpv: ~/Projects/libfpv (C API, include/libfpv.h). Only an x64 MinGW toolchain exists.
- Toolchains present: i686-w64-mingw32-{gcc,g++-posix}, x86_64-w64-mingw32-gcc.
- KB: no prior field notes for GTA IV or FPV.

## Decisions (agreed with user)
1. 32-bit bridge: add an i686 MinGW toolchain to libfpv and load libfpv in-process.
2. Loader stack: FusionFix (ASI loader, dinput8.dll) + Tomasak's .Net ScriptHook pack (user-installed;
   ships Aru's C++ ScriptHook.dll 0.5.1, which auto-detects 1.2.0.59). Mod is a native .asi.
3. Drone body: a small vanilla prop; collision via the game's world probe through
   fpv_world_set_raycast_callback.
4. Extras in v1: crash effects on peds/vehicles (ragdoll + damage). No fisheye, no kamikaze, no clip.
5. Repo: Grand Theft Auto IV/gtaiv-fpv (this folder). Deploy copies build output into GTAIV/.
6. Toggle: hotkey (default F7, configurable .ini) launches drone from Niko; Niko frozen; same key returns.
7. Input: user's radio via libfpv SDL. Automated testing allowed (check user idle first).

## Plan
1. Backups: game folder file list + profiles/settings (um backup).
2. libfpv i686 build (toolchain file, fix 64-bit assumptions), run tests under wine if possible.
3. Install FusionFix + ScriptHook (current releases), verify a hello .asi loads.
4. Vertical slice: hotkey -> scripted camera follows libfpv pose, player frozen.
5. Raycast callback via game world probe (RE needed), impacts -> ragdoll/damage.
6. Prop body, OSD + overlay renderer (D3D9), audio (libfpv -> game/own output).

## Log
- 2026-10-13: Backups: ~/.universal-modder/backups/gtaiv-profiles/20261013-185132.zip,
  gtaiv-settings/20261013-185133.zip. Vanilla GTAIV/ root files hashed in vanilla-root-files.md5
  (restore = delete anything in GTAIV/ not listed there, e.g. dinput8.dll, *.asi, plugins/).
- 2026-10-13: libfpv i686 build works. Branch `win32-i686` in ~/Projects/libfpv (commit cd10d3a):
  cmake/toolchains/mingw-w64-i686.cmake, build dir build/win32. Needs `-msse2 -mfpmath=sse`
  (x87 default made wall_crash tunnel through the wall). 20 tests / 321 checks pass under
  Windows via WSL interop (copy libfpv.dll next to tests/fpv_tests.exe first, else exit 53).
  Note: build/win (x64) tests dir holds a stale libfpv.dll -> 30 bogus failures; unrelated.
  Next: install FusionFix + Zolika ScriptHook, hello-world .asi.
- 2026-10-13: User installed FusionFix + Tomasak's ScriptHook pack. New files in GTAIV/:
  dinput8.dll (ASI loader), d3d9.dll + vulkan.dll + d3d9.cfg (DXVK from FusionFix), plugins/GTAIV.EFLC.FusionFix.{asi,ini,cfg},
  update/, ScriptHook.dll, ScriptHookDotNet.asi, aCompleteEditionHook.asi, scripts/. scripthook.log (19:15 run, user's
  test) shows "Using game version Dynamic (1.2.0.59)" and ScriptHookDotNet initialised -> the hook works on this exe.
- **User launches with GTAIV/PlayGTAIV.exe, NOT Steam** (a Steam update could wipe the mod). Do the same.
- ScriptHook API: Aru's SDK headers cloned to ~/gtaiv-ref/gtaiv-scripthook-sdk (github violetfrost/gtaiv-scripthook-sdk;
  reference only, Aru's license, don't commit). ScriptHook.dll exports MSVC C++ only, so src/scripthook.{h,cpp} is a
  MinGW shim: GetProcAddress on mangled names, ScriptThread built by hand (ctor ??0ScriptThread@@QAE@XZ via thiscall,
  copy its vtable incl. RTTI slot [-1], override [0] RunTick and [4] scalar deleting dtor; vtable order verified from
  ??_7ScriptThread@@6B@: RunTick, RunScript, OnStart, OnKill, dtor). Register with
  ?RegisterThread@ScriptHookManager@@SAXPAVScriptThread@@@Z (cdecl). Natives via
  ?SpeedyInvoke@NativeInvoke@@CAXPAVNativeContext@@I@Z (cdecl, ctx layout in scripthook.h).
  Native hash = joaat(lowercase name) (verified: get_player_id = 0x1cc648ea matches SDK). Macro N("NAME").
  ScriptHook also exports ID3DHookService / IKeyboardHookService (vtables dumped with
  scratch script; not used yet; could be useful for OSD drawing).
- Hello-world .asi built (build: `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake && cmake --build build`,
  deploy: ./deploy.sh -> GTAIV/GTAIV.FPV.asi). Writes GTAIV/GTAIV.FPV.log ("attached", "ScriptThread registered",
  "first script tick"); F7 logs Niko's coords and prints a help text. **NOT YET VERIFIED IN GAME.**
- Added GTAIV/commandline.txt (-windowed -width 1280 -height 720) for stable screenshots. Undo: delete it.
- Testing attempts (2026-10-13 ~19:25):
  - `um win launch <exe>` timed out (cmd.exe started from a /mnt/d cwd); launch from /mnt/c instead:
    `cd /mnt/c && cmd.exe /c start "" "D:\...\GTAIV\PlayGTAIV.exe"`. `um win setup` done (ffmpeg gfxcapture, h264_qsv).
  - Game opened the CE episode picker (GTA IV / EFLC), still 1920x1080 fullscreen (commandline.txt not applied
    at that stage?). Screenshots come out 1920x1080 regardless of --scale. Window rect 0 0 1920 1080.
  - WinDrive `click` did nothing (cursor didn't move); `scanmode on` + `key 0x0D` sent but no visible progress.
  - **No .asi loaded at the picker**: scripthook.log / ScriptHookDotNet.log not rewritten, no GTAIV.FPV.log.
    User stopped the game twice after ~2 min of nothing. Unknown whether Enter started loading.
  - User asked me to stop and continue in a later session.

- 2026-10-13 22:27: **Hello-world VERIFIED in game** (user launched PlayGTAIV.exe, loaded save, F7): GTAIV.FPV.log shows
  attached -> ScriptThread registered -> first script tick -> "F7: player 0 ped 2 at 601.42 1409.84 17.48". Root *.asi load fine.
- Vertical slice written (src/drone.{h,cpp}), built, deployed, NOT yet tested in game:
  - libfpv.dll loaded lazily (LoadLibrary from GTAIV/) on first F7; data_dir GTAIV/fpv-data; log -> GTAIV.FPV.log.
  - coords: right +X, up +Z, forward +Y, 1 unit = 1 m (GTA IV world: X east, Y north, Z up).
  - F7: drone spawns 2 m in front of Niko on GET_GROUND_Z, yaw = Niko heading; SET_PLAYER_CONTROL off;
    CREATE_CAM 14 + ACTIVATE_SCRIPTED_CAMS; each tick SET_CAM_POS/ROT/FOV from frame.camera.
    Euler: heading = atan2(-fx, fy), pitch = asin(fz), roll = atan2(-right.z, up.z) — **roll sign unverified**.
  - Keys (only when game window focused): libfpv keyboard fallback W/S/A/D/arrows/Enter/T/B; C cycles FPV/chase/LOS;
    R resets to spawn + repair; F7 back to Niko (destroys cam, restores control, deletes drone).
  - Collision: raycast callback answers only downward rays (dir.z < -0.5) via GET_GROUND_Z_FOR_3D_COORD. No walls yet.
  - Log line every 2 s: drone pos, flags, altitude, throttle, arming-disable text.
  - deploy.sh now also copies ~/Projects/libfpv/build/win32/libfpv.dll to GTAIV/.

- 2026-10-13: **Slice VERIFIED in game** by the user: flies, camera follows, roll sign correct. Radio "Arduino RaspberryPi
  Pico" detected as radio (log says "needs setup" but the user flies it fine). frame.altitude stays -1 (ground ray not
  answered for its query? check later).
- World raycast research: no raycast native in IV. IV-SDK .NET (~/gtaiv-ref/IV-SDK-DotNet, reference only) has
  CWorld::ProcessLineOfSight(CVector* from, CVector* to, uint32* unk=0, tLineOfSightResults* res, flags=142, 1, 0,
  seeThrough=2, 4), cdecl, returns bool; results 0x54 bytes: +0 phInstGta*, +0x10 end position, +0x20 / +0x30 vectors
  (likely normal), flags 2 static, 4 buildings, 8 vehicles, 32/64 peds, 128 objects. Addresses only for 1.0.7.0 (0x596D80)
  and 1.0.8.0 (0x52A400), not 1.2.0.59. Native hashes are not plain in GTAIV.exe (registration obfuscated), so
  added src/recon.cpp: on first tick, logs handler addresses via ScriptHook's Game::GetNativeAddressFromHash for
  GET_GROUND_Z_FOR_3D_COORD etc. and dumps the in-memory image to GTAIV/fpv-data/recon/gtaiv-image.bin (once).
  Plan: disassemble those handlers offline (capstone via `uv run`, script in scratchpad) to find the line test.

- 2026-10-13 22:43: recon ran (image base 0x700000, dump in GTAIV/fpv-data/recon, local only). Handlers (RVA):
  GET_GROUND_Z_FOR_3D_COORD 0x7944e0, GET_CHAR_HEIGHT_ABOVE_GROUND 0x79ee70, HAS_CHAR_SPOTTED_CHAR 0x79f6d0,
  IS_POINT_OBSCURED_BY_A_MISSION_ENTITY 0x7949f0, GET_WATER_HEIGHT_NO_WAVES 0x7de3a0.
  Chain (VA @0x700000): native 0xe944e0 -> 0xe962d0 -> FindGroundZ 0xd523f0(x,y,z,bool*,0,4) -> vertical probe
  0xd54510(CVector* start, float endZ, phInst* exclude, Intersection* out, typeFlags, ?) -> **general probe 0xa38880**,
  thiscall on *(void**)0x15b9c78 (phLevel), ret 0x20: (Segment* {vec4 a, vec4 b}, Intersection* out, phInst* exclude,
  typeFlags, -1, 7, 1, 0) -> nonzero on hit. Intersection 0x54: +0 phInst*, +0x10 position, +0x20/+0x30 vectors
  (normal candidates, logged for the first 10 hits), +0x40 t, +0x4c part (0xffff), +0x50 material byte.
  Disassembler: scratchpad gtadis.py (`uv run -q gtadis.py <dump> <va>`; don't name it dis.py).
- src/world.cpp: libfpv raycast callback through that probe, types 2|4|8|64|128. Signature-checked (prologue bytes +
  `mov ecx,[level]` at vertical probe +0x60); falls back to ground-only GET_GROUND_Z on mismatch. NOT yet tested.

- 2026-10-13 22:50-22:57: **World collision VERIFIED (self-tested, user asked me to test myself).**
  Probe sweep indoors (Niko's Broker apartment): walls 3-9 m, normals clean; nA == nB always (use either). Outdoors
  (road 585,1410,10): buildings ~12 m each side, open along the road. Drone: climbed, hit the apartment ceiling (n 0,0,-1);
  run 2 cruised into a building wall at x=597 (n -1,0,0), prop strike, crash, fell and landed on the street (n up).
  Rays include peds (type 64): a down ray from Niko's head hits Niko himself (inst differs). Fine for now.
- Radio: input source was KEYBOARD ("needs setup", overlay not drawn yet). Copied the user's sm64ex mapping
  (C:/Users/Hammerill/AppData/Roaming/sm64ex/libfpv-data/radio_mappings.json) into GTAIV/fpv-data -> source RADIO.
- **Self-test workflow** (src/autotest.cpp): touch GTAIV/fpv-data/autotest.txt -> 10 s later (also on game start if the
  file exists) it moves Niko to the nearest road (GET_CLOSEST_CAR_NODE + LOAD_SCENE), logs an 8-way + down probe
  sweep, launches the drone and flies via fpv_input_set_channels: arm at 2 s, altitude hold (+4 m target; hover is
  about thr -0.7 in ANGLE), pitch 0.25 from 7 s, stops at 20 s. **Delete autotest.txt before handing back to the user**
  (otherwise every game start teleports Niko). Loop: kill exact GTAIV pid (`um win ps`), ./deploy.sh, relaunch from /mnt/c
  with `timeout 10 cmd.exe /c start "" "D:\...\GTAIV\PlayGTAIV.exe"` (no episode picker appeared, loads straight
  into the save ~18 s), wait for "autotest: flying" in the log, `um win shot --exe GTAIV.exe x.png --scale 0.33`.
  Check `um win drive --proc explorer idle` first.
- Throttle note: 5" PropEater hovers near thr -0.7 (15% stick); thr 0.1 shot it to 700 m in 18 s.

- 2026-10-13 23:00-23:22: **Impacts on vehicles and peds VERIFIED (self-tested).**
  - Entity pools (1.2.0.59, VA @0x700000, `mov ecx,[pool]` sites signature-checked in world.cpp): peds *(0x1bb6f1c)
    (site 0xeb37b9 in GET_PLAYER_CHAR's helper 0xeb37b0), vehicles *(0x15e22a4) (0xea653c), objects *(0x1932c60)
    (0xd04664). Pool {objects, flags, count, entry_size}; handle = index<<8 | flags[index] (game fn 0x7b7020,
    handle->ptr 0x799d20, both thiscall on the pool). PlayerInfo+0x598 = CPed*, CPed+0xb30 = vehicle.
    phInst+0xC = CEntity* (IV-SDK layout holds). Verified: Niko -> ped handle 2 (= GET_PLAYER_CHAR).
  - fpv object_id = kind<<28 | handle (1 ped, 2 vehicle, 3 object, 4 building/other).
  - effects.cpp: ped >= 4 m/s -> SWITCH_PED_TO_RAGDOLL(ped,1000,3000,0,1,1,0) + DAMAGE_CHAR(speed*2) + APPLY_FORCE_TO_PED;
    car >= 5 m/s -> SET_CAR_HEALTH(-speed*3) + APPLY_FORCE_TO_CAR; objects -> APPLY_FORCE_TO_OBJECT. Cooldown 0.5 s per
    entity, started only when an effect applied (a 2 m/s graze first used to swallow the real hit). Player skipped.
  - Results: car 1000 -> 979 at 7.1 m/s; ped ragdoll=1, 200 -> 185 (178 after the fall) at 7.8 m/s.
  - Native hashes in GTAIV.exe registration are transformed (not joaat, not xor/add const); use ScriptHook's
    Game::GetNativeAddressFromHash at runtime (recon.cpp logs handlers each start).
- **Hot reload**: GTAIV.FPV.asi is now a loader (src/loader.cpp) owning the ScriptHook thread, the log ("w") and
  libfpv's log callback (export gtaiv_fpv_libfpv_log, so libfpv never calls into an unloaded core). It polls
  GTAIV.FPV.core.dll's timestamp every 30 ticks, calls the core's gtaiv_fpv_core_unload (stops drone + autotest),
  FreeLibrary, copies to GTAIV.FPV.core.live.dll and loads that. ./deploy.sh while the game runs = reload in ~1 s
  (the asi and libfpv.dll stay locked; deploy says "kept the running one").
- Launcher gotcha: killing GTAIV.exe leaves PlayGTAIV.exe + Launcher.exe thinking the game runs; the next launch only
  shows Rockstar's "A game is currently running" dialog. Kill those two by PID (tasklist) before relaunching.
  With hot reload, relaunches are rarely needed.
- Test script: scratchpad run_test.sh <car|ped> <shot-prefix> (hot reload if running, else cold start; reads only new
  log lines; 4 screenshots). Autotest now faces north on the Broker road (585,1410) with the target 16 m ahead,
  holds 0.7 m, pitch 0.5.

- 2026-10-13 23:28: **OSD + F8 overlay VERIFIED on screen (self-tested).** Loader registers a hand-built
  ID3DDeviceHook with ScriptHook's "D3DHook" service (RequestService export, AddDeviceHook = service vtable [5];
  hook vtable: OnCreateDevice, OnResetDevice, OnLostDevice, OnDestroyDevice, OnRender, all thiscall) on its first
  tick. OnRender (render thread) -> core gtaiv_fpv_core_render under a shared SRWLOCK; reload takes it exclusively and
  first asks the render thread to free the core's textures (event, 500 ms timeout). src/render.cpp: fixed-function
  XYZRHW|DIFFUSE|TEX1, DrawIndexedPrimitiveUP with INDEX32, managed A8R8G8B8 font textures (RGBA->BGRA, re-upload on
  revision), per-frame D3DSBT_ALL state block + RT/DS save (ImGui DX9 backend style). OSD (analog style) from a
  mutex-guarded frame copy, only in the FPV view. Overlay mouse from GetCursorPos on the device's focus window; libfpv
  draws its own cursor and has keyboard nav. Screenshots showed Betaflight OSD (25.7 V, 4.28 V/cell, timer, ANGL,
  amps) and the overlay's Fly tab (arming-disable reasons, motor/prop state).
- libfpv code moved to src/fpvapi.{h,cpp} (FPV_FUNCTIONS X-macro). F8 loads libfpv on foot too; while the overlay
  wants input on foot, SET_PLAYER_CONTROL off. Forwarded keys now also F8, Esc, Tab, Backspace, Space.
- Log fix: loader truncates once, then loader and core both append (loader "w" used to overwrite core lines).
- **F8 is stuck "down" system-wide on the user's PC** (GetAsyncKeyState 0x77 & 0x8000 from a separate process,
  scratchpad keys.exe -> C:/Users/Hammerill/AppData/Local/Temp/fpv-keys.exe). It opened the overlay at startup.
  Fix: edge detectors treat a key's first reading as a baseline (core.cpp/drone.cpp). Tell the user; pressing F8
  physically should clear it.

- 2026-10-13 23:35: Probes ignore the player (VERIFIED: sweep down ray from Niko's head now hits the road at 2.01 m).
  CEntity+0x38 = phInst (back-link checked), passed as the probe's `exclude`; that alone wasn't enough (peds have more
  instances, e.g. the ragdoll frag), so hits on the ignored entity restart the probe 25 cm further (max 8 times).
  Ped handle -> entity via the pool (index = handle>>8, flags must match). SET_CAM_NEAR_CLIP(cam, 0.05).
  autotest.txt deleted after testing.

- 2026-10-13 23:55: User reported flickering textures in drone mode + no drone sound.
  - Flicker: blamed on SET_CAM_NEAR_CLIP 0.05 (depth precision -> z-fighting); now kNearClip 0.2 in drone.cpp.
    Self-test screenshots at 0.2 look clean; an A/B at 0.05 was not finished. **User testing.** If it still
    flickers, next suspects: world streaming centred on Niko, not the camera; the D3D state block in render.cpp.
  - Audio was never implemented. src/audio.cpp: waveOut thread (44.1 kHz stereo s16, 4x512 frames) pulling
    fpv_audio_read; started on launch, stopped (joined) in drone stop before the drone is deleted. Log shows
    "audio: started", no waveOut error. Not heard yet. Scratchpad rec_test.sh can record game-only audio.

- 2026-10-14 00:05-00:40: User: FPV-only flicker (less at near 0.2), mirrored view on flips, see-through ground when
  landed; asked for an F8 option to turn off FOV/fisheye "post-effects". All fixed and self-tested:
  - **Flips**: cam test (autotest.txt "cam", GET_VIEWPORT_POSITION_OF_COORD on axes predicted by all 6 Euler orders)
    showed the game builds cameras as Rz(heading) Ry(roll) Rx(pitch), not Rz Rx Ry. drone::basis_to_euler now
    uses roll = asin(-r.z), pitch = atan2(f.z, u.z), heading = atan2(r.y, r.x) (knife-edge fallback). Pass 2 of the
    cam test VERIFIED all 12 cases (inverted, knife edge, past vertical) land forward/up/right correctly.
  - **Flicker**: measured with scratchpad tools/flicker.py (A-B-A pixel toggles in `um win record` clips). FOV and near
    clip made no difference; the OSD pass did: D3DSBT_ALL state block + SetRenderTarget/SetDepthStencilSurface(null)
    swap, even without drawing anything, made the next frame show stale regions and exposure jumps. render.cpp now
    draws on the bound back buffer (it always is: ~150 calls/s, back buffer bound) and saves/restores only the states it
    touches (SavedState). Parked-with-OSD toggles 1-6% (peaks 86%) -> 0.000; hover 0.000.
  - **See-through ground**: near plane (0.2, ±30 cm tall at this FOV) cut the road under a 7 cm-high lens. Now
    near_clip_for() probes 9 frustum rays (centre/edges/corners, aspect up to 2.4) and sets near = 0.8 x closest depth,
    clamped 0.02..0.2. Parked: near 0.079, road rendered to the bottom edge.
  - **FOV/fisheye**: the mod never drew libfpv's lens pass, but libfpv widened the FOV for it (100 -> ~112 vertical).
    New libfpv flag FPV_INIT_NO_LENS_DISTORTION (libfpv working tree, branch win32-i686, NOT committed; 20 tests pass):
    no widening, k1=k2=0, F8 > Camera shows "Fisheye lens: not drawn by this game". The FOV slider there still works.
    Possible later: real fisheye pass in D3D9 (StretchRect + pixel shader).
  - Audio recording check: flying clip RMS 0.043 vs parked 0.014 -> motor sound reaches the game's audio output.
  - autotest.txt knobs: car|ped|cam, still, hover, fov=, near=, osd=. Careful: a hot reload re-triggers the
    existing autotest.txt, so recordings lagged one run until scratchpad at.sh stopped deploying between runs.
  - **User wants the game closed after testing** (GTAIV + PlayGTAIV + Launcher by PID).

- 2026-10-14 00:50-00:58: User asked: commit, fisheye, "Move Niko here", hide HUD, README. All done and self-tested.
  - Committed the earlier work (mod f288204, libfpv a7d8960 on main; win32-i686 was merged into main by then).
  - **Fisheye VERIFIED**: render.cpp StretchRects the back buffer into a D3DPOOL_DEFAULT render-target texture
    (released on device lost) and draws it back through a 64x36 grid whose texcoords follow libfpv's Brown-Conrady
    formula (no shader compiler needed). Only in the FPV view, before the OSD/overlay. FPV_INIT_NO_LENS_DISTORTION
    is no longer passed. drone.cpp lens_fov() redoes libfpv's FOV widening for the real back-buffer aspect
    (render::aspect()) instead of the 4:3 aspect_hint: FOV 119.2 at 16:9. Single application per frame: the
    screenshot shows a moderate barrel warp (lamp poles bowed), not a doubled one.
    Known: PRINT_STRING help text is drawn by the game before our pass, so it gets warped/pushed to the bottom edge.
  - **HUD hidden VERIFIED**: HIDE_HUD_AND_RADAR_THIS_FRAME every drone tick (all views); no state to restore.
  - **Move Niko here VERIFIED** (button + key): libfpv got fpv_overlay_set_action(id, label) /
    fpv_overlay_poll_action() (libfpv 41c30f1, test added, 20 tests pass win32 + linux). Button "Move Niko here (N)"
    in F8 > Fly while flying; N key too. drone::move_niko_here(): probe down from the drone, stop(), LOAD_SCENE,
    SET_CHAR_COORDINATES (WARP_CHAR_FROM_CAR_TO_COORD if in a car) at ground + 1, heading = drone's,
    SET_CAM_BEHIND_PED. Clicked via WinDrive (`click 639 500` at 1920x1080) and `key 0x4E`; Niko moved 13 m.
  - New libfpv exports mean a libfpv.dll change needs a game restart (hot reload keeps the old dll).
  - autotest knobs: `niko` (end with Move Niko here), `secs=<flight seconds>`. WinDrive keys take hex VKs (`key 0x77`).
  - README.md + docs/fpv-view.jpg added.

## Next session — start here
0. Ask the user how the fisheye, HUD hiding and Move Niko here feel with the real radio (fisheye default strength?).
   Maybe draw the launch help text ourselves (the game's is warped by the lens) or show it in the OSD.
1. Check the overlay with real mouse use (GTA may clip/hide the cursor; GetCursorPos may stick) - needs the user.
2. Polish: frame.altitude -1 when > a few m up (libfpv probe range?); tune effect strengths with the user.
3. Prop body (visible drone model for chase/LOS cams), audio (fpv_audio_read -> XAudio2/waveOut).
4. Self-test with scratchpad run_test.sh (recreate from the MODLOG notes if the scratchpad is gone).
5. Delete GTAIV/fpv-data/autotest.txt before handing the game back to the user.
