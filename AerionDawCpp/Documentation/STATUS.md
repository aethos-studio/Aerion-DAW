# Aerion DAW Project Status — May 6, 2026

## Overview
Aerion DAW has completed Milestone 1 in full and made significant progress on Milestone 2. Recent sessions focused on **startup time**, **UI responsiveness**, **readability of on-screen text**, drag-and-drop import workflow, UI polish (collapsible panels, branded plugin windows, animated splash), and project file management.

## Milestone Progress

| Milestone | Status |
|---|---|
| M1: Core Foundation & Stability (v0.3.0) | **Complete** |
| M2: Asset Management & Cloud Sync (v0.5.0) | **In Progress** (4 of 5 items done) |
| M3: AI-Enhanced Workflows (v0.8.0) | Not started |
| M4: Polish & v1.0 Release | Not started |

---

## Completed Features

### Milestone 1 (all complete)
- Reactive UI state via `ProjectData` ValueTree — mute, solo, volume propagate automatically
- Plugin editor support — third-party GUIs open and stay alive
- Audio Device Settings panel (`juce::AudioDeviceSelectorComponent`)
- Automation lanes — volume/pan curves drawn and edited in the Timeline
- Advanced transport — tempo, time sig, bars/beats/ticks readout bound to engine state

### Volume & Fader Pipeline (bug fixes)
- **Correct Tracktion formula**: native fader position `pos = exp((dB − 6) / 20)`, inverse `dB = 20·ln(pos) + 6`. Replaced all prior log10/quartic approximations.
- **Double-click reset at 0 dB**: removed erroneous `convertTo0to1` call so `setParameter` receives the actual value, not a normalized one.
- **Automation drag granularity**: switched from absolute Y→value mapping (0.9 dB/px) to delta-based drag (0.1 dB/px default, 0.01 dB/px with Shift).

### Track Management
- **Free reordering**: drag any track or folder to any position. `RowInfo` carries a `parent` pointer; `moveTrackAfter(t, preceding, folder)` uses Tracktion's `TrackInsertPoint` to handle all cases (reorder, into folder, out of folder).
- **Context menu**: "Move into folder" submenu, "Detach from folder".
- **Recording fixed**: wave input devices enabled, `ensureContextAllocated()` called, each `InputDeviceInstance` wired to armed track via `setTarget` + `setRecordingEnabled`.

### Milestone 2 Features

#### Piano Roll Editor
- `PianoRollEditor` — 128-note grid, beat ruler, piano keyboard. Add notes by click-drag, move by dragging note body, resize by dragging right edge, delete by right-click. Snap to configurable note length (quarter/8th/16th). Horizontal and vertical `ScrollBar`s.
- `PianoRollWindow` — `DocumentWindow` wrapper, 960×560, opens on double-click of an existing MIDI clip in the Timeline.

#### Waveform Preview & Drag-to-Import
- `AudioThumbnail` (80 px strip) in the Browser Files tab shows the waveform of the selected file.
- Async loading via `thumb.setSource(new FileInputSource(f))`; `ChangeListener` triggers repaint on complete.
- Click-drag on a file row initiates OS file drag via `DragAndDropContainer::performExternalDragDropOfFiles`.

#### Google Drive Cloud Tab
- Browser gains a third "Cloud" tab alongside Plugins and Files.
- Shows Connect / Disconnect button, Refresh button, and a scrollable file list.
- Clicking a file calls `GoogleDriveClient::downloadFile` — fetches to a temp path, delivers to `onFileDownloaded` on the message thread, which calls `audioEngine.importAudioFile`.
- Login state and file list changes trigger automatic repaints via `onLoginStateChanged` / `onFilesListed` callbacks.

