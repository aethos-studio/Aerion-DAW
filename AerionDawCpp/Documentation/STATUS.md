# Aerion DAW Project Status — July 9, 2026 (v0.3.0 Pre-Alpha)

## Overview
Aerion DAW has closed the **Milestone 4 completion sprint** (v0.3.0). Milestones 1–4 — Editing, Mixing, Recording & Monitoring, and Project & Workflow — are **complete and verified against the source** (codebase audit, July 2026). The current focus is **Milestone 5: Polish & Stability (v0.4.0)**, where CI now builds and smoke-tests on **both Windows and macOS**, release packaging is a separate workflow, and optional self-signed Windows code signing has landed. Both workflows are **manual (`workflow_dispatch`) only** — run them from the Actions tab. Typography scaling and packaging scaffolds remain partial.

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

## Milestone 5 Inventory (audited July 9, 2026)

What already exists versus what remains, verified against the source tree:

| M5 Item | State | Notes |
|---|---|---|
| CI pipeline | **Done** | `.github/workflows/build-test.yml` — Debug build + `ctest` smoke tests, **manually triggered** (`workflow_dispatch`), running on **both** a Windows MSVC/Ninja runner and a macOS Clang/Ninja runner. |
| Release packaging workflow | **Done** | `.github/workflows/package-release.yml` (`release-package`) — manual (`workflow_dispatch`) workflow, independent of the smoke-test workflow, that builds the Windows NSIS installer and macOS DMG and publishes both to a GitHub Release (tag/draft/pre-release/notes are run inputs). |
| Unit & Integration Tests | **Started** | `AerionTests`: `ProjectData` round-trip + `AerionKeymap` serialisation/conflicts, now verified green on both Windows and macOS. `AudioEngine` smoke tests still to come. |
| Packaging — Windows | **~Done** | NSIS installer scaffolded in `CMakeLists.txt` (CPack, shortcuts, VC++ runtime bundling, installer icon). **Optional self-signed code signing** now available (`AerionDawCpp/Tools/New-AerionSelfSignedCert.ps1` + `WINDOWS_CERT_PFX_BASE64` / `WINDOWS_CERT_PASSWORD` secrets); a paid OV/EV certificate is still required to clear the SmartScreen "unknown publisher" prompt. |
| Packaging — macOS | **Partial** | DMG + universal binary (ARM64 + x86_64) built by the release workflow; signing is ad-hoc only, **notarization not implemented** (requires a paid Apple Developer account). |
| High-DPI / Retina | **Partial (~40 %)** | `Theme::uiSize()` / `kUiFontScale` typography layer shipped; fixed-pixel layout audit not started. |
| Performance Optimization | **Partial** | Splash/deferred device init, repaint scoping, tooltip/toolbar cadence done; timeline/piano-roll paint profiling outstanding. |
| Workspace Layouts | **Missing** | No layout manager or save/restore code yet. |
| Accessibility | **Missing** | No `setAccessibleName()` usage; keyboard-navigable mixer not started. |
| Error Reporting / Console panel | **Missing** | JUCE default dialogs only; no in-app crash reporter or DBG console. |
| App version / title bar | **Done** | CMake `project` version **0.3.0**; menu bar + window title use `Theme::windowTitle()` (`<ProjectName> — Aerion DAW`); About dialog reads `ProjectInfo::versionString`. |

---

## Recently shipped (since last STATUS update)

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

- **Google Drive client (M8 footprint):** `GoogleDriveClient` implements OAuth2 + PKCE, token persistence, and a Browser "Cloud" tab — but **`clientId` / `clientSecret` are still placeholders** (`YOUR_CLIENT_ID`) until a Google Cloud desktop OAuth client is configured. Do not rewrite from scratch for M8; wire credentials and finish sync semantics instead.
- **ONNX Runtime:** declared via `FetchContent_Declare` in CMake but deliberately not linked (build-size cost); linking is the first M8 task.
- **`AIManager`:** still the 2-second mock returning a hardcoded MIDI note — replaced as part of M8 Real Audio-to-MIDI.

---

## Current Build State
- **Platform**: Windows 11 (primary local dev — Visual Studio 2022, MSVC x64). macOS builds via CI/release workflow.
- **Presets**: `win-msvc-debug`, `win-msvc-release`, `win-msvc-debug-tests` (see root `CMakePresets.json` + `AerionDawCpp/Documentation/CURSOR_DEVELOPMENT.md`)
- **Engine**: Tracktion Engine v3.2 / JUCE 8
- **Build**: Clean Debug build of the **AerionDaw** target; full solution builds may still hit unrelated demo targets (e.g. LV2 helper in third-party examples).
- **CI**: both workflows are manual (`workflow_dispatch`) only. `build-test.yml` runs the Debug build + `AerionSmokeTests` on Windows (MSVC/Ninja) and macOS (Clang/Ninja) — run it before merging a PR. `package-release.yml` (`release-package`) builds the Windows NSIS installer and macOS DMG, with optional self-signed Windows code signing, then publishes a GitHub Release with both attached.

## Next Steps (priority order)

Mirrors Milestone 5 in [`ROADMAP.md`](./ROADMAP.md) so the two documents agree.

1. **CI build-on-PR + first smoke tests** — protect the codebase before the polish work churns it. *(Done — `build-test.yml` runs `AerionTests` on Windows and macOS; grow `AudioEngine` coverage next.)*
2. **Performance profiling** — timeline/piano-roll paint profiling; eliminate hot-path allocations.
3. **High-DPI audit** — sweep fixed pixel layouts now that the typography token layer exists.
4. **Error reporting** — in-app console panel for DBG logs in dev builds; structured crash reporter.
5. **Workspace layouts & accessibility** — named window layouts; screen-reader labels and keyboard-navigable mixer.
6. **Packaging finish line** — self-signed Windows code signing is done *(`release-package` + `New-AerionSelfSignedCert.ps1`)*; production OV/EV code-signing (to clear SmartScreen) and macOS notarization still require a paid certificate/Apple Developer account.

## Trademarks

- **ASIO** is a trademark and software of Steinberg Media Technologies GmbH. The Steinberg ASIO SDK is bundled in-tree at `AerionDawCpp/External/ASIO-SDK_*/` under GPLv3; the official ASIO-compatible mark appears small on the splash (bottom-left) and with full attribution in the About dialog.
- **JUCE** is a trademark of Raw Material Software Limited.
- **VST** is a trademark of Steinberg Media Technologies GmbH (VST3 plugin hosting only — no Steinberg-proprietary VST2 code in this repo).
