# Install GTA V Enhanced FPV

Fly a real-Betaflight FPV drone through Los Santos with your own radio,
gamepad or keyboard. The install takes about five minutes.

## 1. What you need

- GTA V Enhanced (the 2025 PC edition), story mode.
- ScriptHookV with GTA V Enhanced support, with its ASI loader
  (`dinput8.dll`): http://www.dev-c.com/gtav/scripthookv/
- ReShade 6.x "with full add-on support", installed for GTA V Enhanced with
  DirectX 12: https://reshade.me
- Optional: an FPV radio in USB joystick mode, and the NVIDIA app for the
  FPV DVR.

## 2. Copy the files

Copy everything in this ZIP into the game folder, next to
`GTA5_Enhanced.exe`. Keep the folders:

```
GTA5_Enhanced.exe
GTAVE.FPV.asi
GTAVE.FPV.core.dll
GTAVE.FPV.logo.png
libfpv.dll
reshade-shaders\Shaders\GTAVE_FPV_Lens.fx
reshade-shaders\Textures\GTAVE_FPV_Bezel.png
```

The drone model is the `dlcpacks\gtavefpv` folder in this ZIP. With
onigiri, copy that folder to `onigiri\dlcpacks`, and add this line before
`</Paths>` in `onigiri\common\data\dlclist.xml`:

```
<Item>dlcpacks:/gtavefpv/</Item>
```

Without it, the mod uses a small game prop as the drone. The model is "DJI
Avata2" by raphael.harris.gaffga, CC BY 4.0. See
`dlcpacks\gtavefpv\CREDITS.txt`.

## 3. Turn on the lens effect

1. Start the game and load story mode.
2. Press Home to open ReShade.
3. Turn on the "GTAVE.FPV lens" effect, and move it to the end of the list.
   Keep it on: the mod shows the lens only in the FPV view.

## 4. Fly

1. Press F9. A drone launches in front of you.
2. Press Home and open the GTAVE.FPV tab.
3. Follow the radio setup wizard once. The keyboard also works: Enter arms,
   W and S are throttle, A and D are yaw, and the arrow keys are pitch and
   roll.

| Key | Action |
|---|---|
| F9 | Launch the drone, or return to the player |
| C | Cycle cameras: FPV, chase, line of sight |
| R | Respawn the drone, repaired |
| Y | Move the player to the drone |
| O | Switch the OSD style: analog or HD |
| G | Show or hide the goggles bezel |
| P | New propellers, where the drone is |
| `,` and `.` | Previous or next radio station |
| N, M, U, L | Races: record or save, mark a gate, undo, next race |

Turn off DLSS frame generation for FPV flying, because it adds input latency.

## Betaflight Configurator

The drone runs real Betaflight 2026.6. To connect Betaflight Configurator,
launch the drone, turn on manual connection mode in Configurator's Options,
and connect to `ws://localhost:5761`.

## Source and license

GPL-3.0-or-later. The source is at https://github.com/bwcon/gtave-fpv and
https://github.com/bwcon/libfpv.
