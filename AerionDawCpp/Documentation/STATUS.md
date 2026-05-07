# Aerion DAW Project Status — May 7, 2026 (v0.2.0 Pre-Alpha)

## Overview
Aerion DAW has just landed the **Pre-Alpha Release** (`91d704c`) with the version bump to **v0.2.0**. Editing is feature-complete through **M1** (including MIDI CC lanes in the Piano Roll). **M2 mixing** is complete for shipped scope: metering (clip + optional K-14 on master), folder/submix routing and UI, cascading mute/solo, and arranger drag gestures. Recording is mostly done (**Input Monitoring** remains). Project & Workflow has **Mixdown Export**; other M4 items remain.

## Milestone Progress

| Milestone | Status |
|---|---|
| M1: DAW Essentials — Editing (v0.0.1) | **Complete** — clip/MIDI/transport editing including **MIDI CC / Pitch lane** in the Piano Roll |
| M2: DAW Essentials — Mixing (v0.1.0 Pre-Alpha) | **Complete** — Phase/Mono, HPF/LPF, Inserts, Sends, Presets, Snapshots, **clip + K-14 on master**, **submix folders + routing + Console/Timeline visuals**, **cascading mute/solo**, **drag-to-indent/detach** |
| M3: DAW Essentials — Recording & Monitoring (v0.6.0) | **Complete** — Metronome, Count-In, Punch In/Out, PDC, Multi-channel Input Routing, Buffer Safety, **live recording waveform**, **ASIO + WASAPI + DirectSound + WinRT MIDI driver pack**, **Reset Audio Settings** safety net, **per-track Input Monitoring override (Auto / On / Off)** and a **per-track MIDI controller selector** all shipped. Per-track input + monitor settings are session-scoped today; persisting them through `IDs::trackInputDeviceIdx` / `IDs::midiInputDevice` / `IDs::monitorMode` is the only follow-up. |
| M4: DAW Essentials — Project & Workflow (v0.7.0) | **In progress** — **Mixdown Export shipped**. Stems export, Bounce/Freeze, Tempo Map, Recent Projects, Auto-save / Crash Recovery, Customisable Keyboard Shortcuts and Collect-and-Save still outstanding. |
| M5: DAW Essentials — Polish & Stability (v0.9.0) | Not started (some performance/typography work has begun in flight) |
| Future: USPs & Differentiators (v1.0+) | Not started — `AIManager` is still a 2-second mock; ONNX runtime not linked |

---

## Recently shipped (since last STATUS update)

These are the items that flipped from "in progress" or "not started" to "done" and now drive the updated milestone table.

- **MIDI CC / Pitch lane (M1):** `PianoRollEditor` — resizable splitter, CC type combo + custom CC dialog, `drawCCLane` / `setControllerValueAt` / `removeControllerEvent`, `IDs::pianoRollCC` on clip state.
- **Submix folders & routing (M2):** `setFolderSubmix` / `isFolderSubmix` / `syncFolderRouting()`; `createNewPlugin` Volume+Pan + LevelMeter vs VCA-only organisational folders; convert items in Timeline + Mixer context menus.
- **Mixer / Arranger submix visuals (M2):** wider folder strip + outline; Arranger **S** badge and 3 px colour bar; drag-left detach + folder drop first/last child polish.
- **Master K-14 overlay (M2):** `paintFader` optional reference ticks; toggle on master strip context menu; `IDs::masterKMeter` on project tree.
- **MIDI Quantization (M1):** `Q` shortcut in `PianoRollEditor` snaps each note's start and length to the active snap interval (`PianoRollEditor::quantize()`).
- **MIDI Velocity Editor (M1):** velocity lane below the grid; drag bars to write `MidiNote::setVelocity()` (`drawVelocityLane()` + `updateVelocityAt()`).
- **Phase Invert / Mono Sum (M2):** `setTrackPhase` / `setTrackMono` driven from Inspector pills and the Mixer-strip context menu.
- **Channel Strip Filters HPF/LPF (M2):** collapsible "QUICK FILTERS" section in the Inspector (`getTrackHPF/LPF`, `setTrackHPF/LPF`) with double-click-to-reset.
- **Inserts in Inspector (M2):** INSERTS list mirrors the live track plugin chain; click-to-edit, right-click to remove, drag a plugin from the Browser to add.
- **Quick Send + Inspector SENDS (M2):** `addSendToNewBus` from the track-header / Mixer-strip context menus; per-send level via `setAuxSendLevelDb` / `getAuxSendLevelDb`.
- **Plugin Preset Browser (M2):** `getPluginNumPrograms`, `getPluginProgramName`, `setPluginProgram` exposed; programs accessible via the plugin window pop-out and the plugin context menu.
- **Mix Snapshots (M2 bonus):** `saveMixSnapshot` / `recallMixSnapshot` / `getMixSnapshotNames` with a "Snapshots" submenu on the Mixer-strip context menu.
- **Metronome (M3):** toolbar toggle plus volume (`get/setMetronomeVolumeDb`) and accent-on-downbeat (`setMetronomeAccentEnabled`).
- **Count-In / Pre-roll (M3):** Off / 1 Bar / 2 Bars via `setCountInMode` and a toolbar count-in button.
- **Punch In / Out (M3):** `setPunchEnabled` uses the loop range as the punch region; toolbar punch button wired.
- **Plugin Delay Compensation (M3):** `setLatencyCompensationEnabled` / `isLatencyCompensationEnabled` with toolbar PDC button.
- **Multi-channel Input Routing (M3):** `getInputDeviceNames` + `setTrackInputDevice` exposed via the Inspector input dropdown per track.
- **Record Buffer / CPU readout (M3):** `BufferInfo { sampleRate, blockSize, cpuUsage }` surfaced in the transport status strip.
- **Mixdown Export (M4):** `MixdownExportDialog` + `MixdownExportJob` render the master to WAV / AIFF / FLAC / OGG with bounds selection (Selection / Loop / Full), tail length, format presets, filename wildcards and a true pre-rendered waveform preview with clip detection.
- **Live recording waveform (M3):** `Timeline::drawTrackRow` taps `RecordingThumbnailManager::Thumbnail` so the in-flight take grows under the playhead instead of materialising only after the take ends.
- **Latency sanity (M3):** `clampExcessiveAudioBufferSize` nudges absurdly large buffers down on first run; `BufferInfo` now reports `oneBlockMs` + `driverIoMs` and the transport strip surfaces both so latency mismatches are visible at a glance.
- **Driver pack (M3):** Build now enables the full driver line-up — **ASIO** (when the Steinberg SDK is bundled), **WASAPI**, **DirectSound**, **CoreAudio**, **ALSA**, **JACK**, **WinRT MIDI** — so users on any common interface get their preferred backend without a custom build.
- **Reset Audio Settings (M3):** Audio Settings dialog gains a button that wipes the saved `audioDeviceState` from `ApplicationProperties`, re-runs JUCE's `initialiseWithDefaultDevices`, then re-applies safe defaults — recovers from misconfigurations without uninstalling.
- **Input Monitoring override (M3):** new `AudioEngineManager::MonitorMode { Auto, On, Off }` per track, applied through `tracktion::InputDevice::setMonitorMode` on each arm. Inspector grew a "MON" pill next to Phase / Mono that cycles the three modes and re-arms the track in place; **AUT** = smart automatic (default), **ON** = always-on input echo, **OFF** = silent (rely on hardware monitoring).
- **MIDI-Input Selector (M3):** new `getMidiInputDeviceNames()` enumerates Tracktion's `DeviceManager::getMidiInDevice(i)` list; `setTrackMidiInputDevice` / `getTrackMidiInputDeviceIdx` pin a specific controller to a track. The existing Inspector "In" dropdown now opens into "Audio Input" + "MIDI Controller" submenus, and the readout shows `<wave dev> | MIDI: <controller>` whenever a MIDI device is pinned. System-level MIDI enable/disable continues to live in the Audio Settings dialog (`AudioDeviceSelectorComponent` already exposes the MIDI inputs section).

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
- **Free reordering**: drag any track or folder to any position. `RowInfo` carries a `parent` pointer; `moveTrackAfter(t, preceding, folder)` uses Tracktion's `TrackInsertPoint` to handle all cases (reorder, into folder, out of folder). **Drag-left** past the header indent (−16 px) while dragging a nested track detaches to top level; folder-row drop uses upper portion of the “into folder” band for **first child** vs **last child**.
- **Context menu**: "Move into folder" submenu, "Detach from folder", **Convert to Submix / Convert to Folder** on folder tracks (Timeline + Mixer).
- **Recording fixed**: wave input devices enabled, `ensureContextAllocated()` called, each `InputDeviceInstance` wired to armed track via `setTarget` + `setRecordingEnabled`.

