# Aerion DAW Roadmap

This document outlines the development path for Aerion DAW. The strategy is simple: **ship a rock-solid, feature-complete DAW first — then layer in the USPs that make Aerion unique.**

---

## Current State (v0.1.0 — May 2026)

All items below are fully implemented and working in the current build.

### Audio Engine & Transport
- Tracktion Engine v3.2 / JUCE 8 integration
- Audio and Folder track support; basic transport (play, stop, record)
- Tempo, time signature, and bars/beats/ticks readout bound to engine state
- Record-arm per track; wave input device routing wired correctly
- Undo / Redo via Tracktion Engine `UndoManager`

### Mixer
- Real-time level meters (`LevelMeterPlugin` / `LevelMeasurer`)
- Volume (dB, correct Tracktion native fader formula) and Pan per track
- Mute and Solo functional and reactive
- Detachable Mixer window
- Automation lanes — volume/pan curves drawn and edited in the Timeline

### Tracks
- Audio, Folder, and Master tracks
- Free drag reordering — move any track or folder to any position
- "Move into folder" / "Detach from folder" via context menu
- Track grouping (`groupTracks`)

### Plugin Hosting
- VST3 (Windows/Linux) and AU (macOS) scanning and hosting
- Plugin windows with branded JUCE-rendered (MetalLookAndFeel) title bars
- Drag plugin from Browser → Timeline track header or Mixer strip

### Browser & File Management
- Local file-system navigator with `AudioThumbnail` waveform preview (80 px strip)
- Plugin browser tab with category listing
- Studio One-style position-aware audio file drop onto Timeline (ghost preview + grid snap)
- Consecutive multi-file drop — files placed back-to-back using actual duration
- Click-drag on Browser file row → OS-level drag-and-drop import

### Piano Roll
- 128-note grid, beat ruler, piano keyboard
- Add (click-drag), move (drag body), resize (drag right edge), delete (right-click)
- Snap to configurable note length (quarter / 8th / 16th)
- Horizontal and vertical scrollbars
- Opens on double-click of a MIDI clip in the Timeline

### Cloud (Google Drive)
- OAuth 2.0 + PKCE flow with local redirect listener
- Persistent token storage (refresh token survives restarts)
- Browser "Cloud" tab — Connect / Disconnect, Refresh, scrollable file list
- Background file download → auto-import into project
- Project upload to Drive (`saveProject`)

### AI (Scaffolding)
- `AIManager` thread scaffold — audio-to-MIDI transcription stub
- Mock transcription (2 s delay → hardcoded MIDI note) wired to `processTranscription()`

### UI & Branding
- Celtic Metal dark theme (`MetalLookAndFeel`)
- Animated fog splash screen — logo rises from darkness, Cinzel typeface, 4 s minimum hold
- Collapsible Inspector (left) and Browser (right) panels with directional chevron toggles
- Project title bar updates to `<ProjectName> — Aerion DAW` after save/load
- Reactive UI state — all components bound to `ProjectData` ValueTree

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
- [ ] **MIDI Quantization:** Quantize selected MIDI notes to grid in the Piano Roll (Q shortcut).
- [ ] **MIDI Velocity Editor:** Velocity lane below the Piano Roll grid; drag bars to adjust per-note velocity.
- [ ] **MIDI CC Lanes:** Display and edit Mod Wheel, Pitch Bend, and other CC data in the Piano Roll.

---

## Milestone 2 — DAW Essentials: Mixing (v0.1.0 pre-Alpha)
Implementing professional mixing workflows while preserving a clean, beginner-friendly UI through progressive disclosure.
Keep the Console clean. Put the advanced technical tools in the Inspector.

