# Performance Baseline

Milestone 5. These are the numbers the UI and audio performance work is measured against. Re-run the same commands after each change and compare. The audio side has its own section at the end: [Audio](#audio).

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

Add `--scale=1.25` (with `--width` / `--height` set to the logical window size) to measure a scaled display, and `--verify` to check partial repaints and scrolling against full repaints.

**Before and after a change:** save the timings, make the change, and compare. `--compare` lists every timing against the saved one and returns exit code 3 if any is more than 20 % slower (changes under 0.05 ms, or 0.5 % of an audio block, count as noise). Use the same arguments both times; it warns when the machine, build type or arguments differ. Two runs with no change in between stay within about 10 %, except sub-0.05 ms timings.

```powershell
.\build-profiling\AerionBench_artefacts\Release\AerionBench.exe --save-baseline=build-profiling\bench-baseline.json
# ... change, rebuild ...
.\build-profiling\AerionBench_artefacts\Release\AerionBench.exe --compare=build-profiling\bench-baseline.json
```

The same options work with `--audio`.

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

### Scroll by copying (2026-10-03)

A scroll now moves the Timeline's cached lane pixels (`CachedLayer::scroll`) and draws only the strip scrolled into view; a sideways scroll also redraws the ruler, whose labels are laid out per view. A first prototype was dropped because the copied pixels did not match a fresh render. What made them match:

- Horizontal scroll position in whole pixels (`Timeline::scrollPx`), and `timeToX` on a 1/256 px grid rounded with `floor (x + 0.5)`, so every position moves by exactly the scroll distance.
- Waveforms in 256 px tiles counted from each clip's left edge, not one image of the visible part; newly visible tiles are the only ones rendered.
- A flat lane background instead of the window gradient, which is fixed to the screen.
- Borders next to the lanes drawn as crisp 1 px fills on their own side, not anti-aliased into the lanes; rows culled with the extra pixel their bottom line reaches into.
- The automation curve runs through the points just beyond the view instead of being anchored at its edges.
- Clip frames, waveform tiles and icons set their opacity before drawing images. They used whatever colour was set before them, so a partial repaint could draw a clip body or waveform transparent.

Screen-fixed overlays (drag previews, the razor line, value tooltips, editors) and recording fall back to a full repaint, as does Direct2D (moving its pixels would mean a GPU readback). Scaled displays fell back too until October 5 (see UI scaling below).

`AerionBench --verify` scrolls 1 to 3000 px in both directions and compares with a full repaint. A few dozen anti-aliased pixels on curves come out up to 4 levels (of 255) apart, because the same curve drawn at a different place on screen rounds slightly differently; the checks allow that and nothing more. Software renderer, Release, 1080p, 32 tracks:

| Scenario | Before (full repaint) | Now |
|---|---:|---:|
| Scroll 40 px sideways | 13.5 ms | **5.0 ms** |
| Scroll 60 px down | 13.5 ms | **4.0 ms** |

Most of the remaining time is copying the window-sized layer and walking every row and clip to find what touches the new strip.

### UI scaling: View → UI Size and scaled displays (2026-10-05)

**View → UI Size** (Auto / 100–200 %) scales the whole interface through JUCE's global scale factor, on top of Windows scaling. Auto picks 125 % on a 1440p display at 100 % Windows scaling and 150 % on 4K at 100 % (`UI/UiScale.h`). `AerionBench --scale=<factor>` paints every scenario and `--verify` check the way a scaled window does: physical-size images, repaint areas rounded out to physical pixels.

Measured first, scaling cost far more than its extra pixels: at 125 % a full Timeline repaint took 59 ms instead of 13 ms, and scrolling fell back to that full repaint. Clip frames and waveform tiles were cached at 100 % and resampled on every paint (0.05 → 0.32 ms and 0.02 → 0.18 ms per clip). What changed:

- Clip frames, waveform tiles and icon rasters are rendered at physical resolution and drawn at whole physical pixels (`drawPhysicalImage`), so they stay plain copies and sharp. The clip-frame nine-slice is cut into sub-images at whole physical pixels, so slices meet without the overlap that clipping would leave.
- The cached Timeline layer is copied to the window at the nearest physical pixel instead of being resampled when the window puts it at a fractional position.
- Scroll by copying works scaled: scroll positions snap to the smallest step that is a whole number of physical pixels (4 px at 125 %, 2 px at 150 %), `CachedLayer::scroll` moves physical pixels, and a logical pixel along each edge of the moved area is redrawn because those physical pixels also show what lies next to it.
- Positions are rounded with `floor (x + 0.5)`; `roundToInt` rounds halves to even, and a 4 px scroll at 125 % moves a x.5 position by 5 px, flipping its rounding.
- Areas the layer redraws start from the lane background when scaled. Fills meeting inside a physical pixel each cover part of it, and the pixel kept a share of whatever it held before, so a redraw differed from a fresh render.

`--verify` passes at 100, 125, 150, 175 and 200 %. Scaled, it allows 24 levels instead of 4 per channel: rounded button corners flatten slightly differently at different absolute positions and differ by up to about 15 levels in their anti-aliased pixels after a scroll; missing or misplaced content differs by far more.

Software renderer, Release, 32 tracks × 20 clips, the window filling the display (the logical size shrinks as the scale grows):

| Display, UI Size | Logical size | Full repaint | Scroll 40 px sideways | Scroll 60 px down | Playhead | Clip drag |
|---|---|---:|---:|---:|---:|---:|
| 1080p, 100 % | 1920 × 1080 | 14.8 ms | 5.1 ms | 4.0 ms | 0.10 ms | 0.09 ms |
| 1440p, 125 % | 2048 × 1152 | 26.1 ms | 12.7 ms | 10.0 ms | 0.15 ms | 0.09 ms |
| 1440p, 150 % | 1706 × 960 | 23.5 ms | 10.6 ms | 9.8 ms | 0.16 ms | 0.08 ms |
| 1440p, 200 % | 1280 × 720 | 21.2 ms | 10.0 ms | 9.5 ms | 0.18 ms | 0.08 ms |
| 4K, 150 % | 2560 × 1440 | 57.6 ms | 24.1 ms | 20.1 ms | 0.23 ms | 0.10 ms |

Before these changes, 1440p at 125 % measured 59 ms for a full repaint and the same for every scroll step. Costs now follow the pixel count (1440p has 1.78× the pixels of 1080p). Scrolling on 4K exceeds a frame with the software renderer, but Auto uses Direct2D on displays above 2560 × 1600: 16.2 ms full repaint and 16.9 ms per scroll step there (CPU submission time).

### Piano Roll measured (2026-10-07)

`AerionBench` now paints the Piano Roll with a dense clip: 2000 notes between C2 and C6 and 1000 CC1 events over 64 bars, 1920 × 600, scrolled so the notes fill the view. `--verify` checks three partial repaints with every note selected (all pass).

Release, 32 tracks × 20 clips in the project, the clip on a track of its own:

| Scenario | Software | Direct2D |
|---|---:|---:|
| Full repaint, nothing selected | 5.4 ms | 5.0 ms |
| Full repaint, all notes selected | 62.6 ms → **7.1 ms** | 64.0 ms → **5.3 ms** |

Selecting notes made the Piano Roll about 12× slower: with all 2000 selected, a repaint took almost four frames, so dragging or editing a large selection stuttered. `drawPianoKeys` called `getSelectedNotes()` once per visible key row, and each call copied the clip's note array and searched it for every selected note; `drawGrid` did the same check per row and `drawNotes` searched the selection per note.

Fixed: `paint()` now works out the selection once (`prepareSelectionForPaint`: a set of the selected notes and a table of the 128 highlighted pitches), the grid, keys and notes look it up, and `getSelectedNotes()` checks the selection against the clip in one pass instead of one search per selected note. Renders with and without a selection, at 100 % and 150 %, are byte-identical before and after (`--snapshots`); `--verify` passes.

### Piano Roll playhead measured (2026-10-09)

`AerionBench` now reports "piano roll playhead move": the two 4 px strips (where the line was, where it is) that one display frame repaints during playback, on the same dense clip. Release, 1920 × 600:

| Scenario | Software | Direct2D |
|---|---:|---:|
| Full repaint | 5.6 ms | 5.4 ms |
| Playhead move | **0.95 ms** | 3.1 ms |
| Timeline playhead move (cached layer), for comparison | 0.11 ms | 1.4 ms |

The move is within budget (6 % of a frame with the software renderer) but costs 17 % of a full repaint for under 1 % of the area: the strip redraws the full height of keys, grid, notes and lanes. A cached layer like the Timeline's would bring it near 0.1 ms; not done yet. Direct2D figures are CPU submission time (see [Caveats](#caveats)).

### Fixed along the way: crash when releasing the Edit

`AudioEngineManager` kept each track's LevelMeterPlugin alive in `trackMeters` and released it only after the Edit was destroyed or replaced. If that was the last reference, the plugin's destructor called into the dead Edit (`Edit::getParameterChangeHandler`). This affected quitting, opening a project and creating a new one. It showed up as the benchmark crashing on exit in about half of its runs, and it now releases meters and thumbnails before the Edit goes away (0 crashes in 10 runs).

## Caveats

- Offscreen Direct2D numbers are CPU submission time. GPU work that finishes after the Graphics context ends is not included, and a real window's swap chain may clip differently. Confirm finding 2 in the app before acting on it.
- The paint benchmark has no audio device, so the playback graph rebuild that the app runs after an edit is not part of its numbers. `AerionBench --audio` measures it on its own (see [Audio](#audio)).
- The 10.9 s cold-start stall has not been attributed yet. Candidates are first-run plugin scanning, font caching and antivirus scanning of the new executable.

## Audio

### Targets

| Measure | Target |
|---|---|
| Time to process one block, 99th percentile | Under 50 % of the block's duration for the reference project at 128 samples |
| Heap allocations on the audio thread | None in steady-state playback |
| Playback graph rebuild after an edit | Under 50 ms |

### How to measure

```powershell
.\build-profiling\AerionBench_artefacts\Release\AerionBench.exe --audio [--audio-tracks=32] [--audio-blocks=3000]
```

`AerionBench --audio` builds a reference project and plays it through Tracktion's playback graph block by block, the way an audio device would, using Tracktion's hosted audio interface instead of a sound card (so it runs on machines and CI runners without audio hardware). The project has 32 audio tracks, each with a clip, an EQ, a compressor, a send to one of two reverb buses and a volume automation curve. For each block size (128 and 256 samples at 48 kHz) it reports the time per block as a share of the block's duration, with one audio thread and with all CPUs, with and without Tracktion's pooled memory options. A replaced global allocator counts heap allocations made on the calling (device) thread during each block; worker threads of the multi-threaded graph are not counted. It fails (exit code 2) if the output is silent or the allocation counter does not work.

### Results: 2026-10-06 (16 CPUs, Release)

| Configuration | 128 samples (2.67 ms): mean / p99 / max | 256 samples (5.33 ms): mean / p99 / max | Allocations on the audio thread |
|---|---|---|---|
| 1 thread | 8.4 / 11.2 / 13.3 % | 7.3 / 9.0 / 11.8 % | 0 |
| **All CPUs (Aerion's setting)** | **3.5 / 4.6 / 7.7 %** | **2.4 / 3.0 / 3.6 %** | **0** |
| 1 thread, pooled memory | 9.4 / 14.5 / 16.2 % | 7.7 / 9.7 / 13.7 % | 0 |
| All CPUs, pooled memory | 5.2 / 6.5 / 8.1 % | 3.3 / 3.9 / 4.3 % | in 5 and 2 of 3000 blocks |

Graph rebuild: median 1.4 ms, max 1.8 ms.

**Loudness meter (2026-10-09):** `Aerion::LoudnessAnalyser` runs on every output block (Tracktion's global output processor), stopped or playing. Release: **0.23 %** of a 256-sample block at 48 kHz, 0 allocations (`AerionBench --audio`, "[loudness meter]"; the run fails if it allocates).

All targets are met. Findings:

1. Tracktion's defaults already run the graph on every CPU, which is 2.4 to 3× faster per block than one thread. Aerion keeps them.
2. Tracktion's pooled memory and node memory sharing are slower here and occasionally allocate on the audio thread, so they stay off.
3. Aerion's own code on the audio thread is the plugin crash guard (`PluginFaultMonitor::process`), which does not allocate or lock.
4. Tracktion asks for a higher process priority while an Edit plays. Aerion used to ignore that request (an empty `EngineBehaviour::setProcessPriority`); it now raises the process to high priority during playback and back to normal afterwards, so other programs cannot starve the audio. This is not visible in the benchmark, which runs on an idle machine.
5. Third-party plugins are outside these numbers: what they cost, and whether they allocate, depends on the plugin.

