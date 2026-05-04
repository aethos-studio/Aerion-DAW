# CLAUDE.md — Aerion DAW

Context file for AI-assisted development. Read this before writing any code.

---

## Project Identity

- **Product:** Aerion DAW — a native desktop DAW by Aethos Studio Ltd.
- **Version:** v0.1.0 (pre-alpha)
- **Platform:** Windows 11 primary; macOS secondary (AU support exists in CMake)
- **Build system:** CMake 3.20+, MSVC 2022 / VS 2026 Build Tools
- **Shell:** PowerShell 7 (all commands assume PowerShell syntax)
- **Standard:** C++20

---

## Technology Stack

| Layer | Technology |
|---|---|
| Audio engine | Tracktion Engine v3.2 (fetched via CMake FetchContent) |
| UI / framework | JUCE 8 (bundled inside Tracktion Engine) |
| AI inference | ONNX Runtime v1.19 (stubbed; not yet linked) |
| Cloud | Google Drive REST API v3 via `juce::URL` |
| Packaging | NSIS (Windows), DMG (macOS, future) |

Tracktion Engine includes JUCE. **Do not add JUCE as a separate FetchContent dependency.**

---

## Repository Layout

```
AerionDaw/
├── CMakeLists.txt
├── CLAUDE.md               ← you are here
├── GEMINI.md
├── ROADMAP.md
├── STATUS.md
├── Resources/
│   ├── aerion_logo.svg
│   ├── aerion_logo_vertical.svg
│   ├── aerion_fader.svg
│   ├── Cinzel-Regular.ttf
│   └── Aerion_Ico.ico
└── Source/
    ├── Main.cpp
    ├── MainComponent.h / .cpp
    ├── AudioEngine.h / .cpp
    ├── UIComponents.h          ← ALL UI classes live here (one big header)
    ├── ProjectData.h / .cpp
    ├── AIManager.h / .cpp
    ├── GoogleDriveClient.h / .cpp
    ├── LogoComponent.h
    └── SplashWindow.h
```

### File roles

**`AudioEngine.h/.cpp`** — `AudioEngineManager`. The single source of truth for all Tracktion Engine interaction: tracks, transport, plugins, meters, volume/pan, undo, save/load. Never call Tracktion APIs directly from UI code — always go through `AudioEngineManager`.

**`UIComponents.h`** — Every UI class: `MetalLookAndFeel`, `DAWMenuBar`, `DAWToolbar`, `Inspector`, `Browser`, `Timeline`, `Mixer`, `Transport`, plus all helpers (`paintFader`, `setFaderFromY`, `Theme` namespace). This is intentionally one large header; do not split it without discussion.

**`ProjectData.h/.cpp`** — `ProjectData`. Holds a `juce::ValueTree` shadow of the engine state. UI components read from this tree; `syncWithEngine()` keeps it up to date. The `IDs` namespace declares all `juce::Identifier` constants here.

**`MainComponent.h/.cpp`** — `MainComponent`. Owns all sub-components and wires their callbacks together. Layout logic lives in `resized()`. Does not draw anything itself except `bgBase` fill.

**`AIManager.h/.cpp`** — `AIManager`. Background thread scaffold for AI tasks. Currently mocked. Lives off the message thread; uses `juce::MessageManager::callAsync` to post results back.

**`GoogleDriveClient.h/.cpp`** — OAuth 2.0 + PKCE flow, token persistence, Drive file listing/upload/download.

**`SplashWindow.h`** — Standalone animated splash. No dependencies on `AudioEngineManager`. Uses `BinaryData` directly.

**`LogoComponent.h`** — Trivial SVG renderer. Inline header-only.

---

## Architecture Rules

### Threading model
- **Message thread** — all JUCE UI painting, event handling, and component callbacks. Never block it.
- **Audio thread** — Tracktion Engine audio graph. Never allocate, lock a mutex, or call JUCE non-realtime-safe APIs here.
- **Background threads** — `juce::Thread` subclasses (`AIManager`, `GoogleDriveClient`, plugin scanner). Post results back to the message thread exclusively via `juce::MessageManager::callAsync`.

### State flow
```
Tracktion Edit (ground truth)
        ↓  syncWithEngine() / valueTreePropertyChanged()
  ProjectData (ValueTree shadow)
        ↓  repaint() / property callbacks
    UI Components (read-only consumers)
```
UI writes go the other way: UI → `AudioEngineManager` method → Tracktion Edit → `valueTreePropertyChanged` fires → ProjectData updates → UI repaints.

