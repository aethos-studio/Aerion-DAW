<p align="center">
  <img src="AerionDawCpp/Resources/aerion_logo_horizontal.svg" alt="Aerion DAW Logo" width="800">
</p>

**A modern Digital Audio Workstation built with C++20, JUCE 8, and the Tracktion Engine.**

---

## Why Aerion DAW?

Aerion is designed to bridge the gap between high-end professional production and modern, cloud-connected workflows.

- **⚡ Native Performance:** Built in C++20 with JUCE 8 and the Tracktion Engine for rock-solid, low-latency audio processing.
- **🎨 High-Polish UI:** Features a unique dark theme, animated splash screens, and Studio One-style position-aware drag-and-drop.
- **🤖 AI-Ready:** Foundation laid for future AI-enhanced workflows including audio-to-MIDI transcription and stem separation.

---

## Key Features


| Area             | Features                                                                                                               |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------- |
| **Audio Engine** | Multi-track audio/folder support, automation lanes (vol/pan), 24-bit/32-bit float support, and VST3/AU plugin hosting. |
| **Timeline**     | Studio One-style drag & drop with ghost previews, multi-file consecutive placement, and grid-snapping clip editing.    |
| **Piano Roll**   | Comprehensive MIDI editor with note quantization, snap-to-grid, and high-performance scrolling.                        |                             |
| **Mixer**        | Real-time level meters, detachable mixer window, and per-track fader/pan control with branded JUCE-rendered windowing. |
| **Browser**      | Waveform previews for local files, plugin category browsing, and a dedicated "Cloud" tab for remote projects.          |
| **Export**       | Professional audio mixdown to WAV/AIFF/FLAC/OGG with bounds selection, format presets, wildcard templates, waveform display with clip detection (Reaper-style), and configurable sample rates/channels. |

---

## Recent Updates

- **Audio Export:** Professional-grade mixdown export supporting WAV, AIFF, FLAC, and OGG formats with real-time waveform preview and clip detection.
- **UI Polish:** Fully themed export dialogs matching Aerion branding, with ASCII-art waveform placeholders and visual clip indicators.
- **Stability:** Eliminated render freezes through smart parameter debouncing and eliminated Edit copy creation for preview-only jobs.

---

## Repository layout

Only the items needed to build and ship the app are tracked. Everything else (IDE caches, AI tooling folders, build output, installers, scratch notes) is gitignored so the repo root stays minimal.

```
Aerion-DAW/
  README.md            This file (the only loose markdown at the root)
  LICENSE              GPLv3
  .gitignore
  .github/             CI workflows and community docs (Code of Conduct)
  AerionDawCpp/        CMake project, C++ sources, assets, bundled docs
    CMakeLists.txt
    CMakePresets.json  Windows CMake presets (optional)
    CMake/             CPack helper scripts
    Documentation/     Roadmap, status, release notes, manual test checklist
    Resources/         SVG assets, fonts, icons
    External/          Third-party SDKs (e.g. Steinberg ASIO on Windows)
    Source/            Application code (Main, AudioEngine, UI, Export, …)
```

Anything else you see locally (e.g. `build/`, `dist/`, `_CPack_Packages/`, `.cursor/`, `.claude/`, `Cursor-AI/`, `Gemini/`, `.vs/`, etc.) is **gitignored** — see `.gitignore` for the full list.

---

## System Requirements


| Requirement  | Minimum                     | Recommended                    |
| ------------ | --------------------------- | ------------------------------ |
| **OS**       | Windows 10 (64-bit)         | Windows 11 (64-bit)            |
| **CPU**      | Intel Core i5 / AMD Ryzen 5 | Intel Core i7 / AMD Ryzen 7    |
| **RAM**      | 4 GB                        | 16 GB                          |
| **Graphics** | OpenGL 3.2 compatible       | Dedicated GPU                  |
| **Audio**    | Windows Audio / ASIO4ALL    | Dedicated ASIO Audio Interface |


---

## Getting Started

### Prerequisites

- **CMake** 3.20+
- **Visual Studio 2022** (MSVC)
- **PowerShell 7** (optional; any shell works)

If your IDE only looks for `CMakePresets.json` at the workspace root, either open the **`AerionDawCpp`** folder as the CMake source tree or always pass **`-S AerionDawCpp`** as in the commands below (presets live beside `CMakeLists.txt`).

### Building (Windows / PowerShell)

Presets live next to the CMake project under `AerionDawCpp/`. From the **repository root**:

```powershell
# Configure + build (Debug)
cmake --preset win-msvc-debug -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug

# ---- Manual alternative ----
cmake -S AerionDawCpp -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target AerionDaw
```

The executable is written to:

`build\AerionDaw_artefacts\Debug\Aerion DAW.exe` (or `Release` when you build that configuration).

---

## Architecture

Aerion DAW follows a strict **Model-View-Controller (MVC)** separation:

- **Model:** `ProjectData` owns the project `juce::ValueTree`, which acts as the single source of truth for the project state.
- **Controller:** `AudioEngineManager` wraps the Tracktion `Edit` and manages the real-time audio graph and transport.
- **View:** Native JUCE components in `UIComponents.h` observe the `ValueTree` and repaint only when the underlying state changes.

See `AerionDawCpp/Documentation/` for the roadmap, status, release procedure, and manual test checklist.

---

## License

This project is licensed under the terms found in the `LICENSE` file.
