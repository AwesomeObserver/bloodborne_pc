<p align="center">
  <img src="launcher/bloodborne.png" width="112" height="112" alt="Bloodborne port icon">
</p>

<h1 align="center">Enhanced Native Bloodborne Port for Windows</h1>

<p align="center">
  <strong>v2 · Native x86-64 game execution · Vulkan rendering · Windows launcher</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Port-v2-981b1e?style=flat-square" alt="Port v2">
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-242424?style=flat-square" alt="Windows 10 and 11">
  <img src="https://img.shields.io/badge/Renderer-Vulkan-981b1e?style=flat-square" alt="Vulkan renderer">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--2.0--or--later-242424?style=flat-square" alt="GPL 2.0 or later"></a>
</p>

<p align="center">
  <a href="https://github.com/AwesomeObserver/bloodborne_pc/releases"><strong>Download builds</strong></a> ·
  <a href="#quick-start">Quick start</a> ·
  <a href="#controls">Controls</a> ·
  <a href="#documentation">Documentation</a> ·
  <a href="https://github.com/AwesomeObserver/bloodborne_pc/issues">Report an issue</a>
</p>

Run Bloodborne's original x86-64 game code natively with Vulkan rendering.
Packaged builds include runtime dependencies and a Windows launcher; no separate
Python installation is needed.

> **Bring your own game files.** Requires a decrypted **CUSA03173** dump, updated to
> **1.09**, with the supported executable. The launcher verifies it before preparation
> and launch. No game files are distributed with this project.

## Highlights

| Feature | What you get |
| :--- | :--- |
| **DLSS & FSR upscaling** | DLSS Super Resolution / DLAA, FSR 3.1, FSR 4, FSR 4.1.1/FP8 and native TAA. **720p–4K**, Native AA through Ultra Performance. |
| **DLSS model presets** | Auto, **A–F, J, K, M, L**. CNN/transformer selection follows the preset automatically, independently of render scale. |
| **Frame generation** | **DLSS FG ×2** or **FSR 3.1 FG ×2**, independent of upscaling. FSR FG also works with DLSS or native TAA. |
| **Raw mouse camera** | Windows Raw Input, direct orbit response, equal-axis sensitivity, invert Y and automatic controller handoff. No added acceleration or virtual stick conversion. |
| **Press-to-bind controls** | Keyboard, mouse, wheel and controller inputs. **Four bindings per action**, controller selection and touchpad gestures. |
| **Graphics & frame rate** | Unlocked FPS, configurable cap, sharpening and model detail. Toggle motion blur, depth of field, chromatic aberration, SSAO, shadows and reflections. |
| **In-game settings** | **Insert / L3 + R3**. Mouse navigation, supported live changes, saved configuration and restart controls. |
| **Mods & gameplay tweaks** | Mod ordering, external XML patches, camera tweaks and optional cheats. Source game files stay intact. |
| **Windows launcher** | **13 languages**, updates that preserve saves/settings/mods, direct shortcuts, shader-cache cleanup, session logs and local crash dumps. |
| **Respawn loading** | Removes the original death-triggered **12-second minimum loading timer**, retaining resource and character readiness checks. |
| **Launch & render caches** | Verified prepared-image reuse, shared shader-module reuse during preload and asynchronous cache writes without payload copies. |

Feature availability is checked at runtime. FSR 4 variants also need their matching
shader assets. DLSS presets A–D are legacy requests that current DLLs may substitute;
E/F are deprecated. See [DLSS presets and frame generation](docs/dlss-frame-generation-windows.md).

## Quick start

**Platform:** Windows 10 (1903+) or Windows 11, 64-bit, with a Vulkan 1.3 runtime
and an up-to-date graphics driver.

