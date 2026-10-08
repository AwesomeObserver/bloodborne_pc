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

## Movement report and rejected quantization change

The later `mouse-camera-20261008-190629-15948.zip` contains 4523 updates over
37.684 seconds, including held W, W+A and W+D with F4 enabled and disabled.
The user subsequently identified adding A as the problematic transition;
adding D appeared normal.

The final pad input remains stable throughout each held combination. There are
no unintended button presses, neutral samples or right-stick commands. While
the mouse owns rotation, the next entry angles match the preceding post-input
angles; view yaw agrees with the stored yaw within approximately `7e-7` radians.
The capture does not show angle feedback or ownership changes causing the jerks.

The original input values and the subsequent change were:

| Input | Original report X/Y | Rejected change X/Y | Current X/Y |
| --- | --- | --- | --- |
| W | 128 / 0 | 128 / 0 | 128 / 0 |
| W+A | 37 / 37 | 38 / 38 | 38 / 37 |
| W+D | 218 / 37 | 218 / 38 | 218 / 37 |

Commit `7c6d2e8` rounded both diagonal components toward zero to keep their offset
inside a radius-128 circle. Its isolated input tests passed, but the user rejected
the gameplay result: W+A still jerked and W+D started doing so too. The jerks
continue while held; physical controller movement remains normal with F4 enabled.
That result invalidates the proposed fix. A radius check alone does not establish
the cause of the gameplay problem. Physical controller values can also round
outside this mathematical circle and must remain untouched.

The rejected change reduced both diagonal offsets to `(90,90)`: length 127.279,
approximately 99.44 percent of radius 128, and exactly 45 degrees. The original
W+D offset `(90,-91)` has length 127.988, approximately 99.99 percent, and an angle
of approximately 44.683 degrees from forward. The original W+A offset `(-91,-91)`
was exactly 45 degrees. The captured routine does not include character animation
selection; an unstable movement-sector boundary at exactly 45 degrees is a
hypothesis, not a verified game-code finding.

## Current keyboard mapping and validation limits

The current mapping restores the W+D sample that the user reported as working and
mirrors it around neutral X=128 for W+A. Normalization considers the neighboring
integer coordinates and selects the nearest candidate inside the radius-128
circle. Equal-error choices prefer the vertical component. All keyboard diagonals
therefore use horizontal offsets of 90 and vertical offsets of 91, preserving
near-full strength without placing the input at exactly 45 degrees. Cardinals,
opposing-key cancellation and mixed controller axes retain their existing behavior.
There is no temporal filtering or change to the working raw mouse camera hooks.

The input regression requires the recorded W+D `(218,37)` to remain unchanged,
W+A to be its mirror `(38,37)`, and diagonal squared magnitude to be at least 16380
and at most 16384. It also checks all 16 WASD states, repeated held-direction
transitions, remapped keys/mouse buttons, physical controller passthrough and
983040 mixed keyboard/controller states. This rejects both previous mappings.
These checks validate delivered input, not character motion or animation.

Gameplay confirmation is still required. If jerks remain, a V2 report should
compare keyboard and physical-controller diagonals in the same open area, with
F4 enabled and mouse ownership established before both cases. Keep the right
stick neutral so it does not return rotation to the native camera. Private reports
and captured game code are not distributed.
