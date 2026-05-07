# Aerion DAW Roadmap

This document outlines the development path for Aerion DAW. The strategy is simple: **ship a rock-solid, feature-complete DAW first — then layer in the USPs that make Aerion unique.**

---

## Current State (v0.1.0 Pre-Alpha — May 2026)

All items below are fully implemented and working in the current build (unless marked as partial).

### Audio Engine & Transport
- Tracktion Engine v3.2 / JUCE 8 integration
- Audio and Folder track support; basic transport (play, stop, record)
- Tempo, time signature, and bars/beats/ticks readout bound to engine state
- Record-arm per track; wave input device routing wired correctly
- Undo / Redo via Tracktion Engine `UndoManager`
- **Startup:** Session/edit initialisation runs first; opening the audio device is deferred to the next message-loop tick so the main window can appear while drivers initialise.

### Mixer
- Real-time level meters (`LevelMeterPlugin` / `LevelMeasurer`)
- Volume (dB, correct Tracktion native fader formula) and Pan per track
- Mute and Solo functional and reactive
- Detachable Mixer window
- Automation lanes — volume/pan curves drawn and edited in the Timeline

### Tracks
- Audio, Folder, and Master tracks
- Free drag reordering — move any track or folder to any position
- "Move into folder" / "Detach from folder" via context menu; drag-into-folder and drag-left-to-detach gestures on headers
- **Submix folders (opt-in):** `Convert to Submix` / `Convert to Folder` on folder headers (Timeline + Mixer); submix folders show an **S** badge and narrower colour bar in the Arranger
- Track grouping (`groupTracks`)

### Plugin Hosting
- VST3 (Windows/Linux) and AU (macOS) scanning and hosting
- Plugin windows with branded JUCE-rendered (`MetalLookAndFeel`) title bars
- Drag plugin from Browser → Timeline track header or Mixer strip

### Browser & File Management
- Local file-system navigator with `AudioThumbnail` waveform preview (80 px strip)
- Plugin browser tab with category listing
- Position-aware audio file drop onto Timeline (ghost preview + grid snap)
- Consecutive multi-file drop — files placed back-to-back using actual duration
- Click-drag on Browser file row → OS-level drag-and-drop import

### Piano Roll
- 128-note grid, beat ruler, piano keyboard
- Add (click-drag), move (drag body), resize (drag right edge), delete (right-click)
- Snap to configurable note length (quarter / 8th / 16th)
- **MIDI CC / Pitch lane:** switchable lane (Mod, Volume, Pan, Expression, Sustain, Pitch Bend, custom CC) with resizable splitter above the velocity lane; draw/edit via `MidiList::setControllerValueAt` / `removeControllerEvent`; selection persisted per clip (`IDs::pianoRollCC`)
- Horizontal and vertical scrollbars
- Opens on double-click of a MIDI clip in the Timeline

### AI (Scaffolding)
- `AIManager` thread scaffold — audio-to-MIDI transcription stub
- Mock transcription (2 s delay → hardcoded MIDI note) wired to `processTranscription()`

### UI & Branding
- Celtic Metal dark theme (`MetalLookAndFeel`)
- Animated fog splash screen — short minimum hold; Cinzel for splash title/subtitle only; body UI uses **system sans** at scaled sizes (`Theme::uiSize` / `kUiFontScale`)
- Collapsible Inspector (left) and Browser (right) panels with directional chevron toggles
- Project title bar updates to `<ProjectName> — Aerion DAW` after save/load
- Reactive UI state — all components bound to `ProjectData` ValueTree
- Recent passes: tooltip timing, toolbar hover invalidation, throttled idle transport readout, tuned inspector/meter refresh

### Architecture
- MVC pattern: `AudioEngineManager` (model), `ProjectData` (ValueTree), UI components (view)
- `ProjectData::syncWithEngine()` keeps ValueTree in sync with live engine state
- `AudioDeviceSelectorComponent` settings panel (ASIO / CoreAudio / ALSA)

---

## Milestone 1 — DAW Essentials: Editing (v0.0.1)
*Everything a producer needs to actually edit a song.*

