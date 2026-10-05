# Aerion DAW Project Status — October 5, 2026 (v0.3.0 Pre-Alpha)

## Overview
Aerion DAW has closed the **Milestone 4 completion sprint** (v0.3.0). Milestones 1–4 — Editing, Mixing, Recording & Monitoring, and Project & Workflow — are **complete and verified against the source** (codebase audit, July 2026). The current focus is **Milestone 5: Polish & Stability (v0.4.0)**. In September 2026 the M5 performance work was largely completed: everything measured now fits a 60 Hz frame at 1080p (Timeline layers, cached chrome and icons, a display-synced UI clock, **View → Graphics Engine** and **View → Lightweight UI**), the icon system was fixed, unsaved-changes tracking was corrected, and crash and data-loss bugs were closed, including save prompts that acted on the wrong buttons. Plugins that crash while processing audio are now caught on Windows, and `UIComponents.h` is split into one header per view. The app log now shows in a **Console** bottom-panel tab, a crash leaves a report that the next launch points to, and CI runs `AerionBench --verify`. **View → UI Size** (October 5) makes the interface readable on large monitors and keeps the Timeline's speed-ups on scaled displays. Tracks can be resized by dragging their bottom edge. Remaining in M5, in order: the last performance item (audio-side hot-path review), plugin crash protection beyond audio processing, accessibility, and packaging sign-off. Scrolling the Timeline now moves its cached pixels. CI builds and smoke-tests on **both Windows and macOS**; release packaging is a separate workflow; both are **manual (`workflow_dispatch`) only**. After M5, **Milestone 6: Complete Core (v0.5.0)** closes the remaining gaps to a modern DAW baseline before the differentiator milestones (M7–M9).

## Milestone Progress

| Milestone | Status |
|---|---|
| M1: DAW Essentials — Editing (v0.0.1) | **Complete** — clip/MIDI/transport editing including MIDI CC / Pitch lane in the Piano Roll |
| M2: DAW Essentials — Mixing (v0.1.0 → v0.1.1) | **Complete** — Phase/Mono, HPF/LPF, Inserts (incl. **bypass + drag-to-reorder**), Sends, Presets, Snapshots, clip + K-14 metering, submix folders + routing, cascading mute/solo |
| M3: DAW Essentials — Recording & Monitoring (v0.2.0) | **Complete** — Metronome, Count-In, Punch In/Out, PDC, multi-channel input routing, buffer safety readout, live recording waveform, full driver pack (ASIO/WASAPI/DirectSound/CoreAudio/ALSA/JACK/WinRT MIDI), Reset Audio Settings, per-track monitor modes, per-track MIDI controller selector |
| M4: DAW Essentials — Project & Workflow (v0.3.0) | **Complete** — Save/Load (`.aerion`), Collect & Save, Bounce/Freeze, Mixdown + Stems export, Tempo Map, Time Signature changes, per-track input/monitor persistence, customisable keyboard shortcuts (`AerionKeymap`), Recent Projects, Auto-save / Crash Recovery, icon system |
| M5: DAW Essentials — Polish & Stability (v0.4.0) | **In progress** — see inventory below |
| M6: DAW Essentials — Complete Core (v0.5.0) | Not started — stock instruments and effects, full parameter automation, time-stretch, sidechain, MIDI learn and control surfaces, clip processing, templates, loudness metering, CLAP spike |
| M7–M9 + Future USPs | Not started — `AIManager` is still a 2-second mock; ONNX Runtime declared in CMake but intentionally not linked |

---

## Milestone 5 Inventory (updated October 3, 2026)

What already exists versus what remains, verified against the source tree:

