# Direct mouse camera on Windows

F4 toggles direct camera control; it starts disabled. The launcher's **Controls** page and
the in-game menu offer sensitivity (1–400%, default 100%) and invert Y. At 100%, 900 raw
counts rotate the camera by one radian. Changes apply live in the game menu and are saved
when it closes; F4 saves its toggle immediately. Opening the port menu, entering a name,
disabling the feature or losing focus releases capture and discards old motion. Lock-on
uses the game's camera; binocular mode updates its separate angles at half sensitivity.

This feature requires the matching Bloodborne 1.09 camera instructions. All five hook
sites are checked before any code is changed. An unsupported image or a conflicting code
patch leaves the image unchanged and displays a reason. Hooks are installed by the loader
before guest execution, and F4 never rewrites executable instructions.

## Review of PR #3

Reviewed [Supermedo/bloodborne_pc PR #3](https://github.com/Supermedo/bloodborne_pc/pull/3),
commit `db1d17c3c6a81746ef5334743cd20609505df82d`, including all seven changed files. Its
description proposes direct mouse rotation, F4 and launcher sensitivity, and reports that
latency varies with the frame cap. The submitted code does not implement the promised
launcher setting: it references `mousecam_sensitivity`, which is absent from its settings
type. This prevents that snapshot from building as submitted.

| Area | Submitted implementation | This implementation |
| --- | --- | --- |
| Input | Extra thread samples every millisecond; SDL relative state is sampled outside its video thread | Raw SDL events on the window thread; no sampler thread or timer |
| Camera writes | Worker writes a captured camera pointer concurrently with game execution | Accumulated motion is consumed on the guest camera thread, before its original angle loads |
| Hook installation | Whole-process discovery, then live changes of five instruction sites on each toggle | Direct checks in the known loaded image; one startup installation, atomic toggle |
| Validation | Main instruction pattern checked; surrounding sites are copied without validating their instructions | Main loads checked byte for byte; all stores decoded and checked for instruction size, camera base and allowed fields |
| Guest CPU state | Added `test r13,r13` changes flags where the original loads did not | Flags, integer registers and complete OS-enabled XSAVE state are preserved around native calls |
| Menu and focus | Worker gate stops updates, but opening the menu leaves relative capture enabled | Menu, text entry and focus transitions release relative mode and clear pending motion |
| Settings | Missing sensitivity member and launcher controls | INI, environment, launcher and live menu; finite values and range checks |
| Angle wrapping | Repeated subtraction; non-finite values can hang | Bounded input, finite checks and constant-time remainder |

SDL documents that `SDL_GetRelativeMouseState` belongs on the video/main thread; see
[SDL3 API](https://wiki.libsdl.org/SDL3/SDL_GetRelativeMouseState). The existing window
event loop also now waits for events instead of sleeping for two milliseconds after each
poll. Input wakes it immediately; the idle timeout is eight milliseconds. This change
does not alter the render cap, Vulkan queue depth or frame generation.

## Validation and performance limits

`mouse-camera-test` executes the real generated hooks against synthetic guest code. It
checks rejection of every changed signature without partial patching, all four original
store paths, lock-on, binocular mode, pitch limits, inverted Y, invalid input, stale-motion
discarding and repeated toggles while another thread executes the camera function.
Register checks include flags, MXCSR, a volatile integer register, the SysV red zone and
both halves of an AVX register. The same 8,000-event stream produces equal rotation at 30/60/120/240 camera
updates per second. The overlay test also checks that waiting for SDL events does not
consume clicks; settings and launcher tests cover persistence and invalid manual values.

A local synthetic run measured about 55 ns with capture disabled or no pending motion,
and 2.3 microseconds per update with motion, including the test's ABI wrapper and guarded
binocular pointer checks. These are CPU microbenchmarks, not game FPS or end-to-end input
latency measurements. The motion path executes at camera-update frequency, rather than
on a 1,000 Hz worker. No native callback or pointer checks run on the idle hook path.

No game dump is available in this workspace, so signature compatibility with an actual
installation and gameplay behavior still require a playtest. In particular, compare
free rotation at different caps, lock/unlock a target, use binoculars, open/close the menu,
Alt+Tab and return, then disable capture and check controller camera movement. A lower
actual render rate, queued GPU frames or frame generation can still increase display
latency; direct input does not remove those parts of the pipeline.
