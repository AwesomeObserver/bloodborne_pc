# Experimental native RTX path tracing — Windows v2

The port now includes an opt-in hardware path tracer in its own Vulkan renderer.
It runs before the game's post-processing and HUD. **GPU regression scenes pass;
full Bloodborne gameplay and image-quality validation are pending.**

## Start

1. Configure the game and graphics in `Bloodborne.exe` once, then close the launcher.
2. Start **Play RTX Hybrid.cmd** to retain the game's lighting and add traced indirect
   lighting and rough reflections. This is the recommended first comparison.
3. Start **Play RTX.cmd** to replace captured opaque surfaces' lighting with the path
   tracer's experimental environment and directional light.
4. Start normally with `Bloodborne.exe` to use the standard renderer again.

Ray-query and capture capabilities are checked at startup. Unsupported devices or
failed RTX resource initialization keep the raster renderer. There is no extra
Remix DLL to install. The launch log distinguishes `RTX path tracing ready` from
`RTX path tracing active`: only the latter confirms that a game scene was traced.

**Trace RTX.cmd** first runs a small GPU preflight, then launches the game in hybrid
mode and collects `logs/rtx-trace-*.zip` when it exits. The report includes this
session's output, native crash dumps if produced, and executable fingerprints.
It does not copy game files, saves or settings. Send this ZIP with screenshots and
reproduction steps if the game has no visible change, incorrect lighting or crashes.
The preflight checks the GPU implementation; it cannot verify the game's scene hook.

## Implementation

- The vertex shader captures world-space positions **after the game's skinning and
  model transforms**, using buffer device addresses. Indices are copied when the
  draw is processed; no guest-memory pointer is retained for deferred recording.
- A bounded triangle list builds a BLAS and TLAS on the same Vulkan device and queue
  as the raster renderer. There is no per-frame geometry readback or cross-device
  frame transfer.
- Compute ray queries evaluate multiple light bounces, occlusion and a diffuse/GGX
  reflection mixture. Hybrid mode retains the game's direct lighting; replacement
  mode uses a default environment and directional light.
- Depth-aware spatial filtering and motion-vector reprojection reduce noise.
  History stores linear view depth, avoiding half-float precision loss near device
  depth 1. History resets after missing frames, resolution changes and large camera
  translations.
- The result goes back into HDR scene color before post-processing. The game's
  tone mapping and HUD continue afterward. Reduced scene targets are processed at
  their actual size, without resolving all inputs to full size first.
- Capture parameter rings wait for their own completed submission before reuse.
  New image resources are retired through the scheduler on resize. Address-bearing
  vertex shader variants are rebuilt each session rather than loaded with stale
  device addresses from the cache.

## Current limits

This is **an experimental native implementation**, separate from the RTX Remix /
shadPS4 demonstration described in the
[original report](https://en.gamegpu.com/news/igry/bloodborne-zapustili-na-pk-s-trassirovkoj-putej-pri-pomoshchi-rtx-remix-i-shadps4).
The demonstration's Bloodborne adapter and converted scene assets are not bundled.

Only eligible direct triangle-list G-buffer draws submitted in the current frame
are captured. Off-screen map geometry is not retained, so secondary-ray occlusion
and reflections can miss objects outside the raster view. Indirect, tessellated,
alpha-cutout and transparent geometry do not yet have complete tracing support.
Visible material color is recovered from the albedo/depth buffers; hidden surfaces
use a neutral fallback, face normals and a shared roughness. Exact game light,
normal-map, emissive, metallic and opacity extraction remain to be implemented.
Replacement mode therefore changes the art lighting substantially.

Pixels without a usable captured surface retain raster color. Menus and loading
frames retain the standard rendering path. DLSS/FSR reconstruction and generated
frames remain downstream; their combination with RTX still needs gameplay testing.
A small ray budget and the current denoiser can produce noise or ghosting. No
frame-rate, image-quality or all-location stability guarantee is implied.

The complete game assets are unavailable in the development environment. Remote
playtesting is required to confirm the G-buffer selection, scene dispatch trigger,
world-space alignment, animated characters, HUD, location transitions and respawns.

## Options

Set these environment variables before launching; RTX itself requires a restart.
The CMD launchers enable RTX for that process only.

| Variable | Default | Range / purpose |
| --- | --- | --- |
| `BB_RTX_PATH_TRACE` | off | `1` enables native ray tracing |
| `BB_RTX_MODE` | `1` | `1`: replace lighting; `2`: hybrid indirect |
| `BB_RTX_SAMPLES` | `1` | 1–4 samples per pixel; more samples cost more time |
| `BB_RTX_BOUNCES` | `3` | 1–6 surface interactions; indirect light needs at least 2 |
| `BB_RTX_MAX_VERTICES` | `1048576` | 32–4194304 captured vertices per frame |
| `BB_RTX_MAX_INDICES` | `3145728` | 96–12582912 indices per frame; three per triangle |

Captures beyond the geometry or per-frame parameter budget are skipped safely.
The active-scene log reports draw/triangle/skip counts and the selected ray budget.
First launch after this update recompiles shader caches with the new format.

## Verification

`path-tracer-test` executes the production acceleration-structure construction,
ray-query and denoising shaders on a GPU and reads their pixels back. It checks
light blocking by geometry outside the primary view, additional-bounce lighting,
background/alpha preservation, invalid input fallback, repeated ring reuse,
resolution changes and hybrid composition. It also runs with asynchronous command
recording/submission and an intentionally unaligned requested index capacity.
`path-tracer-disabled-test` checks that a normal launch enables no ray-query device
feature and publishes no capture addresses.

`ray-capture-test` runs the actual recompiler's vertex epilogue on a GPU. It checks
world-space reconstruction, instancing, negative indexed base vertices, packed
parameter halves, disabled and out-of-range writes. CPU checks cover camera matrix
round trips with both viewport signs and bounded index expansion. Six ordinary,
object-motion and ray-capture SPIR-V variants pass `spirv-val --target-env vulkan1.3`.

These are synthetic tests. They do not substitute for running the full game.
The Vulkan validation layer is not installed in the local environment; the local
GPU tests must not be described as a validation-layer pass.