- [x] **Clip Editing — Trim & Split:** Click-drag clip edges to trim; razor/split tool to cut clips at the playhead or a click point.
- [x] **Clip Move & Nudge:** Drag clips freely on the timeline; nudge by one frame/beat with arrow keys.
- [x] **Clip Gain & Fade Handles:** Per-clip input gain knob; drag-in fade-in and fade-out handles directly on the clip.
- [x] **Comp Tool / Takes:** Record multiple takes on a track; display take lanes and allow comping by selecting segments from different takes.
- [x] **Loop / Cycle Range:** Set a loop region on the ruler; transport loops automatically within it.
- [x] **Markers:** Add, name, and navigate between named markers on the ruler.
- [x] **Snap Settings UI:** Snap mode selector (Bar / Beat / Sub-beat / Off) accessible from the toolbar.
- [x] **MIDI Quantization:** Quantize selected MIDI notes to grid in the Piano Roll (Q shortcut). `PianoRollEditor::quantize()` snaps each note's start and length to the active snap interval.
- [x] **MIDI Velocity Editor:** Velocity lane below the Piano Roll grid; drag bars to adjust per-note velocity (`drawVelocityLane()` + `updateVelocityAt()` writing `MidiNote::setVelocity()`).
- [x] **MIDI CC Lanes:** Stacked CC/Pitch-Bend lane above velocity with 4 px splitter; combo + `Other…` for any CC 0–127; stepped polyline + midpoint reference; click/drag paint with snap; right-click removes; Undo via `Edit::getUndoManager()`; `IDs::pianoRollCC` on clip state.

---

## Milestone 2 — DAW Essentials: Mixing (v0.1.0 pre-Alpha)
Implementing professional mixing workflows while preserving a clean, beginner-friendly UI through progressive disclosure.
Keep the Console clean. Put the advanced technical tools in the Inspector.

- [x] Phase Invert & Mono/Stereo Summing: `setTrackPhase` / `setTrackMono` in `AudioEngineManager` drive the DSP; the Inspector exposes them as compact toggle pills (only when a track is selected) and the Mixer-strip context menu offers them as items.
- [x] Channel Strip Filters (HPF/LPF): pre-fader HPF/LPF (`getTrackHPF/LPF`, `setTrackHPF/LPF`) shown as a collapsible "QUICK FILTERS" section in the Inspector above INSERTS. Double-click to reset to bypass (20 Hz / 20 kHz).
- [x] **Gain Staging & Metering:** Clip flash on meters (`paintFader`); optional **K-14 reference scale** on the Master strip (context menu, persisted as `IDs::masterKMeter`) with a brighter tick at −14 dBFS and softer ticks at −20 / −17 / −11 / −8 dBFS.
- [x] **Folder Track Nesting (Arranger):** + Folder flows through `syncFolderRouting()`; chevron on folder headers; drag reorder; drag into folder (middle band, upper portion = first child); drag header **left** past indent (−16 px) to detach to top level; highlight previews.
- [x] **Folder Channel Strip (Console):** Folder strips in the Mixer; **submix** folders get +8 px width and a coloured outline from the folder colour; `Convert to Submix` / `Convert to Folder` in Mixer + Timeline menus (`setFolderSubmix`).
- [x] **Hierarchical Signal Routing:** `AudioEngineManager::setFolderSubmix` / `isFolderSubmix` / `syncFolderRouting()` — opt-in submix folders (Volume+Pan + LevelMeter vs VCA-only organisational folders); called after add/group/load and menu toggles.
- [x] **Cascading Mute / Solo:** Folder mute/solo propagates to children (engine + Tracktion folder semantics; verified in smoke test).
- [x] Context Menu "Quick Send": `AudioEngineManager::addSendToNewBus()` creates a new bus and inserts an `AuxSendPlugin` on the source track; surfaced from track-header / Mixer-strip context menus.
- [x] Inspector SENDS Wiring: Inspector "SENDS" section enumerates `AuxSendPlugin` instances on the selected track and exposes per-send level via `setAuxSendLevelDb` / `getAuxSendLevelDb` so users don't have to scan the Console.
- [x] Insert Slot Sync: INSERTS list in the Inspector is driven by the live track plugin list, so any plugin added via the Console (drag-drop or context menu) appears immediately in the Inspector and vice versa.
- [x] Insert Logic: Serial DSP processing follows the Tracktion plugin chain ordering; the Inspector supports click-to-edit (opens plugin editor) and right-click to remove. Bypass and drag-to-reorder remain on the polish list.
- [x] Plugin Preset Browser: Engine API `getPluginNumPrograms` / `getPluginProgramName` / `setPluginProgram` exposed; programs are accessible via the plugin window pop-out and the plugin context menu.
- [x] **Mix Snapshots (bonus, not on original roadmap):** `saveMixSnapshot` / `recallMixSnapshot` / `getMixSnapshotNames` capture and restore mixer state; surfaced as a "Snapshots" submenu on the Mixer-strip context menu.

