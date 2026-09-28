# UI Performance Baseline

Milestone 5, Phase 0. These are the numbers the UI performance work is measured against. Re-run the same commands after each change and compare.

## Targets

| Measure | Target |
|---|---|
| Response to a click, key or drag | Visible within one 60 Hz frame (≤ 16.7 ms paint per input event) |
| Playhead and meters during playback | Steady 60 fps, well under one frame of paint |
| Message-thread stalls | None over 50 ms after startup |
| Idle UI CPU | Under 2 % |

## How to measure

Build the profiling configuration in Release (Debug timings are not meaningful):

```powershell
cmake -S AerionDawCpp -B build-profiling -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_CONFIGURATION_TYPES="Debug;Release" `
  -DAERION_ENABLE_PROFILING=ON -DAERION_BUILD_TESTS=ON
cmake --build build-profiling --config Release --target AerionBench AerionDaw --parallel
```

**Headless benchmark:** paints the real Timeline and Mixer offscreen with both renderers:

```powershell
.\build-profiling\AerionBench_artefacts\Release\AerionBench.exe --tracks=32 --clips=20 --frames=200
```

**In the app:** the profiling build writes a report every 5 s to `%APPDATA%\AerionDAW\aerion.log`. Each report has per-zone paint times and a `message thread:` line from the watchdog. The watchdog pings the message queue every 100 ms and records how long each ping waits, which is the delay a click or key press would see.

## Baseline: 2026-09-27

Machine: AMD Ryzen 7 5800H, integrated Radeon graphics (no dedicated GPU), 29 GB RAM, Windows 11. This CPU is fast for its class; expect older hardware to be 2 to 3 times slower.

### Headless benchmark (1920×1080, 32 tracks × 20 clips)

| Scenario | Direct2D (native) | Software |
|---|---:|---:|
| Timeline full repaint | 19.2 ms | 32.6 ms |
| Timeline playhead strip (16 px) | 13.7 ms | 1.2 ms |
| Mixer full repaint | 5.9 ms | 8.1 ms |
| Mixer meters area | 5.7 ms | 7.3 ms |
| Clip drag: Edit update | 0.03 ms | 0.03 ms |
| Clip drag: repaint | 19.3 ms | 33.1 ms |

At 64 tracks × 40 clips the numbers are within about 10 % of these, so row culling works and cost scales with the visible area, not with project size.

### In the app (profiling build, idle project)

| Measure | Result |
|---|---|
| Main window ready | 3.6 s after launch |
| Startup stall, cold first launch of a new build | 10.9 s |
| Startup stall, warm launch | 0.5 s (audio device init: 436 ms on the message thread) |
| Idle ping latency after startup | 1.8 to 2.1 ms avg, 3.9 ms max, no stalls |

## Findings

1. **Every clip drag misses the frame budget.** Each mouse move repaints the whole Timeline: 19 ms with Direct2D, 33 ms with software. That is where drag lag comes from. The Edit update itself costs 0.03 ms, so the fix is the repaint, not the model.
2. **Direct2D ignores the small repaint region.** A 16 px playhead strip costs 72 % of a full repaint with Direct2D but only 4 % with the software renderer. Direct2D is faster for full repaints and more than 10 times slower for the small repaints that playback does 25 times a second.
3. **The Mixer's meter-only repaint saves almost nothing.** The meters area covers nearly the whole console, so it costs 95 % of a full Mixer repaint, every tick during playback.
4. **Track rows dominate Timeline paint** (about 15 of 18 ms), mostly per-row drawing work, not the number of clips.
5. **Audio device init blocks the message thread for about 0.5 s** on every launch, and the first launch of a new build froze the UI for 10.9 s.

## Progress

### Phase 1.1: Timeline culling and clip-drag repaint (2026-09-28)

- `drawTrackRow` now skips a row's header, and any clip, that lies outside the repainted area horizontally. Before, it only skipped rows outside it vertically.
- Clip drags (move, trim, fade) repaint only the clip's old and new area on each mouse move, then repaint the whole Timeline once on mouse-up. The selected clip's property-change listener no longer triggers a full refresh during the drag.
- `AerionBench --verify` checks that partial repaints give exactly the same pixels as a full repaint. The full-repaint image is byte-identical to the one before this change.

Same machine and project (32 tracks × 20 clips, 1080p):

| Scenario | Direct2D before | Direct2D after | Software before | Software after |
|---|---:|---:|---:|---:|
| Clip drag, per mouse move | 19.4 ms | **2.5 ms** | 33.1 ms | **0.08 ms** |
| Timeline playhead strip | 13.7 ms | **4.6 ms** | 1.2 ms | **0.35 ms** |
| Timeline full repaint | 19.2 ms | 19.0 ms | 32.6 ms | 31.1 ms |

Still open: the full Timeline repaint (scroll, zoom) is over one frame, the Mixer meters repaint is about the same as a full Mixer repaint, and Direct2D still costs over 10× the software renderer for the playhead strip.

## Caveats

- Offscreen Direct2D numbers are CPU submission time. GPU work that finishes after the Graphics context ends is not included, and a real window's swap chain may clip differently. Confirm finding 2 in the app before acting on it.
- The benchmark has no audio device, so the playback graph rebuild that the app runs after an edit is not measured.
- The 10.9 s cold-start stall has not been attributed yet. Candidates are first-run plugin scanning, font caching and antivirus scanning of the new executable.
