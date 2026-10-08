# Respawn loading on Windows

The reported loading screen lasts approximately **3–4 seconds after death**.
Loading through **Continue** is much faster; teleportation does not show the delay.
The cause has **not yet been established**, and this change does not claim a respawn
performance fix.

## What was checked

- The host runtime has no explicit death-specific loading pause. Its guest sleep
  imports already use high-resolution Windows waitable timers.
- Semaphore and condition-variable waits follow guest requests. Removing these
  waits would change synchronization rather than establish the source of the delay.
- Reusing or unmapping GPU resources can drain recording work and perform readbacks.
  These paths exist in `Rasterizer::InvalidateMemory`, `Rasterizer::UnmapMemory` and
  `BufferCache::ReadMemory`. Their presence does not show that they cause this report.
- The game executable and a reproducible death-to-respawn session are unavailable
  in this workspace. A timer inside the game, world reset work, saving and GPU waits
  remain possible explanations. The host code alone cannot distinguish them.

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
