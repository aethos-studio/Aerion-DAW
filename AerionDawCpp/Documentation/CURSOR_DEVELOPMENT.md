# Cursor development guide — Fedora 44

This document is the human-readable companion to the Cursor project rules in `.cursor/rules/`. It captures environment setup, build workflows, and guardrails for developing Aerion DAW after migrating from **Windows 11** to **Fedora 44**.

---

## Quick start

```bash
# One-time: install toolchain + JUCE Linux dependencies (see below)
sudo dnf install @development-tools cmake ninja-build gcc-c++ \
  alsa-lib-devel jack-audio-connection-kit-devel \
  libX11-devel libXext-devel libXinerama-devel libXrandr-devel libXcursor-devel \
  libXcomposite-devel libXdamage-devel libXrender-devel \
  mesa-libGL-devel freetype-devel fontconfig-devel libcurl-devel \
  webkit2gtk4.1-devel pkgconf-pkg-config

# Clone (adjust path as needed)
mkdir -p ~/Aethos-Studio
git clone https://github.com/aethos-studio/aerion-daw.git ~/Aethos-Studio/"Aerion DAW"
cd ~/Aethos-Studio/"Aerion DAW"

# Configure + build
cmake --preset fedora-ninja-debug -S AerionDawCpp -B build
cmake --build build --preset fedora-ninja-debug

# Run
./build/AerionDaw_artefacts/Debug/"Aerion DAW"
```

Open the **repository root** in Cursor so `.cursor/rules/` loads automatically.

---

## Cursor rules (guardrails)

| Rule file | When it applies | Purpose |
|-----------|-----------------|---------|
| `00-aerion-project.mdc` | Always | MVC architecture, repo layout, milestone context, change discipline |
| `10-cpp-and-ui.mdc` | When editing `AerionDawCpp/Source/**` | C++20, realtime safety, Theme tokens, plugin paths |
| `20-fedora-linux-dev.mdc` | Always | Fedora build commands, audio stack, Windows migration pitfalls |

Rules are version-controlled. Personal Cursor settings (chats, local indexes) stay gitignored under `.cursor/` except `rules/`.

To invoke a rule manually in chat: `@00-aerion-project` (filename without `.mdc`).

---

## Repository layout

```text
Aerion DAW/                    ← open this folder in Cursor
  .cursor/rules/               ← agent guardrails (tracked)
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

**Important:** CMake presets and `CMakeLists.txt` live under `AerionDawCpp/`. From the repo root, always pass `-S AerionDawCpp`.

---

## Build presets

| Preset | Generator | Configuration |
|--------|-----------|---------------|
| `fedora-ninja-debug` | Ninja | Debug |
| `fedora-ninja-release` | Ninja | Release |
| `win-msvc-debug` | Visual Studio 2022 | Debug (CI / Windows) |
| `mac-clang-*` | Ninja Multi-Config | macOS |

```bash
# Debug (daily development)
cmake --preset fedora-ninja-debug -S AerionDawCpp -B build
cmake --build build --preset fedora-ninja-debug

# Release
cmake --preset fedora-ninja-release -S AerionDawCpp -B build
cmake --build build --preset fedora-ninja-release

# Smoke tests (Milestone 5)
cmake --preset fedora-ninja-debug -S AerionDawCpp -B build -DAERION_BUILD_TESTS=ON
cmake --build build --target AerionTests
ctest --test-dir build --output-on-failure
```

Executable path after build:

```text
build/AerionDaw_artefacts/Debug/Aerion DAW
build/AerionDaw_artefacts/Release/Aerion DAW
```

---

## Audio stack on Fedora 44

| Backend | Status | Notes |
|---------|--------|-------|
| **ALSA** | Enabled by default (JUCE) | Works with PipeWire's ALSA compatibility layer |
| **JACK** | Auto-enabled if dev headers installed | Preferred device order in app: JACK → ALSA |
| **PipeWire** | Session manager | Default on Fedora; no special CMake flag required |
| **VST3** | Hosting enabled | Scan paths: `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3` (JUCE defaults) |
| **ASIO / WASAPI / WinRT MIDI** | Windows only | Do not expect these on Linux builds |

Install JACK development headers for low-latency pro-audio:

```bash
sudo dnf install jack-audio-connection-kit-devel
```

---

## Migrating from Windows 11

### What changes

| Topic | Windows 11 | Fedora 44 |
|-------|------------|-----------|
| Shell / scripts | PowerShell | bash |
| Compiler | MSVC 2022 | GCC (default) or Clang |
| CMake generator | Visual Studio | Ninja |
| Debug binary | `Aerion DAW.exe` | `Aerion DAW` (no extension) |
| Audio | ASIO / WASAPI | ALSA / JACK via PipeWire |
| Plugin folders | `Program Files\Common Files\VST3` | `~/.vst3`, `/usr/lib/vst3` |
| IDE | Visual Studio | Cursor + CMake Tools (optional) |

### What stays the same

- C++20, JUCE 8, Tracktion Engine v3.2 (fetched by CMake)
- MVC architecture (`ProjectData` → `AudioEngineManager` → JUCE views)
- Project file format (`.aerion`)
- Git workflow and CI on GitHub (Windows build + smoke tests on PR)

### Common first-build issues

1. **Missing X11 / GL / Freetype headers** — install the `dnf` development packages listed above.
2. **WebKit GTK** — JUCE may require `webkit2gtk4.1-devel` on Fedora 44 for certain modules.
3. **Long first configure** — `FetchContent` clones Tracktion Engine (~submodules). Subsequent configures are fast.
4. **No sound** — check PipeWire is running (`wpctl status`); pick ALSA or JACK device in Aerion's audio settings.

---

## Architecture reminders (for humans and agents)

```
ProjectData (ValueTree)  ──observe──►  JUCE UI components
       │
       └──► AudioEngineManager ──► Tracktion Edit / realtime graph
```

- **Do not** store project state only in UI widgets.
- **Do not** block the audio thread.
- **Do** use `Theme::` for colours and scaled sizes.
- **Do** guard platform-specific code with `JUCE_WINDOWS`, `JUCE_MAC`, `JUCE_LINUX`.

---

## What agents should not do

- Commit `build/`, `.vs/`, personal notes, or API keys
- Add Windows-only path logic to shared Linux code paths
- Enable ONNX Runtime linking without an explicit Milestone 8 task
- Run large refactors across `UIComponents.h` without a focused goal
- Change ASIO trademark / splash / About attribution
- Assume the cloud agent VM path (`/home/simon/...`) — local path is `~/Aethos-Studio/Aerion DAW/`

---

## CI vs local

GitHub Actions **build-test** workflow runs on `windows-latest` with MSVC. Before opening a PR from Fedora:

1. Build locally with the Fedora preset
2. Run smoke tests if you touched `ProjectData` or `Keymap`
3. Ensure no Windows-only code leaked into shared paths without guards

Linux packaging (CPack) is not yet wired like Windows NSIS / macOS DMG — local Fedora builds produce the binary only.

---

## Related docs

- [`STATUS.md`](./STATUS.md) — milestone progress and build state
- [`ROADMAP.md`](./ROADMAP.md) — feature roadmap
- [`../../README.md`](../../README.md) — project overview (Windows-centric getting started; Linux details here)

---

*Last updated: June 2026 — Fedora 44 primary dev target.*
