# Contributing to Aerion DAW

## Non-negotiables

- Follow `.cursorrules`.
- Real-time safety: no allocations/locks/logging/I-O on audio-thread paths.
- Keep changes modular and reviewable. Avoid growing `UIComponents.h` further unless the change is tiny.

## Local build (Windows / MSVC)

Recommended (presets):

```powershell
cmake --preset win-msvc-debug
cmake --build --preset win-msvc-debug
```

Manual:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## Where to implement things

- **Editing / engine behavior**: `AerionDawCpp/Source/AudioEngine.`*
- **Project/UI state**: `AerionDawCpp/Source/ProjectData.`*
- **UI interactions**: `AerionDawCpp/Source/UIComponents.h` (migrate new work into `Source/UI/*.cpp` over time)

## Studio One alignment checklist (PR reviewer rubric)

- Drag-and-drop path exists (Browser → Timeline/Mixer) where applicable
- Smart-tool hover zones/modifiers are preferred over new global “modes”
- Undo/redo is correct for the gesture
- No UI jank: heavy work is async, and UI updates happen on the message thread

## Secrets & user data

- Never commit OAuth tokens or client secrets.
- Any per-user configuration belongs in user settings / `%AppData%` data, not the repo.

