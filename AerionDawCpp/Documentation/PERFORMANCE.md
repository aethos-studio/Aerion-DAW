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

### Phase 1.2: Mixer meter repaint (2026-09-28)

- Each playback tick repaints only each strip's two meters, the fader cap between them and the peak readout (`faderLiveAreas` in `UI/Primitives.h`). A whole strip is repainted only when automation has moved its fader or pan since it was last drawn.
- `Mixer::paint` skips the header, and any strip or strip section outside the repainted area, reusing that strip's hit areas from the previous paint. Each strip paints with the clip narrowed to its own bounds. Without that, the software renderer paid for the whole list of repaint rectangles on every shape and got slower (15.2 ms).
- The Timeline's culling now uses `clipRegionIntersects`, which tests each repainted rectangle rather than their combined bounds.
- `AerionBench --verify` also checks the Mixer's playback region and an arbitrary slice of the console against a full repaint.

| Mixer, 32 tracks, 1080p | Direct2D before | Direct2D after | Software before | Software after |
|---|---:|---:|---:|---:|
| Meters repaint per playback tick | 5.7 ms | **3.3 ms** | 7.3 ms | **2.4 ms** |
| Full repaint | 5.9 ms | 5.2 ms | 8.1 ms | 6.5 ms |

The remaining meter cost is mostly the fader cap SVG, drawn for every visible strip on each tick. Caching it as an image is part of the planned SVG caching work.

### Phase 1.3: Graphics engine setting (2026-09-28)

JUCE 8 opens every window with Direct2D, and the numbers above show it is the slower choice for the partial repaints that playback and editing consist of (playhead strip 4.1 ms vs 0.37 ms software, clip drag 1.9 ms vs 0.08 ms). **View → Graphics Engine** now offers:

- **Auto** (default): the software renderer, unless the largest display has more than 2560 × 1600 physical pixels. There, full repaints (resizing, scrolling) start to dominate and Direct2D's faster full repaint wins. The menu shows which engine Auto picked.
- **Hardware Accelerated**: Direct2D on Windows, CoreGraphics on macOS.
- **Software**: JUCE's CPU renderer.

`GraphicsEngine::Policy` (`UI/GraphicsEngine.h`) applies the choice to every window, because JUCE has no app-wide switch: when the main window appears, when the setting changes, and whenever focus moves to a newly opened window. The choice is saved in the user settings and logged at startup (`Graphics engine: Auto -> ...`). `AerionBench --verify` checks that a window really switches in both directions.

The 2560 × 1600 threshold is a heuristic and should be revisited once Timeline layers make full repaints cheap. The benchmark numbers are CPU submission cost measured offscreen. To compare engines in a real window, switch in the View menu and read the `Timeline::paint` and `message thread` lines of a profiling build's log.

### Phase 1.4: One display-synced UI clock (2026-09-28)

Before, four timers drove the UI independently: MainComponent at 25 Hz (playhead, meters, transport), the Inspector at 20 Hz, the tooltip window at 20 Hz, and the plugin manager at 10 Hz while it was open. The Inspector repainted its fader 20 times a second whenever a track was selected, even with the transport stopped, and the tooltip polled the mouse the whole time the app was open.

Now:

