# Mouse camera response: 2026-10-08

## Evidence

The supplied `mouse-camera-20261008-172508-42260.zip` contains 4096 camera-update
samples over 34.389 seconds, plus the original camera routine and bounded callees.
It was recorded on Camera Fix / Crash Diagnostics.

Raw mouse counts reached the angle fields immediately. Across eligible consecutive
mouse-owned samples, the next entry angles matched the preceding post-input angles;
there were no angle-feedback mismatches above `1e-6` radians. Input accumulation
therefore did not explain the reported reversal delay and vertical inertia.

The position-derived world-orbit direction lagged behind those angles. For example,
the pitch target stayed at approximately 1.15111 radians from 24.639 to 25.281
seconds while the orbit pitch continued from approximately 0.41109 to 1.02105
without vertical mouse input. These values are derived from position/focus vectors,
not a captured view matrix.

## Cause

The game performs another interpolation after computing its desired orbit position:

```text
factor = chase + (1 - chase) * camera.blend_override
position = previous_position + (desired_position - previous_position) * factor
```

The recorded camera has horizontal angular thresholds of approximately 10 and 20
degrees (`+1bc/+1c0`). This stage can hold horizontal response inside its angular
band, including after a reversal. Normal X/Z chase is 0.1 (`+1ac`), while Y chase
is 0.03 (`+1b8`). The unequal chase rates explain why equal raw angular gain still
felt different on the two axes, and why the camera kept moving after input stopped.

The scalar at `+130` is the blend override in this equation, rather than residual
angular velocity. The previous hook cleared it on each mouse update, leaving the
native chase active. Synchronizing angle fields alone could not remove that stage.

The routine also contains two `VEXTRACTPS` yaw stores and five immediate
angle/correction writes that the previous scalar-store discovery did not cover.

## Change

A permanent, ownership-gated hook at image offset `0x143e7e6` supplies `1.0` as the
local blend operand while the mouse owns free rotation. The equation then reaches
the desired orbit immediately on X/Y/Z. Native `+130` state and its writes remain
intact. The hook executes the original operand load for stick ownership, lock-on
or capture off.

The installer verifies the entire 37-byte equation before changing any code.
Angle discovery now decodes complete instructions and gates scalar, extract and
immediate stores to the allowed fields. Existing auto-rotation NOP patches are
preserved. Collision queries and subsequent radial-distance code are unchanged.
The new orbit gate is inline assembly with no additional native callback,
allocation, worker or file operation per update.

## Verification and limits

`mouse-camera-test` covers angle writes, all store forms, entry timing, atomic input
transport, native controller handoff, binoculars, signature rejection, concurrent
toggles and register preservation. `mouse-orbit-test` executes the position blend
and checks its output vector, including native versus mouse ownership.

The same orbit test accepted the actual captured camera routine and replayed the
report's input and angle state through the isolated operator: 3825 eligible
mouse-owned updates, 909 idle updates and 86 horizontal reversals all reached their
target vector immediately. Separate cases cover one-count input, 30/60/120/240
updates per second, native unequal chase rates, pitch limits, yaw wrap, camera
radii and vertical-stick/lock-on/F4 handoff. An instruction-scope comparison
confirms that collision, radial-distance and native override instructions remain
unchanged.

This is not a full gameplay replay: the test supplies a fixed focus/radius and
does not execute level geometry, physics, presentation or the complete camera
routine. Free rotation near walls, movement, binoculars, scripted transitions and
mouse/controller switching still need confirmation in the game. The report and
captured game routine are local verification inputs, not repository/package assets.

## Movement report and keyboard quantization

The later `mouse-camera-20261008-190629-15948.zip` contains 4523 updates over
37.684 seconds, including held W, W+A and W+D with F4 enabled and disabled.
The user subsequently identified adding A as the problematic transition;
adding D appeared normal.

The final pad input remains stable throughout each held combination. There are
no unintended button presses, neutral samples or right-stick commands. While
the mouse owns rotation, the next entry angles match the preceding post-input
angles; view yaw agrees with the stored yaw within approximately `7e-7` radians.
The capture does not show angle feedback or ownership changes causing the jerks.

It does expose asymmetric rounding in the previous keyboard mapping:

| Input | Previous pad X/Y | Offset from center 128 | Offset length | Corrected pad X/Y |
| --- | --- | --- | --- | --- |
| W | 128 / 0 | 0 / -128 | 128 | 128 / 0 |
| W+A | 37 / 37 | -91 / -91 | 128.693 | 38 / 38 |
| W+D | 218 / 37 | 90 / -91 | 127.988 | 218 / 38 |

Float normalization followed by the SDL signed-16-bit to unsigned-8-bit conversion
put W+A outside the radius-128 circle, while W+D stayed inside. The earlier test
also converted its reference controller values through that path and allowed a
one-percent magnitude tolerance, so it missed the defect.

Keyboard normalization now operates in the final 8-bit pad coordinate space.
Scaling and truncation toward the center keep the delivered vector inside the
circle. All four keyboard diagonals have offsets of 90 on each axis and length
127.279, about 99.44 percent of radius 128. Cardinals and opposing-key cancellation
remain immediate; no temporal filtering is added. Controller-only samples and
raw mouse camera code are unchanged. Mixed keyboard/controller movement is also
bounded after quantization.

The regression checks all 16 WASD combinations, repeated W/A/D transitions,
remapped keys and mouse bindings, and all 983040 combinations of nonempty WASD
states with physical 8-bit axis pairs. It also verifies native controller
passthrough. The new circle check fails on the previous mapping and passes with
the correction. All 11 relevant native input, camera, runtime and startup checks
passed.

This establishes an input defect, not the complete cause of the gameplay stop.
The rounding asymmetry exists with F4 both on and off. The recorded character
positions alone cannot distinguish intentional turning and level collisions from
the reported jerks. Gameplay confirmation is still required, especially while
adding and releasing A during forward motion. The V2 movement report remains
available for further comparison; private reports and captured game code are
not distributed.
