# Bloodborne for Windows v2

**Bloodborne running natively on Windows 10 and 11.**

The original PlayStation 4 game runs directly on your PC: its own x86-64 code runs natively,
and the graphics are translated to Vulkan. No emulator window, no setup scripts: unpack the zip,
start `Bloodborne.exe`, pick your game folder and press **PLAY**.

Windows fork: [AwesomeObserver/bloodborne_pc](https://github.com/AwesomeObserver/bloodborne_pc).
Updated to upstream **0.4** (`daa7165`); see the [migration and validation report](docs/windows-upstream-0.4.md).

**[Download the latest version](https://github.com/AwesomeObserver/bloodborne_pc/releases/latest)**

> **No game files are included.** You need your own decrypted dump of Bloodborne
> (CUSA03173), updated to **1.09**, with the supported decrypted executable.
> The launcher verifies the executable before applying patches or starting the game.
> This project is not affiliated with Sony Interactive Entertainment or FromSoftware.

## Features

- **Launcher with every setting in one window**, in 13 languages: English, Arabic, Russian,
  Spanish, Portuguese, French, German, Italian, Polish, Turkish, Chinese, Japanese, Korean.
- **Updates from the launcher:** when a new version is out it tells you, and **Update**
  installs it. Your saves, settings and mods are kept.
- **NVIDIA DLSS**, available when the runtime reports support, with a
  [model preset selector](docs/dlss-frame-generation-windows.md#dlss-model-presets).
- **AMD FSR 3.1, FSR 4 and FSR 4.1.1/FP8** upscaling (matching GPU features and assets required), plus native-resolution TAA.
- **Press-to-bind keyboard, mouse and controller**, up to four alternatives per action,
  controller selection and working touchpad gestures.
- **Unlocked frame rate** with a frame cap (up to 120 by default), or 30/60/90 FPS.
- **Output resolutions** from 720p to 4K, presets from Native AA to Ultra Performance.
- **Cheats page:** never die, stealth, silent footsteps, Rally that never fades, enemy control,
  and gameplay tweaks (camera distance, no camera auto-rotation, easier running, ragdoll
  physics).
- **Game effects** on and off: chromatic aberration, depth of field, motion blur, SSAO, the
  game's own AA, dynamic light shadows, screen-space reflections, model detail.
- **Mods and third-party patches**, loaded without changing your game files.
- **`Play Bloodborne.exe`** starts the game straight away with your saved settings (good for a
  desktop shortcut or Steam).
- **Controller and keyboard**, an in-game settings menu (Insert or L3+R3), name entry on
  screen, a desktop shortcut, and a button to clear the shader cache.

## Requirements

- Windows 10 (1903 or later) or Windows 11, 64-bit
- A graphics card with Vulkan 1.3 and a current driver
- About 6 GB of free memory (RAM + page file), 10 GB for 1440p or 4K output
- Optional DLSS features require support reported by the NVIDIA runtime.

Nothing else to install: everything the game needs is in the zip.

## How to play

1. Download the zip from [Releases](https://github.com/AwesomeObserver/bloodborne_pc/releases/latest)
   and unpack it anywhere.
2. Start `Bloodborne.exe`.
3. On **Game & effects**, choose your game folder (the one with `eboot.bin`).
4. Press **PLAY**.

In the game, **Insert** (or **L3+R3** on a controller) opens the settings menu.
**F4** toggles direct mouse camera control. Enable it and adjust sensitivity/invert Y on
**Controls** in the launcher or in the game menu. Opening a menu or switching windows
releases capture; lock-on retains the game's camera. See [mouse camera](docs/mouse-camera-windows.md).
On **Controls**, click an existing binding and press the desired key, mouse button/wheel
or controller button/trigger. **+** adds an alternative (up to four); **Clear** removes the
binding. Close the capture window to cancel. Changes apply at the next game start.
Use the mouse and wheel to edit and scroll through the menu; Ctrl+click a slider to enter
an exact value. Settings are saved when the menu closes. Effects, model detail and motion
vector patches require **Apply and restart game**. With live resolution changes disabled,
resolution and quality changes also require a restart. Reactive mask controls apply to FSR;
they are disabled for DLSS and TAA.
Keyboard: WASD move, arrows camera, Space Cross, Left Shift Circle, E Square, Q Triangle,
1/3 L1/R1, R/F L2/R2, Z/C L3/R3, I/K/J/L d-pad, Enter Options, Tab touchpad.

## Known issues

- The character preview on the character creation screen stays empty. The character is
  created correctly and looks right in the game.
- Some AMD graphics cards still crash when the game world loads; fixes are in progress.
- Above about 120 FPS the game's movement slows down (a limit of the game itself): keep the
  frame cap at 120 or lower.

## Problems and feedback

Open an [issue](https://github.com/AwesomeObserver/bloodborne_pc/issues), describe what happened
and attach `user\last_run.log` from the game folder.
If the game shows only a black screen, try **Advanced → Clear shader cache** first.

## Building from source

See [packaging/windows/README.md](packaging/windows/README.md): MSYS2 CLANG64, `bash build.sh`,
then `bash packaging/windows/package.sh`. DLSS is built separately with
`packaging/windows/build_dlss.sh`.

## Credits

Uses the [shadPS4](https://github.com/shadps4-emu/shadPS4) renderer.

Also used: [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) and the AMD FidelityFX SDK
(FSR), FSR 4 assets from [FireBurn/Q2RTX](https://github.com/FireBurn/Q2RTX), the DLSS bridge
adapted from [IFreemz/shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR),
the [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) (`nvngx_dlss.dll` under NVIDIA's license),
[LibAtrac9](https://github.com/Thealexbarney/LibAtrac9), [SDL3](https://github.com/libsdl-org/SDL),
[FFmpeg](https://ffmpeg.org), [Dear ImGui](https://github.com/ocornut/imgui),
[sirit](https://github.com/shadps4-emu/sirit), [magic_enum](https://github.com/Neargye/magic_enum),
[miniz](https://github.com/richgel999/miniz), [xbyak](https://github.com/herumi/xbyak),
[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
[MSYS2](https://www.msys2.org) / [LLVM](https://llvm.org), [PyInstaller](https://pyinstaller.org)
and [Pillow](https://python-pillow.org). Game patches by Kyo, Lance McDonald, auser1337,
illusion, emoose and the Bloodborne community.

NVIDIA and DLSS are trademarks of NVIDIA Corporation. The icon is original
artwork.

## License

GNU GPL v2 or later ([LICENSE](LICENSE)). Third-party components keep their own licenses
(see `licenses\` in the download).
