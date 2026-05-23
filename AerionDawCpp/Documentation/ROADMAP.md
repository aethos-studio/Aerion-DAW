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
- Add (click-drag), move (drag body), resize (drag right edge), delete (right-click / Delete)
- Selection-aware editing: single/multi-select, marquee select, copy/cut/paste, duplicate, nudge, transpose, quantize, and Escape-to-clear.
- Snap to configurable note length with visible dynamic subdivisions; note insertion snaps to the clicked cell start.
- **MIDI CC / Pitch lane:** switchable lane (Mod, Volume, Pan, Expression, Sustain, Pitch Bend, custom CC) with resizable splitter above the velocity lane; draw/edit via `MidiList::setControllerValueAt` / `removeControllerEvent`; selection persisted per clip (`IDs::pianoRollCC`)
- Horizontal and vertical scrollbars
- Opens on double-click of a MIDI clip in the Timeline

### AI (Scaffolding)
- `AIManager` thread scaffold — audio-to-MIDI transcription stub
- Mock transcription (2 s delay → hardcoded MIDI note) wired to `processTranscription()`

### UI & Branding
- dark theme (`MetalLookAndFeel`)
- Animated fog splash screen — full intro hold; Cinzel for splash title/subtitle only; body UI uses **system sans** at scaled sizes (`Theme::uiSize` / `kUiFontScale`)
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

## Milestone 2 — DAW Essentials: Mixing (v0.1.0 Pre-Alpha → v0.1.1 hotfix)
Implementing professional mixing workflows while preserving a clean, beginner-friendly UI through progressive disclosure.
Keep the Console clean. Put the advanced technical tools in the Inspector.

### Mixing Features (v0.1.0)
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

### Hotfixes (v0.1.1 — May 2026)
- [x] **Meter Post-Fader Gain:** Fixed meters showing pre-fader peak instead of post-fader; `Primitives.h::paintFader()` now applies fader gain to both current and max peak readings before display (`peak + faderGainDb`, `maxPeak + faderGainDb`).
- [x] **Project Reload UI Refresh:** Fixed UI not refreshing when loading a new project without window resize; `MainComponent::editStateChanged()` now explicitly calls `mixer.repaint()` and `timeline.repaint()` alongside the existing `browser.repaint()`.
- [x] **Typography Polish:** Global font scale increased (`kUiFontScale` 1.14 → 1.25) and targeted micro-label fixes across Transport captions, Console strip, Piano Roll, Arrangement, and Primitives layers for improved readability (7 UIComponents fixes + 2 Primitives fixes + tooltip coupling fix).

---

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

- [x] **Full Project Save / Load:** Complete round-trip serialisation of all tracks, clips, plugin state, automation, and mixer settings to a single `.aerion` file (XML + referenced audio).
- [x] **Audio File Management — Collect & Save:** Copy all referenced audio into a project folder; detect and warn about missing files on open.
- [x] **Bounce / Freeze:** Freeze state persistence (`IDs::frozen`, `IDs::preFreeze`, `IDs::freezeFile`), async render/unfreeze workflow, Inspector Freeze / Unfreeze state, Timeline track/clip badges, and context-menu actions.
- [x] **Export — Mixdown:** `MixdownExportDialog` + `MixdownExportJob` render the master to WAV / AIFF / FLAC / OGG with configurable sample rate and channels, a true pre-rendered waveform preview (with clip detection), bounds selection (Selection / Loop / Full), tail length, format presets, and filename wildcards.
- [x] **Export — Stems:** Export each track (or bus) individually as a rendered audio file. `sourceBox` lists all audio tracks; selecting one renders only that track to file with master plugins disabled.
- [x] **Tempo Map:** AudioEngine wrappers plus interactive Timeline Tempo Lane with visible BPM nodes, double-click insert, drag to move/change BPM, and right-click delete for non-root nodes.
- [x] **Time Signature Changes:** Per-bar time signature changes can be inserted from the Transport at the playhead bar or from the Timeline ruler; visible signature flags can be selected, dragged by bar, edited via preset menu, and removed.
- [x] **Per-track Input + Monitor Persistence:** Inspector audio input, MIDI controller pin, and monitor mode now persist on each track `ValueTree` via `IDs::trackInputDeviceIdx`, `IDs::midiInputDevice`, and `IDs::monitorMode`, with migration from legacy RuntimeState XML.
- [x] **Keyboard Shortcut System:** `AerionKeymap` + `KeyboardShortcutsPanel` provide editable bindings per action, live conflict detection with reassign/cancel prompt, import/export of `.aerionkeys` files, reset-to-defaults, and persistence via `appProperties`. `MainComponent` and `PianoRollEditor` dispatch through `AerionKeymap::matches()` so every rebindable action picks up custom bindings.
- [x] **Recent Projects List:** Menu → Open Recent with up to 10 entries.
- [x] **Crash Recovery:** Auto-save every N minutes to a recovery folder; prompt to restore on next launch.
- [x] **Hotkey implementation:** All catalog actions (File new/open/save, Edit undo/redo, Transport play-stop/record/go-to-start, Clip nudge/trim/delete, Audio crossfade, Track mute/solo/arm, Piano Roll select-all/copy/cut/paste/duplicate/delete/nudge/transpose/quantize/clear-selection) now flow through the `AerionKeymap` dispatch layer.
- [x] **UX Redesign — Icon System (Milestone 4 Polish):** Inspector ARM/MUTE/SOLO icons, Toolbar XF icon, Timeline M/S/R/A track buttons, SVG Transport icons, and Mixer-side M/S icons now render via the shared `drawTrackIconBtn`.

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