### Listener pattern
`AudioEngineManager` exposes a `Listener` interface (`editStateChanged()`). Components that need to react to broad engine changes add themselves as listeners. For fine-grained property changes, components listen to the `ProjectData` ValueTree directly via `juce::ValueTree::Listener`.

### Callbacks in MainComponent
Inter-component communication goes through `std::function` callbacks wired in `MainComponent`'s constructor. Examples: `browser.onFilePicked`, `timeline.onImportFiles`, `mixer.onPluginDroppedOnStrip`. Never give sub-components direct pointers to sibling components.

---

## JUCE / Tracktion Conventions

### Namespace alias
Always alias at the top of `.cpp` files:
```cpp
namespace te = tracktion;
```
Never write `tracktion::` inline throughout a `.cpp` — use the alias.

### Tracktion IDs
Tracktion uses its own `IDs` namespace (e.g., `tracktion::IDs::TRACK`, `tracktion::IDs::volume`). The project has its own `IDs` namespace in `ProjectData.h` for our shadow tree. Do not confuse the two. When syncing, translate between them explicitly.

### Fader / volume formula
Tracktion stores fader position as a linear value where `db = 20 * ln(pos) + 6`. The inverse is `pos = exp((db - 6) / 20)`. Helper wrappers live on `AudioEngineManager`:
```cpp
AudioEngineManager::getFaderPosFromDb(float db)   // db → 0..1 linear display position
AudioEngineManager::getDbFromFaderPos(float pos)   // 0..1 → dB
setTrackVolumeDb(track, db)
getTrackVolumeDb(track)
```
**Never** use `log10` or quartic approximations. **Never** pass a `0..1` normalised value to `setParameter` — Tracktion wants the actual value.

### Pan
Range is `-1.0 .. 1.0`. Stored on `VolumeAndPanPlugin::panParam`. Use:
```cpp
audioEngine.setTrackPan(track, pan);
audioEngine.getTrackPan(track);
```

### SmartThumbnail
Waveform thumbnails are owned by `AudioEngineManager::thumbnails` (a `std::map` keyed on `itemID.getRawID()`). Retrieve via `getThumbnailForClip(clip, componentToRepaint)`. Never construct a `SmartThumbnail` directly in UI code.

### Plugin windows
Plugin windows are created by `AerionUIBehaviour::createPluginWindow` in `AudioEngine.cpp`. To open one call `plugin->showWindowExplicitly()`. Never construct `AerionPluginWindow` directly.

### Track filtering
`te::getTopLevelTracks(*edit)` returns ALL Tracktion meta-tracks (Tempo, Chord, Marker, Arranger, Master). Always filter to only `AudioTrack*` and `FolderTrack*` when building UI lists:
```cpp
audioEngine.getTopLevelTracks()   // already filtered
```

### insertWaveClip signature
```cpp
track->insertWaveClip(name, file,
    { { te::TimePosition::fromSeconds(start),
        te::TimeDuration::fromSeconds(length) },
      te::TimeDuration::fromSeconds(0.0) },
    false);
```
The nested brace initialiser is a `ClipPosition`. The trailing `false` means "don't delete existing clips".

---

## Theme & Visual Language

### Colour tokens (always use `Theme::` constants, never hardcode hex)

| Token | Hex | Usage |
|---|---|---|
| `bgBase` | `#080a0e` | Window / outermost background |
| `bgPanel` | `#11141a` | Panel backgrounds |
| `surface` | `#1c212b` | Controls, clip bodies |
| `border` | `#2d3748` | Dividers, outlines |
| `accent` | `#63b3ed` | Arctic Blue — secondary highlights |
| `active` | `#3182ce` | Ice Blue — primary interactive state |
| `textMain` | `#f0f4f8` | Primary text |
| `textMuted` | `#8a99a8` | Labels, secondary text |
| `meterGreen` | `#48bb78` | Meter fill (low) |
| `meterYellow` | `#ecc94b` | Meter fill (mid) |
| `meterRed` | `#f56565` | Meter fill (high) |
| `recordRed` | `#e53e3e` | Record arm / clip indicator |

Track accent colours cycle via `Theme::colourForTrack(int idx)`.

