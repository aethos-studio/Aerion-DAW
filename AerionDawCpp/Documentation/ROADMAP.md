# Aerion DAW Roadmap

This document outlines the development path for Aerion DAW. The strategy is simple: **ship a rock-solid, feature-complete DAW first — then layer in the USPs that make Aerion unique.**

In practice that means two phases:

| Phase | Milestones | Goal |
|---|---|---|
| **Complete core** | M6 | In-app updates first, then the last gaps to a modern DAW baseline: full automation, time-stretch, sidechain, controller mapping, templates and metering. |
| **Differentiators** | M7–M9 + Future | What makes Aerion distinct: composition tools, creative/performance workflows, AI, cloud and collaboration. |

A milestone is done when its items are verified: benchmark targets for performance work (see [`PERFORMANCE.md`](./PERFORMANCE.md)), pixel checks and snapshot renders for UI work (`AerionBench --verify` / `--snapshots`), and smoke tests in CI for engine work.

---

## Where Aerion is now

**v0.5.0 Alpha** (October 2026) is the first public release, with the in-app update check, Follow Playback and a Mac-native menu bar, shortcuts and trackpad gestures. Milestones 1–5 (editing, mixing, recording and monitoring, project workflow, polish and stability) are complete; what they delivered is summarised in the [README](../../README.md#shipped-milestones). **v0.5.1** adds the first Milestone 6 items: sidechain routing, clip gain / normalise / reverse, and project templates. This roadmap covers what comes next.

---

## Milestone 6 — DAW Essentials: Complete Core (v0.6.0)
*Close the gaps every mainstream DAW already covers, so Aerion is complete before it adds differentiators. Several items build on capabilities Tracktion Engine already ships, so they are exposure and UI work rather than new DSP. Built-in (stock) instruments and effects are not part of Aerion: they will come as a separate product (decided October 7, 2026). Aerion hosts the user's plugins; new MIDI tracks start without an instrument.*

- [ ] **Update Mechanism (first; ships in v0.5.0, done once a real update from 0.5.0 to a later release has gone through):** Aerion checks the GitHub releases for a newer version at startup (and on demand from Help → Check for Updates), shows what changed, downloads the installer for the platform, checks it, and runs it after Aerion closes. Alpha builds also see pre-releases; a setting turns the automatic check off. Comes first because it gets every later fix to alpha testers.
- [ ] **Full Parameter Automation:** Automate any plugin or mixer parameter, not just volume and pan. Per-track lane chooser, multiple visible lanes, automation modes (Read / Write / Touch / Latch) that record from UI and controller moves, point thinning, and copy/paste of automation with clips.
- [ ] **Audio Warping & Time-Stretch:** Audio clips follow tempo changes (auto-tempo), warp markers for manual timing correction, per-clip pitch and speed controls. SoundTouch is already compiled in (`TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH`); evaluate higher-quality stretchers (Rubber Band, élastique) and their licences.
- [x] **Sidechain Routing (v0.5.1):** Sidechain inputs for hosted plugins that support them, set up from the insert slot's context menu in the Inspector and the Mixer (for ducking, sidechain compression and gating).
- [ ] **MIDI Learn & Controller Mapping:** Map hardware knobs, faders and buttons to any parameter by moving the control; mappings saved per project, with user-level defaults.
- [ ] **Control Surface Support:** Mackie Control (MCU) and HUI transport and mixer control, building on Tracktion's control surface support.
- [ ] **Audio Clip Processing:** Reverse, normalise, clip gain envelope, and pitch/speed per clip; non-destructive where possible, with rendered results kept in the project folder. *Clip gain, normalise and reverse shipped in v0.5.1; the gain envelope and pitch/speed come with time-stretch.*
- [ ] **Project & Track Templates:** Save and start from project templates (tracks, routing, devices, layout) and insert track templates (a track or folder with its devices and sends). *Project templates shipped in v0.5.1; track templates are still to do.*
- [ ] **Analysis Metering:** Loudness meter on the master (integrated / short-term / momentary LUFS and true peak), spectrum analyser, and phase correlation meter; loudness targets for common delivery platforms.
- [ ] **CLAP Plugin Hosting — Feasibility Spike:** Check how JUCE 8 and Tracktion Engine support CLAP hosting today, then implement it if the support is solid enough.

---

## Milestone 7 — Pro Composition & Audio Editing (v0.7.0)
*Close pro-composition DAW gaps for songwriters, composers, and vocal producers.*

*Custom keyboard shortcuts shipped in Milestone 4 — see `AerionKeymap` / `KeyboardShortcutsPanel`.*

- [ ] **Command Palette + Action Registry:** Centralise every menu item, toolbar action, shortcut, and context command behind a single action registry. This becomes the foundation for custom keymaps, macros, command search, and eventual scripting.
- [ ] **Macro Actions:** Let users chain existing commands into named macros, assign shortcuts, and store them in the project/user settings. This is the REAPER-style productivity bridge before full scripting.
- [ ] **Chord Track:** Add a global chord lane above the Timeline. Chords should be insertable/editable on the ruler, saved in the `.aerion` project, and exposed to MIDI tools, future Session Players, and AI generation.
- [ ] **Scale-Aware Piano Roll:** Add global/project scale selection, per-clip scale override, "highlight in scale", "filter to scale", and scale quantize for selected MIDI notes.
- [ ] **Arrangement Variants / Scratch Pads:** Add alternate arrangement lanes or scratch pads so users can try song structures without duplicating projects. Keep clips linked where possible; allow committing a scratch arrangement back to the main Timeline.
- [ ] **ARA / Integrated Pitch Workflow — Feasibility Spike:** Evaluate Tracktion Engine/JUCE support for ARA2 hosting and define the save/load/render contract for Melodyne/RePitch-style workflows.
- [ ] **Pitch + Timing Editor (First Pass):** If ARA is viable, integrate ARA plugin workflows. If not, implement a native analysis cache with transient markers, basic pitch display, and elastic timing handles as a stepping stone.
- [ ] **Mastering / Project Page:** Add a release workspace for song sequencing, loudness analysis, inter-song spacing, export presets, revision notes, and album/EP delivery exports.

---

## Milestone 8 — Creative Production & Performance (v0.8.0)
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
- [ ] **Out-of-process plugin hosting:** run each third-party plugin in its own process, with audio and MIDI passed through shared memory and its editor hosted across the process boundary. Needed for crashes in an open plugin editor and for any plugin crash on macOS, which cannot be recovered in-process. A large architectural change; schedule it as its own milestone item after M6.

---

## Milestone 9 — AI, Cloud & Collaboration Differentiators (v0.9.0+)
*Make Aerion feel distinct instead of just feature-complete.*

*If AI becomes Aerion's headline feature, ONNX Runtime Integration and Real Audio-to-MIDI can be pulled forward to start right after Milestone 6; today the only AI piece is the `AIManager` mock.*

- [ ] **ONNX Runtime Integration:** Link runtime, manage model loading off the UI thread, and expose a model capability registry to the app.
- [ ] **Model Manager UI:** Download, update, remove, and select AI models. Keep the base installer lean and make model storage/versioning explicit.
- [ ] **Real Audio-to-MIDI:** Replace the `AIManager` mock with real transcription, clip selection, preview, correction, and commit-to-MIDI workflow.
- [ ] **Stem Separation:** Add "Separate Stems" for vocals, drums, bass, and other instruments, with GPU/CPU capability checks and background progress.
- [ ] **AI-Assisted Mixing:** Add gain staging suggestions, masking warnings, EQ matching, loudness targets, and mix snapshot comparison.
- [ ] **AI Arrangement Assistant:** Use Chord Track, markers, clip metadata, and arrangement variants to suggest intros, drops, bridges, edits, and alternate structures.
- [ ] **Cloud Project Sync:** Sync `.aerion` project files plus referenced audio to Google Drive first, then abstract the provider layer for future services. *Scaffolding exists: `GoogleDriveClient` (OAuth2/PKCE, Browser Cloud tab) — OAuth client credentials must be configured before login works in the field.*
- [ ] **Version History:** Browse project snapshots, compare metadata, restore prior versions, and recover individual clips or mix states.
- [ ] **Collaboration Sessions:** Real-time or near-real-time multi-user project sessions with conflict rules for clips, tracks, mixer state, and comments.
- [ ] **Mobile Companion App:** Remote transport, marker navigation, recording controls, monitor mix controls, and macro control surface.

---

## Future — USPs & Differentiators (v1.0+)

These are long-horizon expansions after the DAW core, pro workflows, creative tools, AI, and cloud foundations are stable.

### Linux Support
Adding support for Linux Systems (Flatpak)

---

## Trademarks

- **ASIO** is a trademark and software of Steinberg Media Technologies GmbH. The Steinberg ASIO SDK is bundled in-tree at `AerionDawCpp/External/ASIO-SDK_*/` under GPLv3; the official ASIO-compatible mark appears small on the splash (bottom-left) and with full attribution in the About dialog.
- **JUCE** is a trademark of Raw Material Software Limited.
- **VST** is a trademark of Steinberg Media Technologies GmbH (Aerion uses VST3 plugin hosting only).
