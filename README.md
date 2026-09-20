<p align="center">
  <img src="AerionDawCpp/Resources/aerion_logo_horizontal.svg" alt="Aerion DAW Logo" width="800">
</p>

**Aerion DAW** is a native digital audio workstation built in **C++20** with **JUCE 8** and the **Tracktion Engine v3.2**. It targets serious home and project-studio production on **Windows 11** first, with **macOS** builds supported through CI and release packaging.

**Current version:** v0.3.0 Pre-Alpha · **Active milestone:** [M5 — Polish & Stability](AerionDawCpp/Documentation/ROADMAP.md) (targeting v0.4.0)

[![Build & Smoke Tests](https://github.com/aethos-studio/Aerion-DAW/actions/workflows/build-test.yml/badge.svg)](https://github.com/aethos-studio/Aerion-DAW/actions/workflows/build-test.yml)

---

## What Aerion is today

Aerion is a **working pre-alpha DAW**, not a demo shell. You can record, edit, mix, and export a complete song session on Windows today. Milestones 1–4 — editing, mixing, recording, and project workflow — are implemented and in daily use. Milestone 5 adds stability, packaging, tests, and performance work toward a shippable v0.4.0.

The long-term vision includes AI-assisted workflows and cloud project sync, but those are **scaffolding only** right now. The current focus is making the core DAW fast, stable, and trustworthy.

---

## What you can do

### Arrange and edit

- Multi-track **audio**, **MIDI**, and **folder** tracks with free reordering and submix folders
- **Position-aware drag-and-drop**: ghost previews, grid snap, consecutive multi-file import
- Clip trim, split, move, nudge, fades, comps, loop regions, and markers
- **Piano roll** with note editing, velocity lane, MIDI CC / pitch-bend lanes, quantize, and snap
- **Tempo map** and **time signature** changes on the timeline ruler
- **Automation** lanes for volume and pan

### Record

- Per-track **record arm**, input routing, and monitor modes (Auto / On / Off)
- **ASIO**, WASAPI, and DirectSound on Windows; CoreAudio on macOS
- Metronome, count-in, punch in/out, plugin delay compensation
- Live recording waveform that grows under the playhead

### Mix

- Real-time level meters, faders, pan, mute, and solo
- **Detachable mixer** window and per-track inspector
- Phase invert, mono sum, HPF/LPF quick filters
- Serial **insert** chain with bypass and drag-to-reorder
- **Sends**, plugin presets, and mix **snapshots**
- **VST3** hosting (Windows/Linux) and **AU** (macOS)

### Project and export

- Save and load **`.aerion`** project files with full round-trip state
- Collect & save, auto-save, and crash recovery
- **Freeze / bounce** tracks; mixdown and **stems** export to WAV / AIFF / FLAC / OGG
- Customisable **keyboard shortcuts** (`.aerionkeys` import/export)
- **Workspace layouts** — built-in Editing / Mixing / Recording presets plus saved custom layouts

---

## Platforms

| Platform | Status |
|---|---|
| **Windows 11 / 10 (x64)** | Primary development and daily-use target. Visual Studio 2022 + MSVC. |
| **macOS** | Secondary. Built and packaged via GitHub Actions (universal ARM64 + Intel DMG). |
| **Linux** | Not a supported product target. Engine code has ALSA/JACK hooks via JUCE, but there is no Linux installer or CI preset on `main`. |

---

## Recent progress (September 2025)

Work landed on `main` as part of the Milestone 5 performance push:

- **Timeline row culling** — playback playhead repaints skip off-screen track rows, roughly halving paint cost on the hot path during transport
- **Paint profiling** — opt-in `AERION_PROFILE_*` probes and a `win-msvc-profiling` CMake preset for measuring UI cost in Release builds
- **Headless benchmark** — `AerionBench` renders the Timeline offscreen for repeatable paint timings
- **AudioEngine smoke tests** — 13 headless tests covering tracks, mute/solo, tempo map, snapshots, and transport flags
- **Stability fixes** — clip/piano-roll lifetime safety and guards against editing tracks while freeze is in progress

---

## What's still in progress (M5)

| Area | State |
|---|---|
| UI performance | Profiling infrastructure in place; static chrome caching and edit-driven repaint storms remain |
| High-DPI / Retina | Typography tokens shipped (~40%); fixed-pixel layout audit not started |
| Tests | `ProjectData`, `AerionKeymap`, and `AudioEngineManager` smoke tests; no GUI or audio-thread tests yet |
| Packaging | Windows NSIS + optional self-signed signing; macOS DMG without notarization |
| Accessibility | Not started |
| AI / Cloud | `AIManager` is a mock; Google Drive client has placeholder OAuth credentials |

See [`STATUS.md`](AerionDawCpp/Documentation/STATUS.md) and [`ROADMAP.md`](AerionDawCpp/Documentation/ROADMAP.md) for the full milestone breakdown.

---

## Getting started (Windows)

### Prerequisites

- **CMake** 3.20+
- **Visual Studio 2022** with the *Desktop development with C++* workload (MSVC, x64)
- **Git** (Tracktion Engine is fetched on first configure)

Open the **repository root** in your editor. Root `CMakePresets.json` includes the Aerion presets; `.vscode/settings.json` points CMake Tools at `AerionDawCpp/`.

### Build

From the repository root in **PowerShell**:

```powershell
# Configure + build (Debug)
cmake --preset win-msvc-debug -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug

# Release
cmake --build build --preset win-msvc-release

# Smoke tests
cmake --preset win-msvc-debug-tests -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug-tests
ctest --test-dir build -C Debug --output-on-failure
```

The app binary:

```text
build\AerionDaw_artefacts\Debug\Aerion DAW.exe
```

For profiling builds, release packaging, and the paint benchmark, see [`CURSOR_DEVELOPMENT.md`](AerionDawCpp/Documentation/CURSOR_DEVELOPMENT.md).

### CI and releases

Both GitHub Actions workflows are **manual only** (`workflow_dispatch`):

- **build-test** — Debug build + smoke tests on Windows and macOS
- **release-package** — Windows NSIS installer and macOS DMG published to a GitHub Release

Run them from the [Actions tab](https://github.com/aethos-studio/Aerion-DAW/actions).

---

## Architecture

Aerion follows strict **Model–View–Controller** separation:

| Layer | Component | Role |
|---|---|---|
| Model | `ProjectData` | `juce::ValueTree` is the single source of truth for project state |
| Controller | `AudioEngineManager` | Wraps the Tracktion `Edit`, transport, and real-time audio graph |
| View | JUCE components | Observe the ValueTree; UI repaints when state changes |

Application code lives under `AerionDawCpp/Source/`. The largest UI surface is currently consolidated in `UIComponents.h` (a known refactor target as M5 performance work continues).

---

## Repository layout

```text
Aerion-DAW/
  README.md                 This file
  LICENSE                   GPLv3
  CMakePresets.json         Includes AerionDawCpp presets
  .github/                  CI workflows
  AerionDawCpp/             CMake project root
    Source/                 Application code
    Resources/              Icons, fonts, SVG assets
    External/               Third-party SDKs (e.g. Steinberg ASIO on Windows)
    Documentation/          Roadmap, status, dev guides
    Tools/                  Dev helper scripts
```

Build output (`build/`), IDE caches, and local scratch folders are gitignored.

---

## System requirements

| | Minimum | Recommended |
|---|---|---|
| **OS** | Windows 10 (64-bit) | Windows 11 (64-bit) |
| **CPU** | Intel Core i5 / AMD Ryzen 5 | Intel Core i7 / AMD Ryzen 7 |
| **RAM** | 4 GB | 16 GB |
| **Graphics** | OpenGL 3.2 compatible | Dedicated GPU |
| **Audio** | Windows Audio / ASIO4ALL | Dedicated ASIO interface |

---

## License

This project is licensed under the terms in [`LICENSE`](LICENSE).

**Trademarks:** ASIO is a trademark of Steinberg Media Technologies GmbH. JUCE is a trademark of Raw Material Software Limited. VST is a trademark of Steinberg Media Technologies GmbH. Aerion uses VST3 plugin hosting only.