- [ ] Phase Invert & Mono/Stereo Summing: Implement the DSP logic. UI Adaptation: Do not put these on the Console strips. Add these as small, elegant toggle buttons inside the left-hand Inspector panel that appear only when a track is selected.
- [ ] Channel Strip Filters (HPF/LPF): Implement pre-fader filters. UI Adaptation: Instead of cluttering the Mixer with EQ knobs, add a small, collapsible "Quick Filters" section in the Inspector above the INSERTS list.
- [ ] Gain Staging & Metering: Implement clip detection. Add a subtle red clipping indicator to the very top of the existing level meters in the Console. Add K-metering logic specifically to the existing Master Bus display.
- [ ] Folder Track Nesting (Arranger): Wire up that existing + Folder button. Implement the drag-and-drop indenting in the Arranger timeline and add an expand/collapse chevron to the Folder track header.
- [ ] Folder Channel Strip (Console): Ensure the Console generates a strip for Folder tracks. UI Adaptation: Give Folder strips a distinct visual cue in the Console (e.g., a slightly wider strip, or a colored outline matching the folder color) to distinguish them from standard audio tracks.
- [ ] Hierarchical Signal Routing: Update the engine so nested tracks route audio to the parent Folder Track instead of Main Out.
- [ ] Cascading Mute / Solo: Link state management so clicking M or S on a folder applies logically to child tracks.
- [ ] Context Menu "Quick Send": Implement the right-click "Add Send to New Bus..." on track headers.
- [ ] Inspector SENDS Wiring: Wire up the SENDS section in the Inspector. When a user creates a Quick Send, populate a slot in the Inspector with a Send Level slider so they don't have to look at sends on the main Console.
- [ ] Insert Slot Sync: Wire up the + FX slots in the Console to strictly mirror the INSERTS list in the Inspector. If a user adds a plugin via the Console, it must instantly appear in the Inspector (and vice versa).
- [ ] Insert Logic: Implement the serial DSP processing for these slots, plus drag-to-reorder and click-to-bypass functionality.
- [ ] Plugin Preset Browser: Implement the save/load schema for plugin states, accessible via the plugin's pop-out window.

## Milestone 3 — DAW Essentials: Recording & Monitoring (v0.6.0)
*A reliable, low-latency recording experience.*

- [ ] **Input Monitoring:** Toggle per-armed track; route input signal through the FX chain to the output while recording.
- [ ] **Latency Compensation:** Automatic plugin delay compensation (PDC) reported and applied correctly across all tracks.
- [ ] **Count-in / Pre-roll:** Configurable bar count-in before record starts; metronome click during count-in.
- [ ] **Metronome:** Dedicated metronome toggle on the Transport with volume control and accent-on-downbeat option.
- [ ] **Punch In / Out:** Set punch points on the ruler; record only overwrites the punched region.
- [ ] **Multi-channel Input Routing:** Assign any available input channel (mono or stereo pair) to any track via a dropdown in the Inspector.
- [ ] **Record Buffer Safety:** Guard against missed samples on buffer underrun; expose buffer size / sample rate in the status bar.

---

## Milestone 4 — DAW Essentials: Project & Workflow (v0.7.0)
*Professional session management.*

- [ ] **Full Project Save / Load:** Complete round-trip serialisation of all tracks, clips, plugin state, automation, and mixer settings to a single `.aerion` file (XML + referenced audio).
- [ ] **Audio File Management — Collect & Save:** Copy all referenced audio into a project folder; detect and warn about missing files on open.
- [ ] **Bounce / Freeze:** Render a track (with plugins) to a new audio clip in place; frozen track shows waveform and locks plugins to save CPU.
- [ ] **Export — Mixdown:** Export the master output to WAV/AIFF/MP3/FLAC with configurable bit depth and sample rate.
- [ ] **Export — Stems:** Export each track (or bus) individually as a rendered audio file.
- [ ] **Tempo Map:** Draw a tempo curve / step tempo changes on the Timeline ruler; clips follow the map.
- [ ] **Time Signature Changes:** Insert per-bar time signature changes from the Transport.
- [ ] **Keyboard Shortcut System:** Fully customisable key bindings panel; ship sensible defaults (matching Pro Tools / Logic conventions where possible).
- [ ] **Recent Projects List:** Menu → Open Recent with up to 10 entries.
- [ ] **Crash Recovery:** Auto-save every N minutes to a recovery folder; prompt to restore on next launch.

---

## Milestone 5 — DAW Essentials: Polish & Stability (v0.9.0)
*Ship-ready quality.*

- [ ] **Performance Optimization:** Multi-threaded audio graph; minimize UI thread blocking; profile and eliminate hot-path allocations.
- [ ] **High-DPI / Retina Support:** All custom-drawn components scale correctly at 150 % / 200 % display scaling.
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
