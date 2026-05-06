# Aerion DAW Architecture

Aerion DAW is a JUCE GUI application built on Tracktion Engine.

## High-level modules

- **App shell**: `AerionDawCpp/Source/Main.cpp`
  - Starts the splash window and then the main window.
- **Composition root**: `AerionDawCpp/Source/MainComponent.{h,cpp}`
  - Owns the core services (`AudioEngineManager`, `ProjectData`, `GoogleDriveClient`, `AIManager`) and the primary UI components.
- **Engine wrapper**: `AerionDawCpp/Source/AudioEngine.{h,cpp}`
  - Owns `tracktion::Engine` + the active `tracktion::Edit`.
  - Exposes transport, editing operations, plugin scanning/hosting, metering, and persistence helpers.
- **Project model**: `AerionDawCpp/Source/ProjectData.{h,cpp}`
  - Owns a `juce::ValueTree` for UI state and mirrors the Tracktion edit with `syncWithEngine(...)`.
- **UI**: `AerionDawCpp/Source/UIComponents.h`
  - Contains `Timeline`, `Mixer`, `Inspector`, `Browser`, `Transport`, `DAWToolbar`, `DAWMenuBar`, etc.

## Data flow (current)

- **Engine → UI**:
  - UI queries `AudioEngineManager` for live values (transport position, meters, plugin lists).
  - `MainComponent::editStateChanged()` triggers `ProjectData::syncWithEngine(edit)` to keep UI state in sync.
- **UI → Engine**:
  - UI calls `AudioEngineManager` methods to perform edits (import clips, add tracks, toggle mute/solo, add plugins, etc).

## Real-time safety rules (non-negotiable)

- Treat Tracktion/JUCE audio callbacks as **real-time**:
  - **No allocations**, **no locks**, **no logging**, **no file/network I/O** on any audio-thread path.
- Heavy work must be off the message thread:
  - Plugin scanning, Drive networking, audio analysis, AI inference must be asynchronous.
  - UI updates should return to the message thread via `juce::MessageManager::callAsync`.

## “Studio One feel” principles

- Prefer **drag-and-drop first** workflows (Browser → Timeline/Mixer).
- Prefer **hover-zone + modifiers** “smart tool” editing over mode-heavy tools.
- Every gesture needs an **Undo** story via Tracktion edit operations/UndoManager.

## Planned refactor direction

`UIComponents.h` is currently a single large compilation unit for speed of iteration.
As features grow, migrate to:

- `AerionDawCpp/Source/UI/Timeline.{h,cpp}`
- `AerionDawCpp/Source/UI/Mixer.{h,cpp}`
- `AerionDawCpp/Source/UI/Inspector.{h,cpp}`
- etc.

This improves compile times and keeps changes reviewable.

## JUCE indexing (Cursor)

This repo fetches Tracktion Engine (which vendors JUCE) during CMake configure.
After you configure once, JUCE sources will exist inside your build tree under CMake’s FetchContent deps.

Recommended workflow:

- Configure with presets (generates `build/`):
  - `cmake --preset win-msvc-debug`
- Then add the fetched JUCE source folder to the Cursor workspace and force indexing.
  - Look for a `build/_deps/...` folder containing JUCE modules (vendored via Tracktion Engine).