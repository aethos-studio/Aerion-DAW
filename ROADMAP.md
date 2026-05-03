# Aerion DAW Roadmap

This document outlines the development path for Aerion DAW, from its current state to a full v1.0 release.

## Current State (v0.1.0)
- **Audio Engine:** Core Tracktion Engine integration. Support for audio/folder tracks, basic transport, and mixer logic.
- **UI:** High-fidelity **Celtic Metal** interface. Functional Timeline, Mixer (with real-time meters), and Transport components.
- **Branding:** Established **Aethos Studio** identity with synchronized "Monolith A" assets.
- **Plugins:** Functional VST3/AU scanning and hosting. Plugins can be added to any track.
- **Cloud:** Functional Google Drive OAuth 2.0 + PKCE implementation with file listing/upload scaffolding.
- **AI:** Scaffolding for audio-to-MIDI transcription and stem separation.
- **Browser:** Functional local file system and plugin navigator.
- **Architecture:** Model-View-Controller pattern established. Basic Undo/Redo functional via Tracktion Engine.

---

## Milestone 1: Core Foundation & Stability (v0.3.0)
*Focus: Turning the scaffold into a reliable, reactive DAW.*

- [x] **Reactive UI State:** Fully bind all UI components to the `ProjectData` ValueTree. Ensure state changes (mute, solo, volume) propagate automatically without manual repaints.
- [x] **Plugin Editor Support:** Add the ability to open and manage third-party plugin GUI windows.
- [x] **Audio Device Settings:** Add a proper configuration panel for ASIO/CoreAudio/ALSA device selection using `juce::AudioDeviceSelectorComponent`.
- [x] **Automation Lanes:** Add support for drawing and editing volume/pan automation in the Timeline.
- [x] **Advanced Transport:** Bind tempo, time signature, and bars/beats/ticks readout to the real engine state.

## Milestone 2: Asset Management & Cloud Sync (v0.5.0)
*Focus: Managing files and bridging the gap between local and cloud.*

- [x] **Google Drive Integration:** Browser "Cloud" tab with login/logout, file listing, and background download-to-import via `GoogleDriveClient::downloadFile`.
- [x] **Waveform Preview:** `AudioThumbnail`-based waveform preview strip in the Browser (Files tab). Drag-to-import triggers OS file drag via `DragAndDropContainer::performExternalDragDropOfFiles`.
- [x] **Piano Roll Editor:** Full MIDI note editor (`PianoRollEditor` / `PianoRollWindow`) — opens on double-click of a MIDI clip. Supports add, move, resize, delete, snap, and scrollbars.
- [ ] **Quantization:** Implement functional grid snapping and MIDI/Audio quantization logic.

## Milestone 3: AI-Enhanced Workflows (v0.8.0)
*Focus: Integrating production-ready AI models.*

- [ ] **ONNX Runtime Integration:** Finalize the production build of ONNX Runtime and link it to the `AIManager`.
- [ ] **Real Audio-to-MIDI:** Replace the mock transcription with a high-accuracy model (e.g., Basic Pitch or similar) wired to the Inspector.
- [ ] **Stem Separation:** Add a "Separate Stems" utility in the Inspector using Demucs or Spleeter models.
- [ ] **Model Management:** Create a UI for downloading, updating, and selecting different AI models to save on initial binary size.
- [ ] **AI-Assisted Mixing:** Implement basic AI-driven gain staging or EQ suggestions.

## Milestone 4: Polish & v1.0 Release
*Focus: Performance, stability, and user experience.*

- [ ] **Performance Optimization:** Optimize the audio graph for zero-latency performance and minimize UI thread blocking during heavy AI tasks.
- [ ] **UI/UX Refinement:** Add workspace layouts (Editing, Mixing, Recording), smooth UI animations, and high-DPI scaling support.
- [ ] **Keyboard Shortcuts:** Implement a customizable keyboard mapping system.
- [ ] **Testing & QA:** Complete unit test coverage for `ProjectData` and integration tests for the `AudioEngine`.
- [ ] **Documentation:** Comprehensive user manual and "Quick Start" video tutorials.
- [ ] **Packaging:** Create installers for Windows (NSIS/MSI) and macOS (DMG).

---

## Future Ideas (v2.0+)
- **Collaborative Editing:** Real-time multi-user project editing via cloud.
- **Mobile Companion App:** Remote transport and mixer control via tablet.
- **Expanded AI:** Generative MIDI tools and AI-driven synthesis.
- **Video Support:** Video playback track for film scoring workflows.
- **Project Syncing:** Implement automatic background syncing of project files and local assets to Google Drive. (Delayed to later undetermined stage)
