# Install GTA IV FPV

Fly an FPV drone through Liberty City with your own radio, gamepad or
keyboard. This takes about five minutes.

**[Download gtaiv-fpv.zip](https://github.com/hammerill/gtaiv-fpv/releases/latest/download/gtaiv-fpv.zip)**

## 1. Have the right game

You need **GTA IV: The Complete Edition, version 1.2.0.59**: the current
version on [Steam](https://store.steampowered.com/app/12210/). Older versions
(1.0.x, from the disc or from before the Complete Edition) are not supported.

Find the game folder: in Steam, right-click the game > Manage > Browse local
files, then open the **`GTAIV`** folder (the one with `GTAIV.exe` in it).
Every step below puts files into that folder.

## 2. Install FusionFix

1. Download [**GTAIV.EFLC.FusionFix.zip**](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/latest/download/GTAIV.EFLC.FusionFix.zip)
   (from [FusionFix's releases](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/latest)).
2. Extract everything into the `GTAIV` folder. `dinput8.dll` should end up
   next to `GTAIV.exe`.

## 3. Install ScriptHook

1. Download [**scripthookdotnet_v1.7.1.8.zip**](https://github.com/Tomasak/gta4_scripthookdotnet/releases/download/release/scripthookdotnet_v1.7.1.8.zip)
   (from [Tomasak's ScriptHook releases](https://github.com/Tomasak/gta4_scripthookdotnet/releases/latest)).
2. Extract everything into the `GTAIV` folder. `ScriptHook.dll` should end
   up next to `GTAIV.exe`.

## 4. Install GTA IV FPV

1. Download [**gtaiv-fpv.zip**](https://github.com/hammerill/gtaiv-fpv/releases/latest/download/gtaiv-fpv.zip).
2. Extract everything into the `GTAIV` folder.

Your `GTAIV` folder now has, among the game's own files:

```
GTAIV/
├── GTAIV.exe
├── dinput8.dll           (FusionFix)
├── plugins/              (FusionFix)
├── ScriptHook.dll        (ScriptHook)
├── GTAIV.FPV.asi         (this mod)
├── GTAIV.FPV.core.dll    (this mod)
└── libfpv.dll            (this mod)
```

## 5. Fly

Start the game, load a save and press **F7**. Niko launches the drone.

| Key | Action |
|---|---|
| F7 | Launch the drone / return to Niko |
| F8 | Settings: radio setup, rates, OSD, camera |
| C | Cycle cameras: FPV, chase, line of sight |
| R | Respawn the drone, repaired |
| Y | Move Niko to the drone |

- **Radio:** plug it in (USB joystick mode), press F8 and follow the radio
  setup wizard once. Your setup is remembered. Besides arm, it asks for
  angle mode, beeper, turtle, air mode and OSD disable switches; skip any
  you don't want. OSD disable gives a clean picture for recording: no OSD
  and no game text.
- **Keyboard:** Enter arms, W/S is throttle, A/D is yaw, and the arrow keys
  are pitch and roll.
- **Gamepad:** works too.

## Tune it in Betaflight Configurator

The drone runs real Betaflight, so you can set it up like a real quad, with
PIDs, filters, rates and modes, in
[Betaflight Configurator](https://app.betaflight.com) (the web app or the
desktop app):

1. In the game, launch the drone with F7.
2. In Configurator, open Options and turn on **manual connection mode**.
3. Pick **Manual Selection**, enter `ws://localhost:5761` as the port and
   press Connect.

F8 > Configurator shows the same address. While Configurator is connected,
Betaflight won't arm, the same as a real quad, so disconnect it before you
fly. Keep the motor protocol on PWM and ARM on AUX1.

## Updating

Download the newest `gtaiv-fpv.zip` and extract it over the old files. Your
settings are kept.

## Uninstalling

Delete `GTAIV.FPV.asi`, `GTAIV.FPV.core.dll`, `GTAIV.FPV.core.live.dll`,
`libfpv.dll`, `libfpv.LICENSE.txt`, `GTAIV.FPV.log` and the `fpv-data`
folder.

## Something wrong?

- **F7 does nothing:** check that `dinput8.dll` (step 2) and
  `ScriptHook.dll` (step 3) are next to `GTAIV.exe`, and that the three
  mod files are in the same folder, not in a subfolder.
- **Anything else:** look in `GTAIV/GTAIV.FPV.log`, then
  [open an issue](https://github.com/hammerill/gtaiv-fpv/issues) and attach
  that file.