#### Timeline drag-and-drop (audio & plugins)
- **Position-aware file drop**: Audio files dropped onto the Timeline land at the grid-snapped time on the target track (or a new track if dropped below existing tracks).
- **Ghost preview**: While hovering, a translucent clip rectangle snaps to the beat grid; row highlight shows the target track.
- **Consecutive multi-file drop**: Multiple files dropped at once are placed back-to-back without gaps, using each file's duration to advance the cursor.
- **Plugin drag — Track Headers**: Drag a plugin from the Browser's Plugins tab onto a track header. Plugin window opens immediately.
- **Plugin drag — Mixer Strips**: Same drag can be dropped onto any strip in the Mixer with hover highlight.

### UI & Branding

#### Animated Splash Screen
- Layered fog halos, logo rise, staggered title fade (Cinzel for splash title only).
- Short minimum hold plus quick fade so the app does not block on a long artificial delay.
- Subtitle "BY AETHOS STUDIO" uses embedded **Cinzel** (Google Fonts TTF in BinaryData).

#### Collapsible Side Panels
- Inspector (left) and Browser (right) panels each have a 14 px toggle strip with a directional chevron arrow.
- Clicking the strip collapses/expands the panel instantly. Arrow direction updates to reflect state.
- Center Timeline and Mixer expand to fill the reclaimed space.

#### Project Title Bar
- Window title updates to `<ProjectName> — Aerion DAW` after save or load.
- Falls back to `Aerion DAW` for unsaved new projects.

#### Plugin Window Branding
- Plugin windows use JUCE-rendered (`MetalLookAndFeel`) title bars — no white OS bar.
- Mixer detach window and plugin manager window use dark `bgPanel` background with branded text color.

### Startup & responsiveness (May 2026)

- **Splash / main window order**: The main window is constructed first; the splash is dismissed only after that completes, avoiding a blank gap during heavy initialization.
- **Deferred audio device open**: After `ProjectData`/edit setup, `AudioDeviceManager` initialisation runs on the next message-loop cycle (`MessageManager::callAsync`), so the UI can appear and pump events while the OS loads the selected audio backend (e.g. WASAPI/ASIO on Windows). `broadcastChange()` runs when hardware is ready for status refresh.
- **Tooltips**: Faster hover feedback and more reliable show logic (`AerionTooltipWindow`).
- **Toolbar / transport**: Fewer redundant repaints; toolbar repaints on hover **zone** change only; transport/CPU strip refresh throttled when idle.
- **Inspector / mixer**: Meter/timer cadence tuned to reduce unnecessary full repaints while keeping meters useful.

### Typography & readability (May 2026)

- **Body UI** uses the **system sans** (e.g. Segoe UI on Windows) at a consistent scale — **Cinzel is no longer the default for general UI** (it remains appropriate for splash branding only).
- **`Theme::uiSize()`** and **`Theme::kUiFontScale`** centralise logical point sizes so panels, timeline, transport, and browser text are slightly larger and easier to read.
- **`MetalLookAndFeel`** scales default fonts for text buttons, combo boxes, and alert dialogs.

---

## Current Build State
- **Platform**: Windows 11 (MSVC 2022 Build Tools / VS 2026)
- **Engine**: Tracktion Engine v3.2 / JUCE 8
- **Build**: Clean Debug build of the **AerionDaw** target; full solution builds may still hit unrelated demo targets (e.g. LV2 helper in third-party examples).

## Next Steps (priority order)
1. **Quantization** (M2) — grid-snap and MIDI quantize in Piano Roll
2. **Project Syncing** (M2) — background cloud sync of `.aerion` project files
3. **Optional UX** — brief “audio starting” or disabled transport until `AudioDeviceManager` finishes opening (if any edge cases appear after deferred init)
4. **Timeline / paint profiling** — narrow invalidation, reduce full repaints in heavy views if anything still feels sluggish
5. **ONNX Runtime** (M3) — link runtime, wire to `AIManager`
6. **Real Audio-to-MIDI** (M3) — replace mock with a transcription model
7. **Stem Separation** (M3) — separate-stems pipeline integration
8. **Keyboard Shortcuts** (M4) — customisable key mapping
