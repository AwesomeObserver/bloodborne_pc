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
- Raw mouse camera: direct orbit response, equal-axis gain, invert Y and controller
  handoff. Mouse-owned rotation bypasses the game's angular hold band and chase.
- Press-to-bind keyboard, mouse and controller; four bindings per action.
- Unlocked FPS, configurable frame cap, sharpening and individual effect toggles.
- In-game settings, mod ordering, XML patches, camera tweaks and optional cheats.
- Launcher interface in 13 languages, updates, session logs and local crash dumps.
- Respawn loading: removes the death-triggered 12-second minimum loading timer
  while preserving resource and character readiness checks.

DLSS availability is checked at runtime. FSR 4 needs matching shader assets;
Download FSR 4 assets is on Graphics. FSR 4.1.1 uses a prepared fsr4_411 folder,
selected on Graphics or through BB_FSR411_DIR. Unsupported modes are unavailable
or fall back to FSR 3.1.

CONTROLS

F4          Toggle raw mouse camera
Insert      Open the port settings menu (controller: L3 + R3)
WASD        Move
Alt + WASD  Walk (hold either Alt; remap Walk (hold) on Controls)
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

Movement keys use a circular stick range. Adding a strafe key retains the held
axis. Releasing the first key uses an 8 ms directional handoff at the current
movement strength. Hold Alt to walk; release it to return to normal movement.
Releasing all movement keys stops keyboard input immediately.
Opposite movement keys cancel.

The launcher and in-game menu expose mouse sensitivity and invert Y. Opening the
port menu, name entry or switching windows releases capture. Lock-on uses the
game's camera. Conflicting camera patches disable raw camera with a reason.
Moving the right stick restores both native camera axes with F4 still on. After
releasing the stick, move the mouse to take control again; queued motion is discarded.

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
If the game closes without a dump, run Trace Crash.cmd and reproduce it.
The logs/crash-trace-*.zip report contains that session's output, dump and binary
fingerprints. The external Windows debugger is enabled only for this diagnostic
launch; exception processing can reduce performance. Normal launches retain the
in-process reporter. Native exit status is logged in decimal and hexadecimal.
Guest crash context adds up to 2 MiB of nearby code and object pages to the
normal minidump. Collection runs only after a crash.
Fatal GPU assertions also write a minidump before stopping with exit code 23.
Their dump uses assertion marker 0xE0424201 and records the failing thread and
reason. This marker identifies a deliberate assertion, not a CPU access violation.

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

Camera response diagnostics: run Trace Mouse Camera.cmd, load a character,
enable F4 and record direction reversals, vertical sweeps and stops for about
15 seconds. Close the game; the report ZIP appears in logs\.
The direct mouse-orbit fix is active in normal launches; tracing is optional.
Its isolated camera-code replay passed. Full gameplay validation remains pending.

Sustained diagonal movement diagnostics: run Trace Keyboard Movement.cmd.
After loading, enable F4 and move the mouse once. Hold W for 3 seconds,
W+A for 5 seconds, then W+D for 5 seconds without moving the mouse.
Also start with A or D, then add W or S and hold each combination for 3 seconds.
Release the first key while keeping the second held: W -> W+D -> D, then
D -> D+W -> W, holding each step for 3 seconds. Repeat with A and S.
Repeat while looking around, then disable F4 and repeat W/W+A/W+D.
Close the game and send the mouse-camera ZIP from logs\.
The V2 report records final pad state, camera bases and follow vectors together.
For a controller comparison, leave F4 enabled and move the mouse once before
repeating the same movement directions with the left stick. Keep the right stick neutral.
Diagonal rounding now preserves whichever movement axis was already held.
W/S-first and A/D-first diagonals retain the same near-full stick strength.
On primary-key release, a brief handoff preserves strength and splits the turn
into two smaller steps. It expires after 8 ms on the next input poll; stopping,
changing direction, opposite keys and menu capture cancel it immediately.
The user confirmed that adding a strafe key works in either axis order.
The release correction needs gameplay confirmation. Raw mouse camera is unchanged.

Respawn loading diagnostics: run Trace Respawn Loading.cmd.
With the game focused, press F8 before choosing Continue, then F8 when the character
is controllable. After a death, press F8 when the loading screen appears, then F8
when the respawned character is controllable. Repeat for two deaths and close the game.
Send the loading-*.zip from logs\ under the port's data folder.
This compares native process/thread CPU and logical I/O counters with existing frame,
GPU readback and wait logs. The tracing script observes loading; the Windows
loader applies the 1.09 minimum-delay fix by default. For a comparison with the
original timer, add BB_RESPAWN_DELAY_FIX=0 in Advanced environment overrides.
Diagnostic logging can affect timings; use the same mode for both loading paths.
The report includes logs and paths, but does not copy saves, settings or game assets.

Performance update: repeated launches reuse a content-verified prepared image.
Changing the executable, bundled modules or preparation tools rebuilds it.
Settings patches, content profiles and mods are still evaluated every launch.
BB_PREPARE_CACHE=0 in Advanced environment overrides forces fresh preparation.
Normal launches no longer collect diagnostic fault stacks; Save session log
still enables the existing detailed statistics. See docs\performance-windows.md.

GNU GPL v2 or later. See LICENSE, licenses\ and README.md for third-party credits.
Not affiliated with Sony Interactive Entertainment or FromSoftware.
