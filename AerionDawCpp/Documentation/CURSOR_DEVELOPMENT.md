# Cursor development guide — Windows 11

This document is the human-readable companion to the Cursor project rules in `.cursor/rules/`. It captures environment setup, build workflows, and guardrails for developing Aerion DAW on **Windows 11** with **Visual Studio 2022** (MSVC).

> **Note:** A Fedora/Linux workflow existed on branch `cursor/fedora-cursor-dev-guidelines-b4a3` for a temporary Linux dev period. **Windows is the primary dev target again.** Linux/Flatpak support remains on the long-term roadmap (`ROADMAP.md` → Future USPs).

---

## Quick start

```powershell
# Prerequisites: Visual Studio 2022 (Desktop C++), CMake 3.20+, Git
# Open repo root in Cursor — CMake Tools picks up presets via CMakePresets.json

cmake --preset win-msvc-debug -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug

# Run
& "build\AerionDaw_artefacts\Debug\Aerion DAW.exe"
```

Open the **repository root** in Cursor so `.cursor/rules/` loads automatically.

---

## Cursor rules (guardrails)

| Rule file | When it applies | Purpose |
|-----------|-----------------|---------|
| `00-aerion-project.mdc` | Always | MVC architecture, repo layout, milestone context, change discipline |
| `10-cpp-and-ui.mdc` | When editing `AerionDawCpp/Source/**` | C++20, realtime safety, Theme tokens, plugin paths |
| `20-windows-dev.mdc` | Always | Windows build commands, audio stack, IDE/CMake setup |

Rules are version-controlled. Personal Cursor settings (chats, local indexes) stay gitignored under `.cursor/` except `rules/`.

To invoke a rule manually in chat: `@20-windows-dev` (filename without `.mdc`).

---

## Repository layout

```text
Aerion-DAW/                    ← open this folder in Cursor
  .cursor/rules/               ← agent guardrails (tracked)
  .vscode/                     ← CMake Tools defaults (tracked)
  CMakePresets.json            ← includes AerionDawCpp/CMakePresets.json
  README.md
  AerionDawCpp/                ← CMake project root (-S AerionDawCpp)
    CMakeLists.txt
    CMakePresets.json
    Documentation/             ← this file, ROADMAP, STATUS
    Source/                    ← application C++
    Resources/
    External/                  ← ASIO SDK (Windows; GPLv3)
  build/                       ← local only (gitignored)
```

**Important:** `CMakeLists.txt` and presets live under `AerionDawCpp/`. From the repo root, pass `-S AerionDawCpp` (or use root `CMakePresets.json` / `.vscode/settings.json`).

---

## Build presets (Windows)

| Preset | Configuration | Purpose |
|--------|---------------|---------|
| `win-msvc-debug` | Debug | Daily development |
| `win-msvc-release` | Release | Performance / shipping builds |
| `win-msvc-debug-tests` | Debug + `AERION_BUILD_TESTS=ON` | Smoke tests via `ctest` |
| `win-msvc-profiling` | Release + `AERION_ENABLE_PROFILING=ON` | Paint / hot-path measurement |

```powershell
# Debug (daily development)
cmake --preset win-msvc-debug -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug

# Release
cmake --build build --preset win-msvc-release

# Tests
cmake --preset win-msvc-debug-tests -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug-tests
ctest --test-dir build -C Debug --output-on-failure
```

All Windows presets use **Visual Studio 17 2022**, **x64**, and write output under `build/`
(`win-msvc-profiling` uses `build-profiling/` so it never clobbers your daily build tree).

---

## Measuring paint performance (Milestone 5)

Performance work starts with numbers, not guesses. `AERION_ENABLE_PROFILING` compiles in
the `AERION_PROFILE_*` probes from `Source/UI/Profiling.h`; with the option off every probe
expands to nothing, so profiled call sites are free in shipping builds.

Always profile a **Release** build — Debug paint timings are meaningless.

```powershell
cmake --preset win-msvc-profiling -S AerionDawCpp -B build-profiling
cmake --build build-profiling --preset win-msvc-profiling
& '.\build-profiling\AerionDaw_artefacts\Release\Aerion DAW.exe'
```

Every 5 seconds the app writes a table to the JUCE log listing, per zone, the call rate,
average and worst-case milliseconds, milliseconds spent per wall-clock second, and how many
items each pass iterated over.

The counters matter as much as the timings. `Timeline.rowsVisited` versus `Timeline.rowsDrawn`
shows how many track rows the paint loop walked against how many it actually painted, and
`Timeline.clipsVisited` does the same for clips. The gap between visited and drawn is the
work viewport culling removes.

To profile a new hot path, add one line and rebuild:

```cpp
void MyComponent::paint (juce::Graphics& g)
{
    AERION_PROFILE_SCOPE ("MyComponent::paint");
    AERION_PROFILE_COUNT ("MyComponent.items", items.size());
    ...
}
```

These probes are **message-thread only** — reporting formats strings and writes to the
logger, so never put them on the audio thread.

### Headless paint benchmark

