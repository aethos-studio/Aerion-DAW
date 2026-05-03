# Aerion DAW Project Status - May 3, 2026

## Overview
Aerion DAW has completed Milestone 1 in full and made significant progress on Milestone 2. Recent sessions focused on correctness fixes to the volume/automation pipeline, track reordering, recording, and new features: Piano Roll editor, waveform browser preview, drag-to-import, and the Google Drive cloud tab.

## Milestone Progress

| Milestone | Status |
|---|---|
| M1: Core Foundation & Stability (v0.3.0) | **Complete** |
| M2: Asset Management & Cloud Sync (v0.5.0) | **In Progress** (3 of 5 items done) |
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

### UI & Branding
- **Splash screen**: portrait window (400×550) using `logo_vertical.svg`. Animated circuit-growth lines sized relative to window. 3-second minimum display, 0.5-second fade-out.
- **Mixer height**: 320 px (was 260).

---

## Current Build State
- **Platform**: Windows 11 (MSVC 2022 Build Tools)
- **Engine**: Tracktion Engine v3.2 / JUCE 8
- **Build**: Clean Debug build, no errors or warnings in project sources.

## Next Steps (priority order)
1. **Project Syncing** (M2) — background Drive sync of `.aerion` project files
2. **Quantization** (M2) — grid-snap and MIDI quantize in Piano Roll
3. **ONNX Runtime** (M3) — link runtime, wire to `AIManager`
4. **Real Audio-to-MIDI** (M3) — replace mock with Basic Pitch model
5. **Stem Separation** (M3) — Demucs/Spleeter integration
6. **Keyboard Shortcuts** (M4) — customisable key mapping
