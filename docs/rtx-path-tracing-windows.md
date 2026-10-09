# Official RTX Remix SDK integration

The previous custom Vulkan path tracer has been **removed**. The normal game
renderer is restored. This project now has a backend built on NVIDIA's official
RTX Remix SDK; **playable Bloodborne path tracing is not completed yet**.

## Implemented and checked

- Unmodified C SDK **0.6.4**, pinned to RTX Remix **1.5.2**, runtime commit
  `b81a7b566b1eeb9edb4dc2b3c9d3972e0f253ad4`.
- Official scene submission: camera, opaque/PBR materials, meshes, instances and
  lights. Unchanged resources reuse their stable identifiers and revisions.
- SDK mesh validation checks triangle counts, vertex/index bounds, finite
  attributes, material references and skinning data before submission.
- Shared D3D9/Vulkan output imports a Win32 KMT memory handle on the same adapter.
  It does not pass device-local image or semaphore handles between devices.
- A completion event gates reading; the consumer must finish before output reuse.
  Texture contents stay on the GPU. CPU readback is used only by diagnostics.
- Explicit resize handles a width-only change too. Imported images must be
  released before the SDK texture is replaced.
- Opaque material defaults set alpha testing to `ALWAYS`: zero in this SDK means
  `NEVER` and makes geometry invisible. Sprite dimensions default to 1 by 1.
- The SDK window is destroyed while its D3D9 device and callback are still alive.
  Destroying it after SDK shutdown caused an unhandled Windows callback exception
  in the initial diagnostic implementation.

`bb-remix-check.exe` renders a synthetic scene, checks visible material color,
compares SDK readback with a separate Vulkan device's imported pixels, resizes,
replaces a mesh and destroys resources. It does not load Bloodborne assets or
measure gameplay performance. The renderer, acceleration structures, path
tracing, denoising and reconstruction come from the official runtime.

The SDK backend lives in [`gpu/remix`](../gpu/remix). It is not called by normal
game launch and does not replace the game's output with a synthetic scene.
There is no in-game RTX toggle at this stage.

## Runtime installation and diagnostics

The installer verifies the original release archive SHA-256, extracts only the
64-bit `.trex` runtime and license notices, rejects unsafe ZIP paths and links,
and checks the PE architecture. The 32-bit bridge is not installed into the port.
No system Direct3D libraries or existing installations are overwritten.

From a packaged Windows build:

```powershell
.\Bloodborne.exe --script .\scripts\install_remix.py
.\'Check RTX Remix SDK.cmd'
```

Packages containing `remix-runtime-1.5.2` can run the check offline. The test runs
in its own process and runtime directory, with the external crash monitor when
available. Logs, a PPM image, a result JSON and any crash dump are written under
`logs/remix-sdk-*`. A 180-second process timeout also covers blocked runtime calls.
Initial shader compilation can take longer than cached runs.

For a source build:

```powershell
cmake --build out/gpu --target bb-remix-check
python scripts/install_remix.py --destination out/remix-runtime-1.5.2
Push-Location out/remix-runtime-1.5.2/.trex
../../gpu/remix/bb-remix-check.exe ./d3d9.dll
Pop-Location
```

The runtime currently logs `common device objects were not disposed of` during
shutdown, including after explicit application resource destruction. The
standalone diagnostic exits successfully, but this warning remains unresolved.
This is another reason to retain process isolation before gameplay integration.

## Remaining game integration

Bloodborne's shaders and assets must be translated into the SDK's scene inputs:
static map geometry and placements, actual texture/material semantics, analytical
lights, animated character deformation, stable object identities and map lifetime.
Geometry outside the current raster view must remain available to secondary rays.
The existing clip-position motion cache alone is insufficient for that scene.

Remix's final output is tonemapped. Game integration must compose it at the correct
stage, preserve HUD/menus and choose one owner for temporal jitter, upscaling and
frame generation. Writing it into the HDR scene before another tonemap would be
incorrect. Gameplay validation, image comparison and performance measurement are
still required; the synthetic SDK test establishes none of those results.

## Official sources and licensing

- [RTX Remix SDK documentation](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/documentation/RemixSDK.md)
- [RTX Remix 1.5.2 release](https://github.com/NVIDIAGameWorks/rtx-remix/releases/tag/remix-1.5.2)
- [Pinned runtime source](https://github.com/NVIDIAGameWorks/dxvk-remix/tree/b81a7b566b1eeb9edb4dc2b3c9d3972e0f253ad4)

SDK copyright and MIT notices are retained in `gpu/remix/LICENSE.txt` and the
original header. Runtime installation retains the official third-party notices.
The pinned original release ZIP SHA-256 is
`cc424be4dd1a0c6fd922bc6a7f8e5f6582baea7043a38afa6686d8b6faabad01`.