`AerionBench` renders the real `Timeline` into an offscreen image, so paint cost is
measurable without a display. It is built by the test presets but deliberately **not**
registered with `ctest`: timings are machine-dependent, so it is a tool you run and read,
not a pass/fail gate.

```powershell
cmake --preset win-msvc-debug-tests -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug-tests
& '.\build\AerionBench_artefacts\Debug\AerionBench.exe' --tracks=32 --clips=20 --frames=100
```

It reports two scenarios: a `full repaint` (scroll, zoom, any edit) and a `playhead strip`
(the 16 px invalidation `MainComponent::timerCallback` issues 25 times a second during
playback), each against the 40 ms budget of a 25 Hz tick.

`--png=<path>` dumps a full repaint to disk. Two runs that should render identically — a
pure culling change, say — can then be compared byte-for-byte, which is the closest thing
to a visual regression test available without a display.

Caveat: `SmartThumbnail` loads waveforms asynchronously and the benchmark does not pump the
message loop, so clips render without waveforms and these numbers **exclude** waveform
rasterisation. Treat them as a floor on real paint cost.

### Manual configure (without presets)

```powershell
cmake -S AerionDawCpp -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target AerionDaw
```

---

## Visual Studio 2022 checklist

Install via **Visual Studio Installer**:

- Workload: **Desktop development with C++**
- Individual components (if not included):
  - MSVC v143 (or latest) x64/x86 build tools
  - Windows 10/11 SDK
  - CMake tools for Windows (optional; standalone CMake also works)

Verify from a **Developer PowerShell for VS 2022** or any shell where `cmake` is on PATH:

```powershell
cmake --version
cmake --preset win-msvc-debug -S AerionDawCpp -B build
```

---

## IDE integration (Cursor / VS Code)

Tracked settings in `.vscode/settings.json`:

- `cmake.sourceDirectory` → `AerionDawCpp`
- `cmake.buildDirectory` → `build`
- Default configure/build preset → `win-msvc-debug`

After cloning or changing presets: **Developer: Reload Window**, then run **CMake: Configure** from the command palette.

Recommended extensions (`.vscode/extensions.json`): **CMake Tools**, **C/C++**.

---

## Audio on Windows

| Backend | Status |
|---------|--------|
| **ASIO** | Bundled SDK under `External/ASIO-SDK_*`; auto-enabled in CMake |
| **WASAPI** | JUCE default |
| **DirectSound** | JUCE default |
| **WinRT MIDI** | Enabled in `CMakeLists.txt` for Windows 10+ |

Use **Audio Settings** in the app to pick ASIO or WASAPI. Reset Audio Settings is available if a bad device state is saved.

---

## First configure notes

- **Tracktion Engine** must be pre-cloned with **HTTPS submodules** (FetchContent alone fails on Windows because Tracktion's JUCE submodule uses SSH). Run once per machine:

```powershell
git clone --depth 1 --branch v3.2.0 https://github.com/Tracktion/tracktion_engine.git tracktion_engine-src
Push-Location tracktion_engine-src
git submodule set-url modules/juce https://github.com/juce-framework/JUCE.git
git submodule update --init --recursive --depth 1
Pop-Location

$te = (Resolve-Path tracktion_engine-src).Path
cmake --preset win-msvc-debug -S AerionDawCpp -B build "-DFETCHCONTENT_SOURCE_DIR_TRACKTION_ENGINE=$te"
cmake --build build --preset win-msvc-debug
```

  `tracktion_engine-src/` is gitignored (local cache, same pattern as CI).

- **ASIO SDK** is already in-tree — no download step.
- **Tests** are off by default; use `win-msvc-debug-tests` or `-DAERION_BUILD_TESTS=ON`.
- **Plugin scan** on first launch runs in the background after the main window appears.

---

## Migrating back from Fedora / Linux

If you previously used the Fedora dev branch:

| Fedora | Windows |
|--------|---------|
| `fedora-ninja-debug` | `win-msvc-debug` |
| `cmake --build build --preset fedora-ninja-debug` | `cmake --build build --preset win-msvc-debug` |
| `./build/AerionDaw_artefacts/Debug/Aerion DAW` | `build\AerionDaw_artefacts\Debug\Aerion DAW.exe` |
| `dnf install …` | Visual Studio Installer |
| `.cursor/rules/20-fedora-linux-dev.mdc` | `.cursor/rules/20-windows-dev.mdc` |

Delete any stale `build/` directory from a Linux configure before running Windows presets (different generator/artefacts).

---

## CI parity

GitHub Actions (`.github/workflows/build-test.yml`) uses:

```text
cmake -S AerionDawCpp -B build -G "Visual Studio 17 2022" -A x64 -DAERION_BUILD_TESTS=ON
cmake --build build --config Release --target AerionDaw
cmake --build build --config Release --target AerionTests
ctest --test-dir build -C Release --output-on-failure
```

Local Debug presets are fine for day-to-day work; run Release + tests before opening a PR.

---

## See also

- [`ROADMAP.md`](./ROADMAP.md) — feature milestones
- [`STATUS.md`](./STATUS.md) — current milestone inventory
- [`../../README.md`](../../README.md) — public build instructions