## Milestone 6 — Pro Composition & Audio Editing (v0.5.0)
*Close the Logic / Cubase / Studio One gaps for songwriters, composers, and vocal producers.*

- [ ] **Command Palette + Action Registry:** Centralise every menu item, toolbar action, shortcut, and context command behind a single action registry. This becomes the foundation for custom keymaps, macros, command search, and eventual scripting.
- [ ] **Custom Key Commands:** Complete the Help / Keyboard Shortcuts panel with editable bindings, import/export keymap files, conflict detection, and a "reset to defaults" path.
- [ ] **Macro Actions:** Let users chain existing commands into named macros, assign shortcuts, and store them in the project/user settings. This is the REAPER-style productivity bridge before full scripting.
- [ ] **Chord Track:** Add a global chord lane above the Timeline. Chords should be insertable/editable on the ruler, saved in the `.aerion` project, and exposed to MIDI tools, future Session Players, and AI generation.
- [ ] **Scale-Aware Piano Roll:** Add global/project scale selection, per-clip scale override, "highlight in scale", "filter to scale", and scale quantize for selected MIDI notes.
- [ ] **Arrangement Variants / Scratch Pads:** Add alternate arrangement lanes or scratch pads so users can try song structures without duplicating projects. Keep clips linked where possible; allow committing a scratch arrangement back to the main Timeline.
- [ ] **ARA / Integrated Pitch Workflow — Feasibility Spike:** Evaluate Tracktion Engine/JUCE support for ARA2 hosting and define the save/load/render contract for Melodyne/RePitch-style workflows.
- [ ] **Pitch + Timing Editor (First Pass):** If ARA is viable, integrate ARA plugin workflows. If not, implement a native analysis cache with transient markers, basic pitch display, and elastic timing handles as a stepping stone.
- [ ] **Mastering / Project Page:** Add a release workspace for song sequencing, loudness analysis, inter-song spacing, export presets, revision notes, and album/EP delivery exports.

---

## Milestone 7 — Creative Production & Performance (v0.6.0)
*Close the Ableton / Bitwig / FL Studio gaps for loop-based writing, modulation, and beat production.*

- [ ] **Clip Launcher:** Add a non-linear scene/clip grid beside the Arranger. Clips should launch in sync, support follow actions later, and record performances back into the Timeline.
- [ ] **Pattern / Step Sequencer:** Add a drum-and-melody pattern editor with per-step velocity, probability, repeat, gate, and resolution. Patterns should appear as Timeline clips and open in a dedicated editor.
- [ ] **MIDI Operators:** Add note probability, repeats, randomisation, and conditional playback to MIDI clips, inspired by Bitwig Operators and modern generative sequencers.
- [ ] **Generative MIDI Tools:** Add transform/generate tools for chords, basslines, arpeggios, melodies, rhythms, humanise, strum, density, and variation. These should be deterministic when seeded so results can be recalled.
- [ ] **Unified Modulation System:** Add track/clip modulators (LFO, envelope follower, step modulator, random, macro controls) assignable to plugin parameters, mixer parameters, and selected clip parameters.
- [ ] **Macro Controls:** Add per-track and per-project macro knobs that can control multiple destinations with ranges and polarity.
- [ ] **MPE Editing:** Extend the Piano Roll to display and edit per-note pitch, pressure, slide/timbre, and pan where supported by MIDI data and hosted instruments.
- [ ] **Sample / Loop Browser Intelligence:** Add tempo/key detection, favourites, tags, "find similar sounds", and one-click preview sync to the project tempo.
- [ ] **Live Performance Mode:** Add a performance-focused workspace with large transport, launcher scenes, mixer macros, panic/stop-all, and hardware MIDI mapping.