| M5 Item | State | Notes |
|---|---|---|
| CI pipeline | **Done** | `.github/workflows/build-test.yml` — Debug build + `ctest` smoke tests, **manually triggered** (`workflow_dispatch`), running on **both** a Windows MSVC/Ninja runner and a macOS Clang/Ninja runner. |
| Release packaging workflow | **Done** | `.github/workflows/package-release.yml` (`release-package`) — manual (`workflow_dispatch`) workflow, independent of the smoke-test workflow, that builds the Windows NSIS installer and macOS DMG and publishes them to a GitHub Release (tag/draft/pre-release/notes are run inputs). The `platforms` input builds one platform alone; a failed platform no longer blocks the other, and a later run with the same tag adds its installer to that release. |
| Unit & Integration Tests | **Started** | `AerionTests`: `ProjectData` round-trip, `AerionKeymap` serialisation/conflicts, `AudioEngineManager` smoke tests (tracks, mute/solo, tempo map, snapshots, transport flags, freeze/unfreeze restore), `GraphicsEngine` choice logic, `PluginFaultMonitor` (crash caught and silenced, JUCE's lock released, skip / report once / re-enable) and dialog button mapping against JUCE's real alert window. The fault and dialog tests are verified on Windows; CI has not been run on them yet. `AerionBench --verify` checks partial repaints pixel for pixel and window engine switching, and now runs in CI. No GUI or audio-thread tests yet. |
| Packaging — Windows | **~Done** | NSIS installer scaffolded in `CMakeLists.txt` (CPack, shortcuts, VC++ runtime bundling, installer icon). **Optional self-signed code signing** now available (`AerionDawCpp/Tools/New-AerionSelfSignedCert.ps1` + `WINDOWS_CERT_PFX_BASE64` / `WINDOWS_CERT_PASSWORD` secrets); a paid OV/EV certificate is still required to clear the SmartScreen "unknown publisher" prompt. |
| Packaging — macOS | **Partial** | DMG + universal binary (ARM64 + x86_64) built by the release workflow; signing is ad-hoc only, **notarization not implemented** (requires a paid Apple Developer account). |
| High-DPI / UI scaling | **Done** | **View → UI Size** (Auto / 100–200 %) scales every window through JUCE's global scale factor; Auto picks 125 % on 1440p and 150 % on 4K at 100 % Windows scaling (`UI/UiScale.h`). Clip frames, waveform tiles and icons render at physical resolution, and scroll by copying works scaled. 1440p at 125 %: full Timeline repaint 59 → 26 ms, scroll step 59 → 10–13 ms. `AerionBench --scale` verifies 100–200 %; CI checks 125 %. |
| Resizable Track Heights | **Done** | Drag a lane's bottom edge in the header column (56–400 px, selected tracks together), double-click to reset, or Track Height in the context menu. Saved per track with the project (`IDs::laneHeight`); `AerionBench --verify` drags an edge and repaints with mixed heights. |
| Performance Optimization | **Mostly done** | Baseline, targets and results in [`PERFORMANCE.md`](./PERFORMANCE.md). Everything measured now fits a 60 Hz frame at 1080p: Timeline full repaint 19–33 → 7.6–14.7 ms, playhead move 13.7 → 1.5 ms (Direct2D) and 1.2 → 0.1 ms (software) via a cached Timeline layer with a playhead overlay, clip drag 19–33 → 0.08–2.1 ms, meters 5.7–7.3 → 1.7–2.4 ms. Also shipped: **View → Graphics Engine**, a display-synced UI clock, cached chrome and icons, **View → Lightweight UI**, audio startup behind the splash. `UIComponents.h` is split into one header per view under `Source/Views/`. Scrolling moves the Timeline's cached pixels and draws only the strip scrolled into view (software: 13.5 → 5.0 ms sideways, 4.0 ms down). Remaining: audio-side hot-path review. |
| Icon & Logo Rendering | **Done** | `UI/Icons.h` fits icons to their viewBox and tints them per state; letter buttons for track toggles (`paintLetterButton`); redrawn metronome icon; filter-free in-app logo (`aerion_logo_ui.svg`). |
| Crash & Data-Loss Fixes | **Done** | Track meters and thumbnails released before the Edit is destroyed (crash on quit / open / new project); unfreeze restores trimmed clips exactly and collapsed folders hide their children in paint and scroll height (PR #15); save prompts (Quit, New / Open Project, Open Recent, Crash Recovery) act on the button clicked (`UI/Dialogs.h`). |
| Plugin Crash Protection | **Started** | Windows: a plugin that crashes in `processBlock` is caught, bypassed and reported; the session keeps playing (`PluginFaultMonitor`, Tracktion hook in `Patches/tracktion/`, `/EHa`). Crashes in plugin editors, state save/load or instantiation, and all plugin crashes on macOS, still take the app down; out-of-process hosting is the next step. |
| Unsaved-Changes Tracking | **Done** | Non-edit engine notifications no longer mark the project changed; direct Edit changes (clip drags) now do, through Tracktion's own change tracking. |
| Workspace Layouts | **Done** | **View → Workspace** submenu: built-in Editing / Mixing / Recording presets + save/delete custom layouts. Captures inspector/browser collapse, mixer dock/detach, bottom panel, and console height; custom layouts + last-active layout persist via `appProperties` and restore on launch. |
| Accessibility | **Missing** | No `setAccessibleName()` usage; keyboard-navigable mixer not started. |
| Error Reporting / Console panel | **Done** | The app log (`Logger::writeToLog`) shows in the **Console** bottom-panel tab and still goes to `aerion.log`; `DBG` output is not captured. `CrashReporter` writes a report folder per crash under `AerionDAW/Crashes` (reason, thread, stack, the session's log, a minidump on Windows); the next launch points to it before Crash Recovery. Reports stay local; `std::terminate` / `abort` on Windows are not caught. |
| App version / title bar | **Done** | CMake `project` version **0.3.0**; menu bar + window title use `Theme::windowTitle()` (`<ProjectName> — Aerion DAW`); About dialog reads `ProjectInfo::versionString`. |

---

## Recently shipped

### October 2026

- **Resizable track heights (October 5, M5):** drag a track's bottom edge in the header column to make it taller or shorter (selected tracks resize together), double-click the edge to reset, or choose Small / Normal / Large / Huge from the track's context menu. Clips and waveforms grow with the track, and the height is saved with the project.
- **UI Size and scaled displays (October 5, M5):** **View → UI Size** (Auto / 100 / 125 / 150 / 175 / 200 %) makes the interface readable on large monitors at 100 % Windows scaling; Auto picks 125 % on 1440p. On any scaled display the Timeline now keeps its speed-ups: clip frames, waveforms and icons are cached at physical resolution and copied, not resampled (sharper, and 1440p at 125 % went from 59 ms to 26 ms per full repaint), and scrolling moves cached pixels again (10–13 ms per step instead of a 59 ms full repaint). `AerionBench --scale=<factor>` measures and verifies any scale.
- **Toolbar and Mixer polish (October 4):** the Snap button is a full 28 px toolbar button with its interval label inside the border; a left click anywhere toggles snap and a right click opens the interval menu (clicking its right half used to open the menu). The Mixer's M / S / MONO / FX / i column is wider (34 px), its buttons taller with more space between them, and it starts slightly below the top of the strip body; strips are 8 px wider so the fader keeps its width. A renamed track or folder shows its new name in the Mixer right away (renames were not passed to `ProjectData`, so the Mixer kept the old name until a click). `AerionBench --verify` now drags a track fader and the master fader, stopped and playing.
- **Scroll by copying, fast wheel scrolling:** several scrolls before one paint keep reusing the Timeline's cached pixels instead of falling back to a full repaint; `AerionBench --verify` checks three steps per paint, sideways and down.
- **Release packaging:** `release-package` takes a `platforms` input (both / windows / macos); the release gets whichever installers built, and a later run with the same tag adds to that release. v0.3.6 was packaged as a draft pre-release.

- **Menu bar and Browser (quality of life):** with a menu open, moving the pointer onto another menu title opens that menu and closes the first, without another click. In the Browser's Files tab a single click selects a folder (now highlighted, like the selected file) and a double-click opens it.

- **Scroll by copying (M5 performance):** scrolling the Timeline moves its cached lane pixels and draws only the strip scrolled into view: 13.5 ms → 5.0 ms sideways and 4.0 ms down per wheel step with the software renderer. Direct2D still repaints in full. Along the way, partial repaints could draw clip bodies, waveforms and icons transparent, depending on what had been drawn before them; they now set their opacity.
- **Crash reports and a log console (M5 error reporting):** a crash writes a folder under `AerionDAW/Crashes` with the reason (e.g. "invalid memory access at 0x…"), the thread, a stack trace, the crashed session's log and, on Windows, a minidump. The log copy matters because each launch deletes `aerion.log`. The next launch says the app crashed and offers to show the report, then asks about Crash Recovery as before. A **Console** tab next to Mixer and Piano Roll shows the app log live, while `aerion.log` keeps receiving it. CI now also runs `AerionBench --verify`.

### September 2026

- **Save prompts acted on the wrong buttons (data loss):** JUCE numbers a three-button dialog's buttons 1, 2, 0, and the prompts assumed otherwise. "Save & Quit" quit without saving; in New / Open Project "Cancel" discarded the project and "Save" did nothing; Open Recent saved on "Discard"; Crash Recovery restored on "Discard". All prompts now go through `UI/Dialogs.h`, which names the answers and puts Cancel last (Escape cancels), and a test checks each button against JUCE's real alert window.
- **Plugin crash protection, first step (M5 stability, Windows):** a hosted plugin that crashes in `processBlock` (invalid memory access, division by zero, an escaped C++ exception) is caught by `PluginFaultMonitor`. That block is silenced, the plugin is bypassed and skipped, and a dialog says what happened; the rest of the session keeps playing. Turning the plugin back on lets it run again. Tracktion has no hook around the plugin call, so a small patch in `Patches/tracktion/` adds one, and MSVC builds use `/EHa` so catching a crash releases the lock JUCE holds while the plugin runs. Not yet covered: crashes in plugin editors, state save/load or instantiation, and macOS.
- **`UIComponents.h` split (M5):** the 9,500-line header is now one header per view under `Source/Views/` (Timeline, Mixer, Piano Roll, Inspector, Browser, Transport, menu bar, toolbar, dialogs), with `UIComponents.h` as an umbrella include. Code moved unchanged.

- **UI performance, phase 2 (M5):** the Timeline renders into a cached layer with the playhead on its own overlay; clip frames come from a nine-slice cache, waveforms from opaque whole-pixel images, icons and the fader cap from cached rasters, and background gradients draw as solid bands in software. **View → Lightweight UI** (Auto on weak machines) uses flat fills and lower animation rates. The splash now waits for the audio devices, so their half-second startup no longer freezes the main window. Also fixed: waveforms could be invisible with the software renderer. Numbers in [`PERFORMANCE.md`](./PERFORMANCE.md).

- **Unsaved-changes tracking (M5 stability):** a new, untouched project no longer shows `*My Song*` or asks to save on quit. The audio device finishing startup, plugin scans, and loading, creating or saving a project now send a status notification that refreshes the UI without marking the project changed. At the same time, edits made directly on the Edit (Timeline clip drags and trims), which never went through the engine's broadcast, now count through Tracktion's own change tracking. They had only looked tracked because every project was already marked changed at startup. The title bar picks these up within 200 ms.
- **Display-synced UI clock (M5 performance):** a `VBlankAttachment` on MainComponent replaces the separate 25 / 20 / 20 Hz component timers. The playhead moves every display frame; meters, transport readout and recording rows update at up to 30 Hz and keep running 1.5 s after stop so they decay to silence. The Inspector no longer repaints its fader 20 times a second while idle, the tooltip poll sleeps when the mouse is still, and the auto-save countdown uses real elapsed time.
- **Graphics engine setting (M5 performance):** **View → Graphics Engine** (Auto / Hardware Accelerated / Software), applied to every window through `GraphicsEngine::Policy`. Auto uses the software renderer up to 2560 × 1600 displays, because Direct2D measured 10× slower for the small repaints playback and editing are made of.
- **Timeline and Mixer repaint cost (M5 performance):** clip drags repaint only the clip (19–33 ms → 0.08–2.5 ms per mouse move); the playhead strip skips headers and clips outside it; the Mixer repaints only meters, fader caps and readouts during playback (5.7–7.3 ms → 2.4–3.3 ms per tick). All partial repaints are checked pixel for pixel by `AerionBench --verify`.
- **Performance tooling:** `AerionBench` measures Timeline and Mixer paint with both renderers, a clip-drag scenario, `--verify` pixel checks and `--snapshots` UI renders at 100 % / 150 %; profiling builds log a message-thread stall watchdog. Baseline and results in [`PERFORMANCE.md`](./PERFORMANCE.md).
- **Unfreeze data loss and collapsed folders (PR #15):** unfreezing restores each clip's full saved state (trim, offset, fades) instead of the whole source file; collapsed folders hide their children in paint and scroll height, matching hit-testing.
- **Crash on Edit teardown:** track meters kept the last reference to each track's `LevelMeterPlugin` until after the Edit was destroyed or replaced, and its destructor then called into the dead Edit. Meters and thumbnails are now released first (quit, open project, new project).
- **Icon system and logo:** icons are fitted to their 24×24 viewBox instead of their drawn content (no more oversized or edge-touching glyphs) and tinted per state; transport icons follow their active state again; track toggles are letter buttons (M / S / R / A) in Timeline, Mixer and Inspector; the metronome icon is redrawn; the app uses a filter-free logo variant that shows on dark backgrounds.
- **Roadmap:** new **Milestone 6 — Complete Core** for the gaps to a modern DAW baseline; later milestones renumbered to M7–M9.

### Earlier

- **Workspace Layouts (M5, July 2026):** New **View → Workspace** submenu with built-in Editing / Mixing / Recording presets, "Save Current Layout…" for user-defined layouts, and per-layout delete. A layout snapshots inspector/browser collapse, mixer dock/detach, the active bottom panel (Mixer / Piano Roll), and console height. Custom layouts and the last-active layout persist via `appProperties`; the active layout is reapplied on launch.
- **Piano roll shortcut focus routing (July 2026):** Fixed a data-loss bug where pressing Delete while editing notes in the embedded piano roll could delete the whole MIDI clip; the editor now grabs keyboard focus and focused keys route through `PianoRollEditor::keyPressed` before global shortcuts.
- **CI expanded to Windows + macOS, both workflows manual-only (July 2026):** `build-test.yml` now runs the Debug build + `AerionSmokeTests` on both a Windows MSVC/Ninja runner and a macOS Clang/Ninja runner, triggered manually from the Actions tab. Release packaging is its own workflow, `package-release.yml` (`release-package`), also manual-only, which builds the Windows NSIS installer and macOS DMG independently of the smoke-test workflow and now publishes both artefacts as a GitHub Release (tag/title/draft/pre-release/notes come from the workflow's manual-run inputs).
- **Optional self-signed Windows code signing (July 2026):** `AerionDawCpp/Tools/New-AerionSelfSignedCert.ps1` generates a self-signed code-signing certificate in the CurrentUser store (no admin rights required). When `WINDOWS_CERT_PFX_BASE64` and `WINDOWS_CERT_PASSWORD` repo secrets are set, `release-package` signs and timestamps the built app and NSIS installer with `signtool`; packaging still succeeds unsigned when the secrets are absent.
- **Cross-platform smoke-test fixes (July 2026):** fixed `ProjectData::createMockData()` appending demo tracks onto the constructor's existing empty `Tracks`/`AuxTracks` trees instead of replacing them, which was masking test assertions. Fixed `AerionKeymap::sameKey()` to compare the Ctrl modifier explicitly (not just Command), resolving a macOS-only false conflict between `ctrl+S` (`file.save`) and bare `S` (`track.solo`) — both platforms' physical modifier keys differ from Windows.
- **Roadmap / version / title bar alignment (July 2026):** CMake app version bumped to **0.3.0**; `Theme::windowTitle()` drives menu bar + window title (`<ProjectName> — Aerion DAW`, including on startup); About dialog uses `ProjectInfo::versionString`; ROADMAP/STATUS/README brought in sync with M5 progress.
- **Insert bypass + drag-to-reorder (M2 polish, June 2026):** engine APIs `isExternalPluginBypassed` / `setPluginBypassed` / `moveExternalPlugin` (with freeze-state guards); `BYP` pill and drag-handle reordering in the Inspector INSERTS list, Plugin Manager window, and Mixer insert rack.
- **Milestone 4 completion sprint (v0.3.0):** per-track input + monitor persistence, time signature changes UI, customisable keyboard shortcuts (`AerionKeymap` + `KeyboardShortcutsPanel` with conflict detection and `.aerionkeys` import/export), Mixer M/S icons via shared `drawTrackIconBtn`, freeze/tempo polish pass.
- **Freeze render lifecycle & MIDI arm data-loss fixes** (`270b459`).
- **Arranger Ruler redesign** — Reaper-style dual-band layout (`97fed9a`).

---

## Known scaffolding ahead of its milestone

- **Google Drive client (M9 footprint):** `GoogleDriveClient` implements OAuth2 + PKCE, token persistence, and a Browser "Cloud" tab — but **`clientId` / `clientSecret` are still placeholders** (`YOUR_CLIENT_ID`) until a Google Cloud desktop OAuth client is configured. Do not rewrite from scratch for M9; wire credentials and finish sync semantics instead.
- **ONNX Runtime:** declared via `FetchContent_Declare` in CMake but deliberately not linked (build-size cost); linking is the first M9 task.
- **`AIManager`:** still the 2-second mock returning a hardcoded MIDI note — replaced as part of M9 Real Audio-to-MIDI.

---

## Current Build State
- **Platform**: Windows 11 (primary local dev — Visual Studio 2022, MSVC x64). macOS builds via CI/release workflow.
- **Presets**: `win-msvc-debug`, `win-msvc-release`, `win-msvc-debug-tests`, `win-msvc-profiling` (see root `CMakePresets.json` + `AerionDawCpp/Documentation/CURSOR_DEVELOPMENT.md`)
- **Engine**: Tracktion Engine v3.2 / JUCE 8, with small Aerion patches from `AerionDawCpp/Patches/` applied at configure time (also to a local `FETCHCONTENT_SOURCE_DIR_TRACKTION_ENGINE` checkout)
- **Build**: build the app and tests as targets, `cmake --build build --config Debug --target AerionDaw AerionTests --parallel`; a full solution build still fails in an unrelated Tracktion example target. After the embedded resource list changes, a parallel build can produce a damaged `AerionDawResources` object (`LNK1236`); rebuild that target on its own to fix it.
- **Performance builds**: a separate Release build with `AERION_ENABLE_PROFILING=ON` (`build-profiling`) holds `AerionBench` and the profiling app; timings from Debug builds are not meaningful.
- **CI**: both workflows are manual (`workflow_dispatch`) only. `build-test.yml` runs the Debug build, `AerionSmokeTests` and `AerionBench --verify` on Windows (MSVC/Ninja) and macOS (Clang/Ninja) — run it before merging a PR. `package-release.yml` (`release-package`) builds the Windows NSIS installer and macOS DMG, with optional self-signed Windows code signing, then publishes a GitHub Release with both attached.

## Next Steps (priority order)

Mirrors Milestones 5 and 6 in [`ROADMAP.md`](./ROADMAP.md) so the two documents agree.

**Finish M5 (v0.4.0):**

1. **Performance** — audio-side hot-path review. Measure every step with `AerionBench` against the targets in [`PERFORMANCE.md`](./PERFORMANCE.md).
2. **Stability** — plugin crash protection beyond audio processing (out-of-process hosting). Error reporting (log console, crash reports) is done.
3. **Accessibility** — screen-reader labels and keyboard-navigable mixer.
4. **Packaging finish line** — self-signed Windows code signing is done *(`release-package` + `New-AerionSelfSignedCert.ps1`)*; production OV/EV code-signing (to clear SmartScreen) and macOS notarization still require a paid certificate/Apple Developer account.

**Open issues:**

- **Track faders reported not moving** (October 4, 2026; the master fader worked, tracks were not frozen). Not reproduced: `AerionBench --verify` drags a track fader and the master fader, stopped and playing, and both follow the mouse. Needs the steps that lead to it.
- **`release-package` cannot create the GitHub Release:** the release job fails with `HTTP 403: Resource not accessible by integration` although its token has `contents: write`, the same as the September 28 run that worked. The v0.3.6 installers were attached by hand. Likely fix: Settings → Actions → General → Workflow permissions → **Read and write permissions** (currently read).
- **Installer file names say 0.3.0:** CPack names them from the CMake `project` version, still 0.3.0, while release tags have moved on (v0.3.6).

**Then M6 — Complete Core (v0.5.0)**, in this order: stock instruments and effects, full parameter automation, audio warping and time-stretch, sidechain routing, MIDI learn and control surfaces, clip processing, templates, analysis metering, CLAP spike.

## Trademarks

- **ASIO** is a trademark and software of Steinberg Media Technologies GmbH. The Steinberg ASIO SDK is bundled in-tree at `AerionDawCpp/External/ASIO-SDK_*/` under GPLv3; the official ASIO-compatible mark appears small on the splash (bottom-left) and with full attribution in the About dialog.
- **JUCE** is a trademark of Raw Material Software Limited.
- **VST** is a trademark of Steinberg Media Technologies GmbH (VST3 plugin hosting only — no Steinberg-proprietary VST2 code in this repo).
