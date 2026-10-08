Enhanced Native Bloodborne Port for Windows
v2
==========================================

Native x86-64 game execution, Vulkan rendering and a Windows launcher.
Runtime dependencies are bundled; no separate Python installation is needed.

QUICK START

1. Extract the Windows archive and open Bloodborne.exe.
2. On Game & effects, select your game folder containing eboot.bin.
3. Choose graphics and controls, then press PLAY.

Requires Windows 10 (1903+) or Windows 11, 64-bit, a Vulkan 1.3 runtime and a
current graphics driver. Bring your own decrypted CUSA03173 dump, updated to 1.09
(eboot.bin, sce_module, sce_sys, dvdroot_ps4). The launcher verifies the supported
executable before preparation and launch. No game files are included.

After setup, Play Bloodborne.exe starts directly with your saved settings.
Bloodborne.exe --play does the same. Add the direct launcher to Steam or use
Advanced -> Desktop shortcut. Built-in updates preserve saves, settings and mods.

HIGHLIGHTS

- DLSS Super Resolution / DLAA, FSR 3.1, FSR 4, FSR 4.1.1/FP8 and native TAA.
- DLSS model presets: Auto, A-F, J, K, M, L. Model type follows the preset:
  E/F = CNN, J/K = Transformer, M/L = Transformer 2. A-D are legacy requests that
  current DLLs may substitute; E/F are deprecated. Render scale is independent.
- Optional DLSS FG x2 or FSR 3.1 FG x2, selected separately from upscaling.
  FSR FG can also run with DLSS or native TAA.
- Raw mouse camera: direct angles, linear sensitivity, invert Y, F4 capture.
- Press-to-bind keyboard, mouse and controller; four bindings per action.
- Unlocked FPS, configurable frame cap, sharpening and individual effect toggles.
- In-game settings, mod ordering, XML patches, camera tweaks and optional cheats.
- Launcher interface in 13 languages, updates, session logs and local crash dumps.

DLSS availability is checked at runtime. FSR 4 needs matching shader assets;
Download FSR 4 assets is on Graphics. FSR 4.1.1 uses a prepared fsr4_411 folder,
selected on Graphics or through BB_FSR411_DIR. Unsupported modes are unavailable
or fall back to FSR 3.1.

CONTROLS

F4          Toggle raw mouse camera
Insert      Open the port settings menu (controller: L3 + R3)
WASD        Move
Arrow keys  Keyboard camera
Space       Cross
Left Shift  Circle
E / Q       Square / Triangle
1 / 3       L1 / R1
R / F       L2 / R2
Z / C       L3 / R3
I/K/J/L     D-pad
Enter       Options
Tab         Touchpad
Backspace   Right touchpad

On Controls, click a binding and press a key, mouse button, wheel direction or
controller button/trigger. + adds an alternative; Clear removes bindings.
Close the capture window to cancel. Remaps apply at the next game launch.

The launcher and in-game menu expose mouse sensitivity and invert Y. Opening the
port menu, name entry or switching windows releases capture. Lock-on uses the
game's camera. Conflicting camera patches disable raw camera with a reason.

In the settings menu, use the mouse and wheel; Ctrl+click a slider to enter an
exact value. Settings save when the menu closes. Effects, model detail and
motion-vector patches use Apply and restart game. Resolution and quality also
need a restart when live resolution changes are disabled.

COMPATIBILITY

- Keep the render cap at 120 FPS or below; higher rates can slow movement and
  rolls. The automatic cap respects this limit.
- Frame generation is experimental: x2 only; requires SDR and object motion vectors.
  DLSS FG gameplay validation is pending. See the linked documentation for tests.
- Character creation preview can be empty; the character still appears in game.

MODS AND PATCHES

Put each mod in its own folder under mods\, with dvdroot_ps4\... or game folders
such as chr\ and parts\ directly inside. Enable and order them on Mods & patches.
The source game files stay intact; later mods take priority over earlier ones.
Place shadPS4/GoldHEN XML patches for version 1.09 in patches\.

SAVES AND LOGS

user\                 Saves and shader caches; another folder can be selected.
bbport.ini            Port settings, beside Bloodborne.exe.
%APPDATA%\bbport-launcher
                      Launcher options.
out\                  Prepared game image and generated patches.
user\last_run.log      Last launch log (or in the selected save folder).
logs\                 Additional logs from Advanced -> Save session log.
logs\crashes\         Native minidumps under the port's data folder.

Crash dumps stay on your computer. BB_CRASH_DUMP=0 disables them;
BB_CRASH_DIR selects an existing output directory.

HELP AND DOCUMENTATION

Black screen: Advanced -> Clear shader cache, then relaunch.
Bug reports: include the build version, reproduction steps and last_run.log;
for native crashes, also include the matching .dmp and session log.

Project and issues:
https://github.com/AwesomeObserver/bloodborne_pc
https://github.com/AwesomeObserver/bloodborne_pc/issues

DLSS presets and frame generation:
https://github.com/AwesomeObserver/bloodborne_pc/blob/windows/docs/dlss-frame-generation-windows.md

Raw mouse camera:
https://github.com/AwesomeObserver/bloodborne_pc/blob/windows/docs/mouse-camera-windows.md

GNU GPL v2 or later. See LICENSE, licenses\ and README.md for third-party credits.
Not affiliated with Sony Interactive Entertainment or FromSoftware.