1. Download a Windows archive from [Releases](https://github.com/AwesomeObserver/bloodborne_pc/releases) and extract it.
2. Open **`Bloodborne.exe`**.
3. On **Game & effects**, select your game folder containing `eboot.bin`.
4. Choose graphics and controls, then press **PLAY**.

After setup, **`Play Bloodborne.exe`** launches directly with your saved settings.
You can add it to Steam or use **Advanced → Desktop shortcut**.

## Controls

| Action | Default input |
| :--- | :--- |
| Move / keyboard camera | **WASD** / **Arrow keys** |
| Walk (hold) | **Alt + WASD** (either Alt) |
| Toggle raw mouse camera | **F4** |
| Open port settings | **Insert** / **L3 + R3** |
| Cross / Circle / Square / Triangle | **Space** / **Left Shift** / **E** / **Q** |
| L1 / R1 · L2 / R2 | **1** / **3** · **R** / **F** |
| L3 / R3 · D-pad | **Z** / **C** · **I / K / J / L** |
| Options / Touchpad | **Enter** / **Tab** |

On **Controls**, click a binding and press the desired input. **+** adds an
alternative; **Clear** removes bindings. Remaps apply at the next game launch.

Keyboard movement uses symmetric diagonals within the circular stick range,
including after rounding to the game's input format. Adding a strafe key preserves
the held axis. Releasing that first key uses a brief directional handoff (8 ms)
at the current movement strength. Hold **Alt** to walk; release it to return to
normal movement. Remap **Walk (hold)** on **Controls**. Releasing all movement keys
stops keyboard input immediately; opposite movement keys cancel.

Mouse sensitivity and invert Y are available in the launcher and in-game menu.
Opening the port menu, name entry or switching windows releases capture; lock-on
uses the game's camera. [Mouse camera details →](docs/mouse-camera-windows.md)

## Compatibility notes

- **Keep the render cap at 120 FPS or below.** Higher rates can slow movement and rolls;
  the automatic cap respects this limit.
- **Frame generation is experimental:** ×2 only; requires SDR output and object motion vectors.
  DLSS FG gameplay validation is pending. See the [validation and limits](docs/dlss-frame-generation-windows.md#validation-and-limits).
- **Some settings require a restart.** Effects, model detail and motion-vector patches
  use **Apply and restart game**. Resolution and quality also need a restart when live
  resolution changes are disabled.
- **Character creation preview can be empty.** The created character still appears in game.

## Saves & troubleshooting

Saves and shader caches default to **`user\` beside `Bloodborne.exe`**; choose another
save folder in the launcher. Port settings live in `bbport.ini`, launcher options in
`%APPDATA%\bbport-launcher`.

For a black screen, try **Advanced → Clear shader cache** and relaunch. For a crash,
[open an issue](https://github.com/AwesomeObserver/bloodborne_pc/issues) with reproduction
steps, the build version and **`user\last_run.log`** (or `last_run.log` in your selected
save folder). Native crash dumps are saved locally in **`logs\crashes\`** under the
port's data folder. **Advanced → Save session log** enables additional session logs.

## Documentation

- [Windows build and packaging](packaging/windows/README.md)
- [DLSS presets, sharpening and frame generation](docs/dlss-frame-generation-windows.md)
- [Raw mouse camera and validation](docs/mouse-camera-windows.md)
- [Windows 0.4 migration and validation](docs/windows-upstream-0.4.md)
- [Windows crash analysis](docs/crash-analysis-windows-v1.5.md)
- [October 8 gameplay crash investigation](docs/crash-analysis-2026-10-08.md)
- [Vulkan command-data crash and missing dump fix](docs/crash-analysis-2026-10-09.md)
- [Respawn loading fix and timing capture](docs/respawn-loading-windows.md)
- [Windows performance audit, optimizations and measurements](docs/performance-windows.md)

## Credits & license

Licensed under **[GNU GPL v2 or later](LICENSE)**. Third-party components retain their
own licenses; packaged builds include a `licenses\` directory.

<details>
<summary>Third-party components and patch credits</summary>

- Rendering: [shadPS4](https://github.com/shadps4-emu/shadPS4),
  [sirit](https://github.com/shadps4-emu/sirit),
  [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).
- Upscaling: [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan), AMD FidelityFX SDK,
  FSR 4 assets from [FireBurn/Q2RTX](https://github.com/FireBurn/Q2RTX),
  [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS), and DLSS bridge work adapted from
  [IFreemz/shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR).
- Runtime and interface: [SDL3](https://github.com/libsdl-org/SDL), [FFmpeg](https://ffmpeg.org),
  [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9), [Dear ImGui](https://github.com/ocornut/imgui),
  [magic_enum](https://github.com/Neargye/magic_enum), [miniz](https://github.com/richgel999/miniz),
  [xbyak](https://github.com/herumi/xbyak).
- Build and packaging: [MSYS2](https://www.msys2.org), [LLVM](https://llvm.org),
  [PyInstaller](https://pyinstaller.org), [Pillow](https://python-pillow.org).
- Game patches: Kyo, Lance McDonald, auser1337, illusion, emoose and the Bloodborne community.

NVIDIA and DLSS are trademarks of NVIDIA Corporation. The port icon is original artwork.

</details>

This project is not affiliated with Sony Interactive Entertainment or FromSoftware.
