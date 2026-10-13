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

## Next session — start here
1. Ask the user to launch (PlayGTAIV.exe), load a save, press F7, try arming/flying (radio or keyboard), C, R, F7 back.
   Then read GTAIV.FPV.log. Check: libfpv init OK, input source, camera follows, axes right (yaw/pitch/roll directions,
   especially roll sign), drone sits on the ground instead of falling through.
2. Fix whatever is off. Possible issues: scripted cam needs SET_CAM_NEAR_CLIP; FOV range; Niko visible in FPV view.
3. Then: real world raycast (walls/buildings) — find an engine probe (e.g. the physics line test used by
   IS_LINE_OF_SIGHT_CLEAR / ped shooting) and call it from the raycast callback.
4. Later: prop body, OSD/overlay via D3D9 EndScene/Present hook (DXVK is the d3d9.dll; ScriptHook ID3DHookService may help),
   audio, crash effects on peds/vehicles.
5. Menu automation notes: mouse clicks don't reach the CE picker; prefer the user launching.