### Rounding radius
- Panels / backgrounds: `4.0f`
- Meters, small controls: `2.0f`
- Fader track groove: `3.0f`

### Font conventions
- UI labels: `juce::Font(11.0f)`
- Track names: `juce::Font(11.0f).withStyle(juce::Font::bold)`
- Counter / numeric displays: `juce::Font(20.0f).withStyle(juce::Font::bold)`
- Subscript labels below counters: `juce::Font(8.0f).withStyle(juce::Font::bold)`
- The **Cinzel** typeface is available via `BinaryData::CinzelRegular_ttf` — use it only for branding text (splash, logo areas).

### Panels and borders
Use `Theme::drawRoundedPanel(g, bounds, colour, alpha)` for any bordered panel surface. Do not duplicate the fill+stroke pattern inline.

---

## Build Instructions (PowerShell)

### First-time configure
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

### Build (Debug)
```powershell
cmake --build build --config Debug
```

### Build (Release)
```powershell
cmake --build build --config Release
```

### Run
```powershell
.\build\AerionDaw_artefacts\Debug\AerionDaw.exe
```

### Clean rebuild
```powershell
Remove-Item -Recurse -Force build; cmake -S . -B build -G "Visual Studio 17 2022" -A x64; cmake --build build --config Debug
```

### Package (Windows NSIS — Release only)
```powershell
cmake --build build --config Release
cd build
cpack -C Release
```

---

## Adding New Source Files

1. Create the `.h` / `.cpp` in `Source/`.
2. Add both to `target_sources(AerionDaw PRIVATE ...)` in `CMakeLists.txt`.
3. `#include "YourFile.h"` where needed.
4. Do **not** add files to `AerionDawResources` — that target is for binary assets only.

## Adding New Binary Resources

1. Drop the file in `Resources/`.
2. Add its path to `juce_add_binary_data(AerionDawResources SOURCES ...)` in `CMakeLists.txt`.
3. Access it as `BinaryData::filename_ext` and `BinaryData::filename_extSize`.

---

## Key Gotchas

1. **`createSingleTrackEdit` adds one audio track automatically.** `setupInitialEdit()` deletes it immediately to start with an empty session. Don't skip this or there will be a phantom track.

2. **`te::getTopLevelTracks` includes meta-tracks.** Never iterate it raw for UI — use `audioEngine.getTopLevelTracks()` which filters them.

3. **LevelMeterPlugin is lazy-created per track.** `getTrackPeak()` will create and subscribe the plugin the first time it's called. This is intentional.

4. **SmartThumbnail must outlive the clip.** The `thumbnails` map in `AudioEngineManager` owns them. Do not store raw references outside the engine.

5. **FileChooser must be heap-allocated and stored** (`std::unique_ptr<juce::FileChooser> fileChooser` on `MainComponent`). Letting it go out of scope crashes on Windows.

6. **`juce::MessageManager::callAsync` captures by value for short-lived data.** Always capture thread-local data by value, not reference, in async lambdas.

7. **`valueTreePropertyChanged` fires on the message thread.** Tracktion volume values arriving here are native fader position values — convert with the `20*ln(pos)+6` formula before storing as dB in ProjectData.

8. **Plugin scanning must call `fm.addDefaultFormats()` if the format count is 0.** The format manager starts empty; the scanner is a no-op without formats.

9. **`VolumeAndPanPlugin` may not exist on all tracks.** Always null-check the result of `getVolumePlugin()` before accessing it.

10. **Mixer detach/reattach.** The `Mixer` component is re-parented between `MainComponent` and `MixerWindow`. Always call `clearContentComponent()` on the window before resetting `mixerWindow` to avoid a dangling `setContentNonOwned` reference.

---

## What Is NOT Done Yet (do not assume these exist)

- Real ONNX Runtime linkage (`AIManager` is fully mocked)
- Clip trim / split / fade handles
- Send/return bus routing (data exists in `ProjectData` mock but is not wired to Tracktion)
- Loop region UI on the ruler
- Markers
- MIDI velocity / CC lanes in Piano Roll
- Bounce / freeze / export
- Crash recovery / auto-save
- Full project round-trip serialisation (save writes Edit XML; load reads it — but plugin state, referenced audio paths, and automation are not yet fully tested)

Refer to `ROADMAP.md` for the full prioritised list.