---

## Milestone 8 — AI, Cloud & Collaboration Differentiators (v0.7.0+)
*Make Aerion feel distinct instead of just feature-complete.*

- [ ] **ONNX Runtime Integration:** Link runtime, manage model loading off the UI thread, and expose a model capability registry to the app.
- [ ] **Model Manager UI:** Download, update, remove, and select AI models. Keep the base installer lean and make model storage/versioning explicit.
- [ ] **Real Audio-to-MIDI:** Replace the `AIManager` mock with real transcription, clip selection, preview, correction, and commit-to-MIDI workflow.
- [ ] **Stem Separation:** Add "Separate Stems" for vocals, drums, bass, and other instruments, with GPU/CPU capability checks and background progress.
- [ ] **AI-Assisted Mixing:** Add gain staging suggestions, masking warnings, EQ matching, loudness targets, and mix snapshot comparison.
- [ ] **AI Arrangement Assistant:** Use Chord Track, markers, clip metadata, and arrangement variants to suggest intros, drops, bridges, edits, and alternate structures.
- [ ] **Cloud Project Sync:** Sync `.aerion` project files plus referenced audio to Google Drive first, then abstract the provider layer for future services.
- [ ] **Version History:** Browse project snapshots, compare metadata, restore prior versions, and recover individual clips or mix states.
- [ ] **Collaboration Sessions:** Real-time or near-real-time multi-user project sessions with conflict rules for clips, tracks, mixer state, and comments.
- [ ] **Mobile Companion App:** Remote transport, marker navigation, recording controls, monitor mix controls, and macro control surface.

---

## Future — USPs & Differentiators (v1.0+)

These are long-horizon expansions after the DAW core, pro workflows, creative tools, AI, and cloud foundations are stable.

### Linux Support
Adding support for Linux Systems (Flatpak)

### Update Mechanism
In software update module that will check the github repo for new releases and offers to update automatically

### Expanded Platform
- **Video Support:** Video playback track with frame-accurate sync for film scoring
- **AI-Driven Synthesis:** Prompt-to-patch synthesis for built-in virtual instruments
- **Score / Notation Editor:** Dorico-style notation view for MIDI clips, chord symbols, lyrics, and printable parts.
- **Surround / Immersive Mixing:** 5.1 / 7.1 / Dolby Atmos-style routing, panners, ADM/BWF export, and monitor calibration.
- **Scripting SDK:** Lua or JavaScript scripting API for actions, project manipulation, batch editing, and UI extensions.

---

## Milestone 4 — Completion Sprint Closed (v0.3.0)

All M4 completion-sprint items shipped:

1. ✅ **Per-track Input + Monitor Persistence** — Inspector audio input, MIDI controller pin, and monitor mode persist on the track `ValueTree`; legacy RuntimeState XML migrated on load.
2. ✅ **Time Signature Changes UI** — Transport edits insert/update at the playhead bar; Timeline ruler shows selectable/drag-editable signature flags with preset and remove actions.
3. ✅ **Customisable Keyboard Shortcuts** — `Source/Keymap.h` defines `AerionKeymap` + `AerionActionCatalog`; the new `KeyboardShortcutsPanel` (in `UIComponents.h`) is an editable list with click-to-capture, conflict detection (offers reassign/cancel), reset-to-defaults, and import/export of `.aerionkeys` files; bindings persist via `appProperties` under key `keymap`.
4. ✅ **Mixer M/S Icons** — `Mixer::drawSideButtonColumn` renders mute/solo via `Timeline::drawTrackIconBtn` using `BinaryData::aerion_mute_svg` / `aerion_Solo_svg`, matching Timeline and Inspector.
5. ✅ **Freeze/Tempo Polish Pass** — Tempo lane now shows a resize cursor and a brighter highlight on hover (`hoveredTempoNodeIndex`); non-root tempo nodes are clamped between their neighbours during drag so ordering can no longer flip; freeze/unfreeze guards (empty track, already-freezing, missing freeze WAV) verified.

**Next:** Milestone 5 — performance profiling, high-DPI audit, workspace layouts, error reporting, tests/CI, and packaging.

---

## Trademarks

- **ASIO** is a trademark and software of Steinberg Media Technologies GmbH. The Steinberg ASIO SDK is bundled in-tree at `AerionDawCpp/External/ASIO-SDK_*/` under GPLv3; the official ASIO-compatible mark appears small on the splash (bottom-left) and with full attribution in the About dialog.
- **JUCE** is a trademark of Raw Material Software Limited.
- **VST** is a trademark of Steinberg Media Technologies GmbH (Aerion uses VST3 plugin hosting only).
