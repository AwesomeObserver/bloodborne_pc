# Direct mouse camera on Windows

F4 toggles direct camera control; it starts disabled. The launcher's **Controls** page and
the in-game menu offer sensitivity (1–400%, default 100%) and invert Y. At 100%, 900 raw
counts rotate the camera by one radian. Changes apply live in the game menu and are saved
when it closes; F4 saves its toggle immediately. Opening the port menu, entering a name,
disabling the feature or losing focus releases capture and discards old motion. Lock-on
uses the game's camera; binocular mode updates its separate angles at half sensitivity.

This feature requires the matching Bloodborne 1.09 camera instructions. The angle-load
hook, four auto-rotation sites and the camera routine's 29 original store signatures are
checked before any code is changed. An unsupported image or a conflicting code
patch leaves the image unchanged and displays a reason. Hooks are installed by the loader
before guest execution, and F4 never rewrites executable instructions.

The store check includes the camera's rotation-state fields `+0x130` and `+0x294`, as well
as its angle fields. Earlier builds omitted those two fields and could reject supported
camera stores with `Camera store signature differs`. The field list can be cross-checked
against [BBShadPS4Mouse's instruction targets](https://github.com/ajungg/BBShadPS4Mouse/blob/89ca38a30bf2c17645b5618d784a541e74e0885f/src/main.cpp#L209)
and [BBMCC's 1.09 validator](https://github.com/Mob100percent/BBMCC/blob/819046cdbddd9d63460c7a4169e374f600267b35/src/Main.cpp#L440).
These references establish instruction targets; the executable tests below use synthetic
instructions, not bytes extracted from a game installation.

The bundled **Disable Camera Auto Rotation via Movement** patch is also supported:
its nine-byte NOP replacements remain untouched, including when capture is off or
lock-on is active. Every remaining store must still be a complete nine-byte `vmovss`
to an allowed field of the camera in `r13`. Arbitrary replacements are rejected before
any hook is written. On a mismatch the game log records the image offset, bytes and
decoded instruction. F4 notifications have a bounded width from their first frame, so
long errors wrap horizontally and remain inside the game viewport.

## Raw input and camera response

Windows SDL relative mode uses [native Raw Input](https://github.com/libsdl-org/SDL/blob/release-3.4.0/src/video/windows/SDL_windowsmouse.c#L677).
The port explicitly disables [system acceleration](https://wiki.libsdl.org/SDL3/SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE),
sets relative speed scaling to one and disables warp-generated motion, overriding
inherited SDL hints. It consumes device deltas rather than cursor positions or virtual
stick axes. Only mouse events belonging to the game window are accepted; synthetic
touch and pen mouse events are ignored. Sensitivity is a constant multiplier, with no
velocity curve, dead zone, time multiplier or smoothing filter.

Earlier builds intercepted only the four **Disable Camera Auto Rotation via Movement**
stores. The remaining angle stores in the camera update routine could still overwrite
mouse results through the game's controller response and interpolation. The loader now
discovers and gates the remaining scalar stores to the rotation and convergence fields
in the supported routine, with a second signature check for its 29 original store encodings
to the five main rotation fields.
Higher-XMM-register variants are also decoded and gated. Capture disables these writes
on idle frames as well, preventing them from adding a smoothing tail after the mouse
stops. F4 off restores their original behavior through the hook gates; lock-on also
retains the game's writes. Unrelated fields and other functions are left unchanged.

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
| Hook installation | Whole-process discovery, then live changes of five instruction sites on each toggle | Main-load and complete camera-response store checks in the known loaded routine; one startup installation, atomic toggle |
| Validation | Main instruction pattern checked; surrounding sites are copied without validating their instructions | Main loads checked byte for byte; stores decoded for size, camera base and allowed fields; exact bundled NOP patches preserved |
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
Separate `patched`, `mixed` and `convergence` cases cover existing NOP patches, both
previously omitted fields and the remaining allowed angle fields. Invalid register bases,
field offsets, loads, integer stores, RIP-relative instructions and partial NOP replacements
must still fail without changing the image. The overlay test checks the long error and
toggle notification layouts at 320x180, 640x360, 1280x720 and 2560x1440, including a nonzero
viewport work-area origin.
Every discovered store is executed with capture off, on and in lock-on, including 120
idle camera updates. Missing response signatures reject the whole installation; unrelated
fields and matching stores outside the routine remain unchanged. Packet sizes from 1 to
900 counts produce equal angles, and idle updates add no rotation.
Register checks include flags, MXCSR, a volatile integer register, the SysV red zone and
both halves of an AVX register. The same 8,000-event stream produces equal rotation at 30/60/120/240 camera
updates per second. The overlay test also checks that waiting for SDL events does not
consume clicks; settings and launcher tests cover persistence and invalid manual values.

A local synthetic run measured about 55 ns with capture disabled or no pending motion,
and 2.3 microseconds per update with motion, including the test's ABI wrapper and guarded
binocular pointer checks. These are CPU microbenchmarks, not game FPS or end-to-end input
latency measurements. The motion path executes at camera-update frequency, rather than
on a 1,000 Hz worker. No native callback or pointer checks run on the idle hook path.
The main-hook benchmark does not include the additional atomic gates at response stores.

`raw-mouse-test` creates a real Win32/SDL window, checks native Raw Input device registration
and confirms that inherited acceleration/scaling hints are overridden. It passes SDL
events through the production window pump and checks constant sensitivity and synthetic
device filtering. It also sends native mouse packets with `SendInput`: slow streams and
single flicks with equal counts must produce equal angles through Windows, SDL and the
camera callback. This test skips when desktop focus is unavailable; it does not change
Windows pointer settings.

No game dump is available in this workspace, so signature compatibility with an actual
installation and gameplay behavior still require a playtest. In particular, compare
free rotation at different caps, lock/unlock a target, use binoculars, open/close the menu,
Alt+Tab and return, then disable capture and check controller camera movement. A lower
actual render rate, queued GPU frames or frame generation can still increase display
latency; direct input does not remove those parts of the pipeline.
