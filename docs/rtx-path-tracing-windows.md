# RTX path tracing: integration investigation

Status as of **2026-10-09**: **not implemented in the game renderer**. There is no
playable Bloodborne RTX build from this investigation. The standalone Remix API
test described below does not establish game compatibility, image quality or FPS.

## What the linked demonstration establishes

The [linked report](https://en.gamegpu.com/news/igry/bloodborne-zapustili-na-pk-s-trassirovkoj-putej-pri-pomoshchi-rtx-remix-i-shadps4)
describes a prototype which converts static world geometry into Remix assets and
streams dynamic geometry from the running game. It does not supply an integration
patch or a playable release. No public Bloodborne implementation was found during
this investigation; that is a search result, not proof that one cannot exist.

The [official Remix SDK](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/documentation/RemixSDK.md)
provides direct scene submission in a 64-bit application: materials, meshes,
lights, camera, instances and presentation. It does not require translating every
Bloodborne draw into Direct3D 9 fixed-function commands. The native game execution
in this port can remain in place while a separate scene adapter feeds Remix.

## Renderer audit

| Area | Existing code | Missing work |
| :--- | :--- | :--- |
| Camera | `CameraMotion::OnConstants` and `FrameCamera` in `gpu/shadps4/video_core/renderer_vulkan/vk_camera_motion.cpp` expose scene camera data. | Validate coordinate conventions, projection, jitter and camera cuts against the reconstructed scene. |
| Draws | `vk_rasterizer.cpp` resolves vertex streams, indices and resource bindings. | Recover mesh attributes, transforms, instances, deformation and stable object identity. Bound buffers are not a complete world-space scene. |
| Motion | `vk_object_motion.cpp` and `emit_spirv_special.cpp` store clip positions for selected G-buffer draws. | Full geometry capture cannot use this unchanged: it is gated on animation/palette changes and has no mesh materials or light definitions. |
| Static world | Game assets are prepared for the original raster renderer. | Convert map placements, meshes and materials once, retaining geometry outside the current view for secondary rays and shadows. |
| Materials and lights | Raster shaders consume cached textures and game constants. | Identify albedo, normals, roughness, opacity, emissive surfaces, light positions, intensity and scene lighting. Rendered color is not a substitute for these inputs. |
| GPU ownership | The port owns its Vulkan device and scheduler; Remix creates its own renderer. | Establish output and synchronization interop, or change presentation ownership. Remix's raw `VkImage`/semaphore handles cannot simply be used as resources of our device. |
| UI and temporal rendering | Scene composition, upscaling and frame generation already exist. | Preserve menus/HUD and choose one owner for jitter, denoising, upscaling and generated-frame presentation. Validate resize, loading and controller/overlay input. |

The current `vk_instance.cpp` does not enable acceleration structures or ray-tracing
pipelines for the port's own device. A native Vulkan path tracer is a separate
alternative to using Remix; adding those extensions alone would not produce a
path-traced game scene.

## Standalone runtime check

The official [RTX Remix 1.5.2 release](https://github.com/NVIDIAGameWorks/rtx-remix/releases/tag/remix-1.5.2)
was downloaded and checked locally. Its release archive SHA-256 is:

```
cc424be4dd1a0c6fd922bc6a7f8e5f6582baea7043a38afa6686d8b6faabad01
```

The matching SDK was taken from the release's pinned runtime commit
`b81a7b566b1eeb9edb4dc2b3c9d3972e0f253ad4`, API **0.6.4**. Its
[C example](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/b81a7b566b1eeb9edb4dc2b3c9d3972e0f253ad4/tests/rtx/apps/RemixAPI_C/remixapi_example_c.c)
was compiled as a standalone Windows x64 executable with a hidden 640x360 window
and checks on initialization, camera, instance, light and presentation results.
It loaded the **64-bit `.trex/d3d9.dll`**, rather than the release's 32-bit bridge.
No development Python or compiler directory was supplied in its runtime PATH.

- 20 submitted frames: all checked calls returned success; process exit code 0.
- A second run of 3 submitted frames also passed.
- A third run of 60 submitted frames passed, including checked mesh/light
  destruction and shutdown.
- Runtime logs selected Trace Ray for indirect integration and Ray Query for
  G-buffer/direct integration.
- No rendered-pixel comparison or performance benchmark was performed. The
  example also logged an error about common device objects during shutdown.
  Explicitly destroying its mesh and light before shutdown did not eliminate the
  error. Resource teardown still needs investigation before integrating this
  runtime.

Research binaries, headers, archives and logs are local under
`out/rtx-remix-research/`. They are not packaged with the port. No game-renderer or
launcher setting was changed and no RTX feature is advertised by this test.

## Required next steps

1. Obtain a complete supported CUSA03173 1.09 game dump locally. The supplied
   `eboot.bin` cannot provide maps, meshes and textures; the user confirmed that
   the full dump is currently unavailable here.
2. Reconstruct one real location with its static geometry and lighting; align it
   with the live game camera and preserve the HUD.
3. Stream the player, enemies and animated objects with stable identities, correct
   skinning, transparency and resource lifetime management.
4. Integrate presentation and temporal rendering, then test location changes,
   death/respawn, cutscenes, menus, resizing and device/resource failures.
5. Measure CPU submission, uploads, acceleration-structure work, ray dispatch,
   denoising, memory use and loading. Cache unchanged meshes/materials, retain
   static scene resources, reuse frame resources and avoid per-frame CPU
   readbacks or global GPU waits. These are implementation goals, not measured
   improvements.

An independently developed adapter can use the public SDK if the demonstration's
source remains unavailable. A working test scene is a prerequisite, but only real
gameplay and image validation can establish a working Bloodborne path-tracing
feature. No frame-rate or stability guarantee follows from the SDK smoke test.