## Milestone 3 — DAW Essentials: Recording & Monitoring (v0.2.0)
*A reliable, low-latency recording experience.*

- [x] **Input Monitoring:** `setTrackMonitorMode` / `getTrackMonitorMode` (Auto / On / Off) per track; the Inspector "MON" pill (next to Phase / Mono) cycles through the three modes and applies them on the next arm. Default Auto = monitor while armed, suspend during clip playback (Tracktion's smart-monitoring).
- [x] **MIDI-Input Selector:** `getMidiInputDeviceNames()` enumerates Tracktion's `DeviceManager` MIDI inputs; the existing Inspector input dropdown now splits into "Audio Input" + "MIDI Controller" submenus so each track can be pinned to a specific physical/virtual MIDI device (or "All MIDI controllers"). System-level enable/disable still happens in the Audio Settings dialog (`AudioDeviceSelectorComponent`'s built-in MIDI section).
- [x] **Latency Compensation:** Plugin delay compensation toggle (`setLatencyCompensationEnabled` / `isLatencyCompensationEnabled`) wired to a toolbar button.
- [x] **Count-in / Pre-roll:** Configurable bar count-in (Off / 1 Bar / 2 Bars) via `setCountInMode` and the toolbar count-in button; the metronome click runs during count-in.
- [x] **Metronome:** Dedicated metronome toggle on the Transport with volume control (`getMetronomeVolumeDb` / `setMetronomeVolumeDb`) and accent-on-downbeat option (`setMetronomeAccentEnabled`).
- [x] **Punch In / Out:** `setPunchEnabled` uses the loop range as the punch region; record only overwrites the punched region. Toolbar punch button wired.
- [x] **Multi-channel Input Routing:** `getInputDeviceNames` / `setTrackInputDevice` expose all wave input devices; Inspector input dropdown lets the user pick the source per track.
- [x] **Record Buffer Safety:** `BufferInfo { sampleRate, blockSize, cpuUsage, oneBlockMs, driverIoMs }` is exposed; the transport status strip shows block size + one-buffer ms + driver round-trip so high-latency configs are visible at a glance.
- [x] **Live recording waveform:** `Timeline::drawTrackRow` renders `RecordingThumbnailManager::Thumbnail` data while the take is in flight — the waveform now grows under the playhead instead of materialising only on stop.
- [x] **Driver pack:** Build enables **ASIO** (Steinberg SDK bundled in-tree under GPLv3; small ASIO-compatible mark on splash, full notice in About), **WASAPI**, **DirectSound**, **CoreAudio**, **ALSA**, **JACK** and **WinRT MIDI** out of the box.
- [x] **Reset Audio Settings safety net:** Audio Settings dialog has a button that wipes the saved `audioDeviceState` from `ApplicationProperties`, re-runs JUCE's `initialiseWithDefaultDevices`, and re-applies the safe defaults — no reinstall required.

---

## Milestone 4 — DAW Essentials: Project & Workflow (v0.3.0)
*Professional session management.*

- [ ] **Full Project Save / Load:** Complete round-trip serialisation of all tracks, clips, plugin state, automation, and mixer settings to a single `.aerion` file (XML + referenced audio).
- [ ] **Audio File Management — Collect & Save:** Copy all referenced audio into a project folder; detect and warn about missing files on open.
- [ ] **Bounce / Freeze:** Render a track (with plugins) to a new audio clip in place; frozen track shows waveform and locks plugins to save CPU.
- [x] **Export — Mixdown:** `MixdownExportDialog` + `MixdownExportJob` render the master to WAV / AIFF / FLAC / OGG with configurable sample rate and channels, a true pre-rendered waveform preview (with clip detection), bounds selection (Selection / Loop / Full), tail length, format presets, and filename wildcards.
- [ ] **Export — Stems:** Export each track (or bus) individually as a rendered audio file. *Dialog scaffolding is shared with Mixdown, but the source dropdown today only offers "Master mix" — extending `sourceBox` and the render job to per-track / per-bus output is the remaining work.*
- [ ] **Tempo Map:** Draw a tempo curve / step tempo changes on the Timeline ruler; clips follow the map.
- [ ] **Time Signature Changes:** Insert per-bar time signature changes from the Transport.
- [ ] **Keyboard Shortcut System:** Fully customisable key bindings panel; ship sensible defaults aligned with common professional DAW conventions.
- [ ] **Recent Projects List:** Menu → Open Recent with up to 10 entries.
- [ ] **Crash Recovery:** Auto-save every N minutes to a recovery folder; prompt to restore on next launch.
- [ ] **Hotkey implementation:** Assign sensible Hotkeys to all functions and create a subentry in the help section laying them all out.

