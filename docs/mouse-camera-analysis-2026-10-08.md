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

## Sustained diagonal movement report

The later report of jerks during W+A/W+D remains unresolved. The user confirmed
that they continue throughout the held combination and occur only with F4 enabled.
The circular keyboard mapping passed its isolated pad tests but did not resolve
the gameplay issue. Those tests do not validate the camera/character interaction.

In the original supplied trace, the focus and reference positions remain constant
after initialization. It therefore contains no moving-character evidence for this
issue. The V2 diagnostic format records the final pad state together with camera
bases, follow vectors and state flags. A comparison of held diagonals with F4 on
and off is required before attributing the jerks to a particular camera operation.
Movement input and the working raw mouse response are unchanged by this diagnostic
update.
