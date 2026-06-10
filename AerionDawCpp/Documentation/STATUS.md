# Aerion DAW Project Status — June 10, 2026 (v0.3.0 Pre-Alpha)

## Overview
Aerion DAW has closed the **Milestone 4 completion sprint** (v0.3.0). Milestones 1–4 — Editing, Mixing, Recording & Monitoring, and Project & Workflow — are **complete and verified against the source** (codebase audit, June 10 2026). The current focus is **Milestone 5: Polish & Stability (v0.4.0)**, where roughly a third of the scope already exists in partial form (packaging, release CI, typography scaling).

## Milestone Progress

| Milestone | Status |
|---|---|
| M1: DAW Essentials — Editing (v0.0.1) | **Complete** — clip/MIDI/transport editing including MIDI CC / Pitch lane in the Piano Roll |
| M2: DAW Essentials — Mixing (v0.1.0 → v0.1.1) | **Complete** — Phase/Mono, HPF/LPF, Inserts (incl. **bypass + drag-to-reorder**), Sends, Presets, Snapshots, clip + K-14 metering, submix folders + routing, cascading mute/solo |
| M3: DAW Essentials — Recording & Monitoring (v0.2.0) | **Complete** — Metronome, Count-In, Punch In/Out, PDC, multi-channel input routing, buffer safety readout, live recording waveform, full driver pack (ASIO/WASAPI/DirectSound/CoreAudio/ALSA/JACK/WinRT MIDI), Reset Audio Settings, per-track monitor modes, per-track MIDI controller selector |
| M4: DAW Essentials — Project & Workflow (v0.3.0) | **Complete** — Save/Load (`.aerion`), Collect & Save, Bounce/Freeze, Mixdown + Stems export, Tempo Map, Time Signature changes, per-track input/monitor persistence, customisable keyboard shortcuts (`AerionKeymap`), Recent Projects, Auto-save / Crash Recovery, icon system |
| M5: DAW Essentials — Polish & Stability (v0.4.0) | **In progress** — see inventory below |
| M6–M8 + Future USPs | Not started — `AIManager` is still a 2-second mock; ONNX Runtime declared in CMake but intentionally not linked |

---

## Milestone 5 Inventory (audited June 10, 2026)

What already exists versus what remains, verified against the source tree:

| M5 Item | State | Notes |
|---|---|---|
| Packaging — Windows | **~Done** | NSIS installer scaffolded in `CMakeLists.txt` (CPack, shortcuts, VC++ runtime bundling, installer icon). Production code-signing still missing. |
| Packaging — macOS | **Partial** | DMG + universal binary (ARM64 + x86_64) built by the release workflow; signing is ad-hoc only, **notarization not implemented**. |
| CI pipeline | **Partial** | `.github/workflows/package-release.yml` builds installers on tags. Build-on-PR/push workflow and a test stage added June 2026 (`build-test.yml`). |
| Unit & Integration Tests | **Started** | First smoke-test target added June 2026 (`AerionTests`): `ProjectData` round-trip + keymap serialisation. Coverage is minimal; engine-level tests still to come. |
| High-DPI / Retina | **Partial (~40 %)** | `Theme::uiSize()` / `kUiFontScale` typography layer shipped; fixed-pixel layout audit not started. |
| Performance Optimization | **Partial** | Splash/deferred device init, repaint scoping, tooltip/toolbar cadence done; timeline/piano-roll paint profiling outstanding. |
| Workspace Layouts | **Missing** | No layout manager or save/restore code yet. |
| Accessibility | **Missing** | No `setAccessibleName()` usage; keyboard-navigable mixer not started. |
| Error Reporting / Console panel | **Missing** | JUCE default dialogs only; no in-app crash reporter or DBG console. |

---

## Recently shipped (since last STATUS update)

- **Insert bypass + drag-to-reorder (M2 polish, June 2026):** engine APIs `isExternalPluginBypassed` / `setPluginBypassed` / `moveExternalPlugin` (with freeze-state guards); `BYP` pill and drag-handle reordering in the Inspector INSERTS list, Plugin Manager window, and Mixer insert rack.
- **Milestone 4 completion sprint (v0.3.0):** per-track input + monitor persistence, time signature changes UI, customisable keyboard shortcuts (`AerionKeymap` + `KeyboardShortcutsPanel` with conflict detection and `.aerionkeys` import/export), Mixer M/S icons via shared `drawTrackIconBtn`, freeze/tempo polish pass.
- **Freeze render lifecycle & MIDI arm data-loss fixes** (`270b459`).
- **Arranger Ruler redesign** — Reaper-style dual-band layout (`97fed9a`).

---

## Known scaffolding ahead of its milestone

- **Google Drive client (M8 footprint):** `GoogleDriveClient` already implements an OAuth2 + PKCE login flow, token persistence (`drive_tokens.json`), and a Browser "Cloud" tab that downloads files into the project. It is functional scaffolding for M8 Cloud Project Sync — kept out of the milestone tables so M8 planning starts from an honest baseline, but it should not be rewritten from scratch.
- **ONNX Runtime:** declared via `FetchContent_Declare` in CMake but deliberately not linked (build-size cost); linking is the first M8 task.
- **`AIManager`:** still the 2-second mock returning a hardcoded MIDI note — replaced as part of M8 Real Audio-to-MIDI.

---

## Current Build State
- **Platform**: Windows 11 (MSVC 2022 Build Tools / VS 2026)
- **Engine**: Tracktion Engine v3.2 / JUCE 8
- **Build**: Clean Debug build of the **AerionDaw** target; full solution builds may still hit unrelated demo targets (e.g. LV2 helper in third-party examples).
- **CI**: tag-triggered installer packaging (Windows NSIS, macOS DMG); PR/push build + test workflow (June 2026).

## Next Steps (priority order)

Mirrors Milestone 5 in [`ROADMAP.md`](./ROADMAP.md) so the two documents agree.

1. **CI build-on-PR + first smoke tests** — protect the codebase before the polish work churns it. *(Workflow + `AerionTests` target landed June 10 2026; grow coverage from here.)*
2. **Performance profiling** — timeline/piano-roll paint profiling; eliminate hot-path allocations.
3. **High-DPI audit** — sweep fixed pixel layouts now that the typography token layer exists.
4. **Error reporting** — in-app console panel for DBG logs in dev builds; structured crash reporter.
5. **Workspace layouts & accessibility** — named window layouts; screen-reader labels and keyboard-navigable mixer.
6. **Packaging finish line** — production code-signing (Windows + macOS) and macOS notarization.

## Trademarks

- **ASIO** is a trademark and software of Steinberg Media Technologies GmbH. The Steinberg ASIO SDK is bundled in-tree at `AerionDawCpp/External/ASIO-SDK_*/` under GPLv3; the official ASIO-compatible mark appears small on the splash (bottom-left) and with full attribution in the About dialog.
- **JUCE** is a trademark of Raw Material Software Limited.
- **VST** is a trademark of Steinberg Media Technologies GmbH (VST3 plugin hosting only — no Steinberg-proprietary VST2 code in this repo).