---

## Milestone 5 — DAW Essentials: Polish & Stability (v0.4.0)
*Ship-ready quality.*

- [ ] **Performance Optimization:** Multi-threaded audio graph; minimize UI thread blocking; profile and eliminate hot-path allocations. *In progress: splash/deferred device init, repaint scoping, tooltip/toolbar cadence — continue with timeline/piano-roll paint profiling.*
- [ ] **High-DPI / Retina Support:** All custom-drawn components scale correctly at 150 % / 200 % display scaling. *Typography tokens (`Theme::uiSize`) are a base layer; audit fixed pixel layouts next.*
- [ ] **Workspace Layouts:** Save and switch between named window layouts (Editing, Mixing, Recording).
- [ ] **Accessibility:** Screen-reader labels on all interactive controls; keyboard-navigable mixer.
- [ ] **Error Reporting:** Structured in-app crash reporter; DBG logs surfaced to a `Console` panel in dev builds.
- [ ] **Unit & Integration Tests:** `ProjectData` round-trip tests; `AudioEngine` smoke tests; CI pipeline (GitHub Actions).
- [ ] **macOS Packaging:** Code-signed DMG, notarised for Gatekeeper.
- [ ] **Windows Packaging:** NSIS installer (already scaffolded in CMake); code-signed binary.

---

## Future — USPs & Differentiators (v1.0+)

These are what make Aerion *Aerion*. They are deliberately deferred until the DAW foundation is solid.

### Linux Support
Adding support for Linux Systems (Flatpak)

### Update Mechanism
In software update module that will check the github repo for new releases and offers to update automatically

### Cloud Sync
- Automatic background sync of `.aerion` project files and referenced audio to Google Drive
- Version history — browse and restore previous project snapshots from Drive
- Real-time collaborative editing — multi-user project sessions via cloud

### AI-Enhanced Workflows
- **ONNX Runtime Integration:** Link runtime, replace stub in `AIManager`
- **Real Audio-to-MIDI:** High-accuracy transcription (Basic Pitch or equivalent) wired to the Inspector
- **Stem Separation:** "Separate Stems" in the Inspector using Demucs / Spleeter models
- **Model Management UI:** Download, update, and swap AI models; keeps initial binary lean
- **AI-Assisted Mixing:** Gain staging suggestions, EQ matching, loudness normalisation

### Expanded Platform
- **Mobile Companion App:** Remote transport and mixer control from tablet / phone
- **Video Support:** Video playback track with frame-accurate sync for film scoring
- **Generative MIDI Tools:** AI-driven melodic and rhythmic generation inside the Piano Roll
- **AI-Driven Synthesis:** Prompt-to-patch synthesis for built-in virtual instruments

---

## Next up (post Pre-Alpha — May 2026)

Picked from what's actually still missing now that M1/M2 scope above (including MIDI CC lanes, submix folders, K-14 on master, and arranger drag gestures) has shipped alongside quantize, velocity, phase/mono, HPF/LPF, inserts/sends/snapshots, metronome, count-in, punch, PDC, multi-channel input, buffer status, and mixdown export.

1. **Stems export (M4):** Extend `MixdownExportDialog::sourceBox` and `MixdownExportJob` so users can render each track or bus individually using the existing dialog.
2. **Bounce / Freeze, Recent Projects, Auto-save & Crash Recovery (M4):** Complete the session-management story so a Pre-Alpha user can safely work on a real project end-to-end.
3. **Per-track input + monitor persistence:** Inspector input device, MIDI controller pin and monitor mode currently live in in-memory hash maps - round-trip them through `IDs::trackInputDeviceIdx` / `IDs::midiInputDevice` / `IDs::monitorMode` on the track ValueTree so they survive save / load.

---

## Trademarks

- **ASIO** is a trademark and software of Steinberg Media Technologies GmbH. The Steinberg ASIO SDK is bundled in-tree at `AerionDawCpp/External/ASIO-SDK_*/` under GPLv3; the official ASIO-compatible mark appears small on the splash (bottom-left) and with full attribution in the About dialog.
- **JUCE** is a trademark of Raw Material Software Limited.
- **VST** is a trademark of Steinberg Media Technologies GmbH (Aerion uses VST3 plugin hosting only).
