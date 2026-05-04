<div align="center">
  <img src="AerionDawCpp/Resources/aerion_logo_horizontal.svg" alt="Aerion DAW" width="650" />


  A Digital Audio Workstation built with C++20, JUCE 8, and the Tracktion Engine.
</div>

---

## Project Overview

Aerion DAW is a native C++ application targeting zero-latency real-time audio,
a lock-free threading model, and modern MIDI 2.0 / MPE workflows. The codebase
follows a strict Model-View-Controller layout: state lives in
`juce::ValueTree` structures, the audio graph is owned by a Tracktion `Edit`,
and the UI is composed of native JUCE components.

### Key Technologies
- **Framework:** [JUCE 8](https://juce.com/)
- **Audio Engine:** [Tracktion Engine](https://www.tracktion.com/develop/tracktion-engine) (v3.2)
- **Build System:** CMake 3.20+
- **Language:** C++20
- **Cloud Sync:** Google Drive (OAuth 2.0 + PKCE)
- **Typography:** Cinzel (Google Fonts, embedded as BinaryData)

## System Requirements

| Requirement | Minimum | Recommended |
| :--- | :--- | :--- |
| **OS** | Windows 10 (64-bit) | Windows 11 (64-bit) |
| **CPU** | Intel Core i5 / AMD Ryzen 5 | Intel Core i7 / AMD Ryzen 7 |
| **RAM** | 4 GB | 8 GB or 16 GB |
| **Graphics** | OpenGL 3.2 compatible | Dedicated GPU / High-res display |
| **Audio** | Windows Audio / ASIO4ALL | Dedicated Audio Interface (ASIO) |
| **Storage** | 200 MB (Installation) | 1 GB+ SSD (Projects & Caching) |

## Repository Layout

```
AerionDawCpp/
  CMakeLists.txt
  Resources/
    aerion_logo.svg            App icon / taskbar icon source (SVG)
    aerion_logo_vertical.svg   Splash screen logo
    aerion_fader.svg           Custom fader knob graphic
    Cinzel-Regular.ttf         Embedded Cinzel typeface (Google Fonts)
    Aerion_Ico.ico             Windows installer icon
  Source/
    Main.cpp                  JUCE application entry point + SplashWindow
    SplashWindow.h            Animated fog splash screen (Cinzel typeface)
    MainComponent.{h,cpp}     Top-level window layout, collapsible panels
    UIComponents.h            DAWMenuBar, DAWToolbar, Inspector, Browser,
                              Timeline (D&D + ghost preview), Mixer, Transport
    ProjectData.{h,cpp}       ValueTree-backed project model
    AudioEngine.{h,cpp}       Tracktion Engine + transport wrapper
    GoogleDriveClient.{h,cpp} OAuth/PKCE login, Drive multipart upload, file listing
    AIManager.{h,cpp}         Audio-to-MIDI transcription scaffolding (ONNX-bound)
modules/
  juce/                       JUCE submodule (also fetched via FetchContent)
```

## Getting Started

### Prerequisites
- **CMake** 3.20 or higher
- A C++20-capable compiler (MSVC 2022, Clang, or GCC)
- **Git** — Tracktion Engine and JUCE are pulled in via `FetchContent`

### Building

```bash
cd AerionDawCpp
cmake -B build
cmake --build build --config Debug --target AerionDaw
```

The resulting executable is written to:
```
AerionDawCpp/build/AerionDaw_artefacts/Debug/Aerion DAW.exe
```

> The first configure pulls Tracktion Engine + JUCE (~several minutes). Subsequent builds are incremental.

## Features

| Area | Details |
| :--- | :--- |
| **Timeline** | Multi-track audio editing, clip move/trim, waveform thumbnails, automation lanes, track reorder drag |
| **Drag & Drop** | Studio One-style: ghost clip preview snaps to grid while hovering; drops land at exact position on the target track (or create a new one); consecutive multi-file placement |
| **Plugin Drag** | Drag from the Browser Plugins tab and drop onto any track header or mixer strip to instantly insert a plugin |
| **Mixer** | Per-track faders, panning, mute/solo, real-time peak meters, detachable window |
| **Browser** | Files tab with waveform preview + OS drag; Plugins tab with VST3 scanning; Google Drive Cloud tab |
| **Piano Roll** | Full MIDI note editor — add, move, resize, delete, snap to grid, scrollable |
| **Collapsible Panels** | Inspector and Browser panels collapse/expand via a 14 px chevron toggle strip |
| **Transport** | Play/Stop/Record, tempo, time signature, bars/beats/ticks display |
| **Project Files** | Save/Load `.aerion` projects; title bar shows project name |
| **Splash Screen** | Animated fog entrance with Cinzel typeface; minimum 4-second display |
| **UI Theme** | Celtic Metal dark theme (`MetalLookAndFeel`) throughout, including plugin windows |

## Architecture

Strict **Model-View-Controller** separation:

- **Model** — `ProjectData` owns the project `juce::ValueTree`. All UI state
  (tracks, regions, mixer levels, sends) is queried and mutated through the
  tree, giving thread-safe, undoable, observable state out of the box.
- **View** — JUCE `Component`s in `UIComponents.h`. The top-level
  `MainComponent` lays them out via bounds logic; each panel paints itself
  from the `ValueTree` it observes.
- **Controller** — `AudioEngineManager` wraps the Tracktion `Edit` and
  transport. UI controls (e.g. `Transport`'s play/stop buttons) call directly
  into it.

## License

This project is licensed under the terms found in the `LICENSE` file.