- A `juce::VBlankAttachment` on MainComponent drives everything that animates. The playhead moves on every display frame (smooth at the monitor's refresh rate instead of 25 Hz). Meters, the transport readout and live recording rows update at most 30 times a second.
- Meters keep updating for 1.5 s after the transport stops, so they fall back to silence instead of freezing at their last level.
- The Inspector has no timer. The display clock repaints only its meter column, and only while audio is moving.
- The tooltip poll runs only after mouse activity and stops once the mouse is still and no tip is waiting to appear.
- MainComponent's own timer runs at 5 Hz for slow chores: the idle transport readout, the auto-save countdown (now in real elapsed time) and profiling reports.

When idle, the display clock still fires every frame, but each idle frame only reads the transport state and returns. Nothing repaints while nothing changes.

### Phase 2: Timeline layers, cheaper full repaints, cached chrome, Lightweight UI, startup (2026-09-28)

A per-zone profile of a full Timeline repaint (software, 29 ms) showed where the time went: clip frames 8.7 ms, page chrome 6.5 ms (mostly the full-window background gradient), waveforms 6.1 ms, clip text 1.8 ms, row headers 1.6 ms. `AerionBench --full-only` with a profiling build prints this breakdown.

- **Timeline layers.** The Timeline renders into its own cached layer (`UI/CachedLayer.h`) and is marked opaque. The playhead is a separate, click-through overlay above it (`TimelinePlayheadOverlay`), so moving it copies cached pixels instead of re-running `Timeline::paint`. The cache image matches the window's renderer. JUCE's own `setBufferedToImage` always uses a Direct2D image, which would mean a GPU readback on every copy with the software renderer, and Direct2D rendering whatever engine was chosen.
- **Clip frames** are assembled from a nine-slice image cached per colour, height and state (`UI/ClipFrame.h`), instead of rasterising rounded-rectangle paths and strokes per clip.
- **Waveforms** are cached as opaque images matching the renderer and drawn at whole pixels, so drawing them is a plain copy. Clip edges are snapped to whole pixels for this. This also fixed waveforms not showing with the software renderer: the cached waveform images were Direct2D images.
- **Background gradients** are drawn as a few solid bands with the software renderer (a subtle vertical gradient has only a few dozen distinct row colours) instead of a per-pixel gradient.
- **Icons and the fader cap** are drawn from rasters cached per size, colour, display scale and renderer (`Icons::drawRasterised`) instead of rasterising SVG paths on each paint.
- **Lightweight UI** (View → Lightweight UI: Auto / On / Off; Auto turns it on with 2 or fewer physical cores or under 6 GB of RAM): flat fills instead of decorative gradients, square opaque clip bodies, meters and transport readout at 20 Hz, playhead at 30 Hz.
- **Audio device startup** stays on the message thread: Tracktion asserts the message thread for parts of it, and ASIO drivers are COM objects tied to the thread that creates them. Instead, the splash now waits, showing "Starting audio devices...", until devices are open (at most 4 s), so the half-second stall no longer freezes the splash fade or the freshly shown main window.

Same machine and project (32 tracks × 20 clips, 1080p):

| Scenario | Direct2D baseline | Direct2D now | Software baseline | Software now |
|---|---:|---:|---:|---:|
| Timeline full repaint (scroll, zoom) | 19.2 ms | **7.6 ms** | 32.6 ms | **14.7 ms** |
| Playhead move | 13.7 ms | **1.5 ms** | 1.2 ms | **0.10 ms** |
| Clip drag, per mouse move | 19.4 ms | **2.1 ms** | 33.1 ms | **0.08 ms** |
| Mixer meters, per playback tick | 5.7 ms | **2.4 ms** | 7.3 ms | **1.7 ms** |
| Mixer full repaint | 5.9 ms | 4.0 ms | 8.1 ms | 3.9 ms |
| Toolbar / transport full repaint | – | 0.6 / 0.8 ms | – | 0.17 / 0.22 ms |

Everything now fits within a 60 Hz frame on this machine. The software full repaint (14.7 ms) is the closest to the limit, and on hardware 2–3× slower scrolling will still drop frames.

**Tried and not shipped: scroll by copying.** Moving the cached pixels on a scroll and rendering only the exposed strip would make scrolling nearly free. A prototype got within a few hundred pixels of a fresh render, but items crossing the edge of the moved area (the ruler's label gutter, the first clip) and the screen-fixed background gradient for vertical scrolls did not match exactly. It was removed rather than shipped with visible seams. It remains the next step for scroll performance on slow machines.

### Fixed along the way: crash when releasing the Edit

`AudioEngineManager` kept each track's LevelMeterPlugin alive in `trackMeters` and released it only after the Edit was destroyed or replaced. If that was the last reference, the plugin's destructor called into the dead Edit (`Edit::getParameterChangeHandler`). This affected quitting, opening a project and creating a new one. It showed up as the benchmark crashing on exit in about half of its runs, and it now releases meters and thumbnails before the Edit goes away (0 crashes in 10 runs).

## Caveats

- Offscreen Direct2D numbers are CPU submission time. GPU work that finishes after the Graphics context ends is not included, and a real window's swap chain may clip differently. Confirm finding 2 in the app before acting on it.
- The benchmark has no audio device, so the playback graph rebuild that the app runs after an edit is not measured.
- The 10.9 s cold-start stall has not been attributed yet. Candidates are first-run plugin scanning, font caching and antivirus scanning of the new executable.
