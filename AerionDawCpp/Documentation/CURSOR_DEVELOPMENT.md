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
      Views/                   ← one header per UI view (UIComponents.h includes them all)
      UI/                      ← theme, icons, cached layers, dialogs
      Tests/                   ← AerionTests smoke tests, AerionBench
    Patches/                   ← Aerion's patches to Tracktion Engine (applied at configure)
    CMake/                     ← ApplyPatches.cmake, packaging helpers
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

It reports the Timeline's full repaint, playhead move, scroll steps and clip drag, the
Mixer, toolbar and transport, and the Piano Roll with a dense clip (`--notes=2000 --cc=1000`,
`--pianoroll-height=600`; full repaint with no notes and with all notes selected), each
against a 60 Hz frame (16.7 ms). Timings belong to the Release profiling build
(`build-profiling`); see [`PERFORMANCE.md`](./PERFORMANCE.md).

- `--save-baseline=<file>` keeps a run's timings in a JSON file; a later run with
  `--compare=<file>` lists every timing against it and returns exit code 3 if any is more
  than 20 % slower (ignoring changes under 0.05 ms, or 0.5 % of an audio block). Works with
  `--audio` too. A baseline belongs to one machine, build type and set of arguments; the
  file records them and `--compare` warns when they differ. Keep baselines next to the build
  (`build-profiling\bench-baseline.json`), not in the repository; save one before a change,
  compare after it.

- `--verify` checks that partial repaints and scrolling give the same pixels as a full
  repaint, plus unsaved-changes tracking, fader drags (including a track whose automation
  drives its volume), track resizing, and accessibility (every painted control has a
  labelled stand-in; Up and Space work a fader and a mute button); exit code 2 on a
  failure. CI runs it at 100 % and at `--scale=1.25`.
- `--scale=<factor>` paints as a window at that display scale does (UI Size times Windows
  scaling); set `--width` / `--height` to the logical window size.
- `--verify` also checks partial repaints of the Piano Roll (keys and grid, CC and velocity
  lanes, right edge) with every note selected.
- `--snapshots=<dir>` renders the main components, the Piano Roll included, at 100 % and
  150 % to PNGs.

`--png=<path>` dumps a full repaint to disk. Two runs that should render identically — a
pure culling change, say — can then be compared byte-for-byte, which is the closest thing
to a visual regression test available without a display.

Caveat: `SmartThumbnail` loads waveforms asynchronously and the benchmark does not pump the
message loop, so clips render without waveforms and these numbers **exclude** waveform
rasterisation. Treat them as a floor on real paint cost.

### Headless audio benchmark

`AerionBench --audio [--audio-tracks=32] [--audio-blocks=3000]` plays a generated reference
project (tracks with a clip, EQ, compressor, send and volume automation, plus two reverb
buses) through Tracktion's playback graph, pulling blocks through Tracktion's hosted audio
interface instead of a sound card, so it runs on machines without audio hardware. It reports
the time per block as a share of the block's duration (128 and 256 samples at 48 kHz, one
thread and all CPUs, with and without pooled memory), heap allocations on the audio thread,
and how long a graph rebuild takes. Exit code 2 if the output is silent or the allocation
counter does not work. Run it from the Release build; targets and results are in the Audio
section of [`PERFORMANCE.md`](./PERFORMANCE.md#audio).

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

- **Engine patches:** configure applies each patch in `AerionDawCpp/Patches/tracktion/` to the engine tree it uses, including this local checkout, with `git apply`. So `git status` in `tracktion_engine-src/` shows them as local changes; that is expected. A patch already applied is skipped; one that no longer applies stops the configure with the `git apply` error. See `Patches/README.md` for how to change a patch.
- **Exception model:** MSVC builds use `/EHa` instead of `/EHsc` (set in `CMakeLists.txt`), which plugin crash protection needs. Changing it rebuilds everything.
- **Full builds:** if building everything fails in one of Tracktion's example programs, build only Aerion's targets: `cmake --build build --config Debug --target AerionDaw AerionTests --parallel`.

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

## Release packaging and signing

`release-package` (`.github/workflows/package-release.yml`, run from the Actions tab) builds the Windows NSIS installer and the macOS DMG and attaches them to a GitHub Release for the `tag_name` input. Installers are named after the tag without its `v` (`v0.5.0-alpha` → `AerionDAW-0.5.0-alpha-Windows.exe` and `AerionDAW-0.5.0-alpha-macOS.dmg`), passed to CMake as `AERION_PACKAGE_VERSION`; local builds use the `project()` version. The release stage (`AERION_RELEASE_STAGE`, default `Alpha`) appears in the About dialog and the installer title; change it in `CMakeLists.txt` when the stage changes.

Installed copies find new releases through **Help → Check for Updates** (and a quiet check after startup), so tag names matter: the update check reads each release's tag as a semantic version (`v0.5.1`, `v0.6.0-alpha.1`; three numbers, an optional `-` suffix) and ignores tags it cannot read, drafts, and releases without a `.exe` (Windows) or `.dmg` (macOS) asset. Builds with a release stage, or of a pre-release tag, are also offered pre-releases; final builds only final releases. A build knows its own version from `AERION_PACKAGE_VERSION` (`AERION_BUILD_VERSION` in the code), so a new release must have a higher tag than the one before. The download is checked against the size and SHA-256 digest GitHub lists for the asset. Logic and tests: `Source/Updates/UpdateChecker.cpp`, `Source/Tests/UpdateTests.cpp`.

Signing is optional; each part runs only when its repository secrets exist:

| Secrets | What they enable |
|---|---|
| `KSM_CONFIG` (secret) and `KEEPER_CODESIGN_RECORD` (variable) | Signs the app and installer with a certificate fetched from a Keeper vault record at run time, so GitHub never stores it. The record holds the `.pfx` as a file attachment (named `aerion-codesign.pfx`, or set the variable `KEEPER_CODESIGN_PFX`) and its password in the Password field. `KSM_CONFIG` is the base64 config of a Keeper Secrets Manager application with read access to that record. Takes precedence over the two secrets below; if Keeper is set up and the fetch fails, the job fails. The log names the certificate (subject, thumbprint, expiry) and where it came from. |
| `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` | Signs the app and installer when Keeper is not set up. A self-signed certificate from `Tools/New-AerionSelfSignedCert.ps1` works but still shows SmartScreen's "unknown publisher"; a paid OV/EV certificate clears it. |
| `APPLE_DEVELOPER_ID_P12_BASE64`, `APPLE_DEVELOPER_ID_P12_PASSWORD` | Signs the app (hardened runtime, `Packaging/macOS/AerionDaw.entitlements`) and the DMG with a Developer ID Application certificate. Needs a paid Apple Developer account. Without it the DMG is ad-hoc signed. |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | With the Developer ID secrets: notarizes the DMG with `notarytool` and staples the ticket, so Gatekeeper opens it without warnings. `APPLE_APP_PASSWORD` is an app-specific password for the Apple ID. |
| `RELEASE_TOKEN` | Used instead of `GITHUB_TOKEN` to create or update the release: a fine-grained token with Contents read and write on this repository. Only needed if `GITHUB_TOKEN` is refused (`HTTP 403: Resource not accessible by integration`). |

---

## See also

- [`ROADMAP.md`](./ROADMAP.md) — feature milestones
- [`STATUS.md`](./STATUS.md) — current milestone inventory
- [`../../README.md`](../../README.md) — public build instructions