### Milestone 2 Features

#### Piano Roll Editor
- `PianoRollEditor` — 128-note grid, beat ruler, piano keyboard. Add notes by click-drag, move by dragging note body, resize by dragging right edge, delete by right-click. Snap to configurable note length (quarter/8th/16th). Horizontal and vertical `ScrollBar`s.
- **CC / Pitch lane** — combo for Pitch / CC1 / 7 / 10 / 11 / 64 / Other; resizable splitter above velocity; paint/edit via `MidiList`; `IDs::pianoRollCC` on clip state.
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

Mirrors the "Next up" list in [`ROADMAP.md`](./ROADMAP.md) so the two documents agree.

1. **Stems export (M4)** — extend `MixdownExportDialog::sourceBox` and `MixdownExportJob` so users can render each track or bus individually using the existing dialog.
2. **Bounce / Freeze, Recent Projects, Auto-save & Crash Recovery (M4)** — round out the session-management story so a Pre-Alpha user can safely work on a real project end-to-end.
3. **Per-track input + monitor persistence** — round-trip `getTrackInputDeviceIdx` / `getTrackMidiInputDeviceIdx` / `getTrackMonitorMode` through the project ValueTree (the existing-but-unused `IDs::trackInputDeviceIdx` plus two new IDs) so the per-track routing survives save / load.
4. **Customisable Keyboard Shortcuts (M4)** — replace hardcoded `keyPressed` paths with a user-editable mapping.
5. **ONNX Runtime + Real Audio-to-MIDI + Stem Separation (Future / USP)** — link ONNX runtime and replace the `AIManager` mock once the foundation milestones are wrapped.

## M1/M2 wrap — manual smoke (May 7, 2026)

Build: **AerionDaw** Debug target succeeds (MSVC). Spot-checks: Piano Roll CC lane paint/edit compiles against `MidiList` APIs; `setFolderSubmix` uses `createNewPlugin` + `insertPlugin` (Tracktion v3.2); cascading mute/solo and meter clip behaviour unchanged from prior implementation; K-14 toggle persists on project tree. Full interactive QA left to the user on hardware.

## Trademarks

- **ASIO** is a trademark and software of Steinberg Media Technologies GmbH. The Steinberg ASIO SDK is bundled in-tree at `AerionDawCpp/External/ASIO-SDK_*/` under GPLv3; the official ASIO-compatible mark appears small on the splash (bottom-left) and with full attribution in the About dialog.
- **JUCE** is a trademark of Raw Material Software Limited.
- **VST** is a trademark of Steinberg Media Technologies GmbH (VST3 plugin hosting only — no Steinberg-proprietary VST2 code in this repo).
