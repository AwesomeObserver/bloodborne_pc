# Respawn loading on Windows

The initial estimate was **3–4 seconds after death**; the first marked capture
measures **13.84 seconds**. Loading through **Continue** is faster; teleportation
does not show the reported delay.
The cause has **not yet been established**. The first capture confirms a long quiet
interval between resource-loading bursts; it does not identify the game instruction
responsible for it. No respawn performance fix is claimed.

## First capture: October 9

`loading-20261009-005030-519237.zip` has SHA-256
`beb81468e5a4af742111d4e4f3464be16d834f1e41e8969f858450c4a40dce65`.
The user confirmed that marker 3 was pressed on the loading screen and that this
was an ordinary respawn at an unlocked lamp, not the first death tutorial.

| Marked interval | Capture seconds | Elapsed |
| --- | --- | --- |
| Continue | 19.2571–24.9317 | 5.6746 s |
| Death loading screen to controllable character | 49.9068–63.7461 | 13.8393 s |

The nearest process-counter samples show approximately 884.4 MiB of logical reads
in the Continue interval and 497.3 MiB in the respawn interval. Between samples
50.9778 and 60.9510, only approximately 1.48 MiB of reads completed. Active resource
post-processing clusters around capture seconds 50 and 62. The character and cloth
threads are recreated around second 50; recreating them does not take the whole
remaining interval.

The native frame log contains 6,583 flips over 55.1821 seconds. Its long stretch of
loading-screen rendering remains around 120 flips per second. Every logged GPU
buffer readback combined takes **9.41 ms** across the whole session. GPU tick waits
are at most 0.4% in the printed five-second report windows. These measurements do
not support a multi-second GPU queue/readback stall as the main explanation.

A quiet I/O counter interval does **not** distinguish a timer, a game task waiting
for a dependency, and an outstanding blocking file operation. There is no per-call
file latency or game instruction snapshot in this report. The guest wait table also
hits its 512-entry capacity in the first window, so absence of another wait site is
not proof that no such wait happened. No synchronization or timer bypass was applied.

`BUILD-INFO.txt` identifies an older Axis Order package. This is package metadata;
it does not verify the actual running executable if files were updated separately.
The game log reports PE timestamp `6ac7f5eb`. None of the later keyboard handoff or
command-data crash changes targets the game's respawn state machine.

The collector spends 45.45 wall-seconds inside sampling calls during a 69.90-second
session. This is elapsed time, **not** collector CPU time or proof that the collector
caused the loading delay. It nevertheless exposes avoidable diagnostic work: a
system-wide process/thread census on every sample and repeated queries for unnamed
threads. Schema 2 limits the census to twice per second and empty-name retries to
three per thread, while retaining approximately 100 ms counter samples and 10 ms
marker polling. Maximum sampler call duration is recorded as well. New threads can
take up to one census interval, approximately 500 ms, to appear in the report.

Further code-level investigation needs the matching decrypted **`eboot.bin` from
game version 1.09**. The report contains guest offsets, but no instructions at those
offsets; the executable is unavailable in this workspace. An entire game directory
or save data is not needed for that analysis.

## What was checked

- The host runtime has no explicit death-specific loading pause. Its guest sleep
  imports already use high-resolution Windows waitable timers.
- Semaphore and condition-variable waits follow guest requests. Removing these
  waits would change synchronization rather than establish the source of the delay.
- Reusing or unmapping GPU resources can drain recording work and perform readbacks.
  These paths exist in `Rasterizer::InvalidateMemory`, `Rasterizer::UnmapMemory` and
  `BufferCache::ReadMemory`. Their presence does not show that they cause this report.
- The capture is available, but the game executable and a local session for replay
  are unavailable. The host code and aggregated counters cannot identify the guest
  instruction responsible for the pause.

## Capture a comparison

Use **Trace Respawn Loading.cmd** beside `Bloodborne.exe`. It works with the packaged
launcher and requires no separate Python installation.

1. With the game focused, press **F8** immediately before choosing **Continue**.
2. Press **F8** when the character becomes controllable.
3. After dying, press **F8** when the loading screen appears. Press **F8** again when
   the respawned character becomes controllable.
4. Repeat step 3 for two deaths in the same area, then close the game normally.

Keep the session short, approximately two minutes. Send `logs/loading-*.zip` under
the port's data folder. F8 observes a key press without blocking or injecting input;
the game retains its usual handling of that key. Markers are manual observations
polled approximately every 10 ms, plus human reaction time. Process and thread
counters are sampled approximately every 100 ms.

Normal launches keep these diagnostics disabled. The script passes diagnostic
options to this launch only and does not edit launcher settings, `bbport.ini`, game
patches, frame limits or synchronization. Diagnostic logging has overhead; compare
both loading paths within the same capture.

## Report contents and limits

| File | Contents |
| --- | --- |
| `markers.csv` | F8 observations in order. Pairs 1–2, 3–4 and so on delimit the manually marked loading intervals. |
| `process.csv` | Cumulative kernel/user CPU time and logical I/O counters for this launch's native game processes, sampled approximately every 100 ms. |
| `threads.csv` | Cumulative CPU time and available Windows names for those processes' threads. Creation times distinguish reused IDs. |
| `game.log` | Console output with capture receipt times, limited to 16 MiB while the output pipe continues to drain. |
| `frames.csv` | Existing per-flip frame timing, uploads, allocations, compilation and GPU waits. |
| `waits.log` | Existing guest wait call sites in approximately five-second report windows. |
| `readbacks.csv` | Existing GPU buffer readbacks, their duration and completion state. |
| `metadata.json` | Capture duration, exit code, observed process IDs, markers and sampler wall time. |

Process, thread and marker times share the capture clock. `game.log` timestamps
show when a line reached the collector; native buffering can delay a line.
The existing frame log starts at the first flip, and the readback log starts at
logger creation. Their `t_s` values have different origins and must not be directly
subtracted from marker times. Guest waits are aggregated across report windows.

Logical I/O includes cached file access and pipes; it is **not physical disk
throughput**. Summed thread CPU and wait times can exceed elapsed wall time because
threads run or wait concurrently. CPU counters show activity, not a guest call
stack. A quiet interval is evidence to investigate a wait or timer, not proof of one.

The script samples only the process tree it launches. It does not suspend threads,
read process memory, capture the screen or upload anything. The ZIP uses an explicit
file allowlist: it does not copy saves, settings, dumps or game assets. Console logs
can contain local paths. Missing native logs can indicate that launch preparation
failed or that explicit diagnostic overrides in Advanced settings took precedence.

The automated fixture checks actual Windows process/thread CPU and I/O sampling,
Unicode output paths and ZIP export through the packaged Python runtime. These
checks validate the diagnostic tool, not gameplay loading performance.
