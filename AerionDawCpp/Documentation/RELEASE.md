# Aerion DAW — Release Procedure

This document explains how to cut a tagged release of **Aerion DAW** and ship installer files for **Windows (NSIS .exe)** and **macOS (.dmg)**.

The version number is sourced from a single line in `AerionDawCpp/CMakeLists.txt`:

```cmake
project(AerionDaw VERSION 0.2.0)
```

CPack reads `${PROJECT_VERSION}` from there, so bumping the version is a one-line change followed by a tag push.

---

## 1. The fast path — tagged release via CI

This produces both installers in a single GitHub Actions run and attaches them to a draft GitHub Release.

```bash
# 1. Bump the version in CMakeLists.txt (project(AerionDaw VERSION 0.2.0))
# 2. Update Documentation/STATUS.md / ROADMAP.md if needed
# 3. Commit the bump
git add AerionDawCpp/CMakeLists.txt AerionDawCpp/Documentation
git commit -m "Release v0.2.0"

# 4. Tag and push
git tag v0.2.0
git push origin main --tags
```

The push of the tag triggers `.github/workflows/package-release.yml`, which has two jobs:

| Job | Runner | Output |
| --- | --- | --- |
| `windows` | `windows-latest` | `dist/AerionDAW-0.2.0-Windows.exe` (NSIS) |
| `macos`   | `macos-latest`   | `dist/AerionDAW-0.2.0-Darwin.dmg` (DragNDrop, Apple-Silicon + Intel universal) |

Both artefacts are uploaded as workflow artefacts **and** attached to a **draft GitHub Release** named `Aerion DAW v0.2.0`. Review the draft, edit the auto-generated notes, then publish it.

> **Manual trigger:** the workflow also has `workflow_dispatch`, so you can fire a one-off non-tagged build from the Actions tab for smoke-testing without cutting a real release.

---

## 2. Fallback — building installers locally

### Windows (NSIS)

Requirements: Visual Studio 2022 Build Tools, CMake 3.20+, NSIS (`winget install -e --id NSIS.NSIS`). The Steinberg ASIO SDK is bundled in-tree under `External/ASIO-SDK_*` so no extra download is needed.

```powershell
cmake -S AerionDawCpp -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --target AerionDaw

# NSIS lives in C:\Program Files (x86)\NSIS by default; CPack needs it on PATH.
$env:PATH = "$env:PATH;C:\Program Files (x86)\NSIS"
Push-Location build
cpack -G NSIS -C Release -B ..\dist
Pop-Location
```

The installer lands at `dist/AerionDAW-<version>-Windows.exe`. Typical size is ~6 MB (Aerion DAW.exe ≈ 13 MB compressed + 1.5 MB of VC++ runtime DLLs; the build is monolithic, so users only get one EXE plus the redistributable runtime).

### macOS (DragNDrop)

Requirements: Xcode command-line tools, CMake 3.20+, Ninja (`brew install ninja`).

```bash
cmake -S AerionDawCpp -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DAERION_ENABLE_ASIO=NEVER

cmake --build build --target AerionDaw

cpack --config build/CPackConfig.cmake -G DragNDrop -C Release -B dist
```

The DMG lands at `dist/AerionDAW-<version>-Darwin.dmg`. The DMG ships an unsigned `Aerion DAW.app` plus an "Applications" symlink. **First-time users will see Gatekeeper warnings** until we wire up Apple code-signing + notarisation (see Section 5).

---

## 3. What ends up inside the installers

The CPack pre-build script (`AerionDawCpp/CMake/CPackPruneStaging.cmake`) strips out the JUCE / Tracktion module headers, `juceaide.exe`, CMake export packages and other build-tooling that the FetchContent sub-projects install by default. The final payloads are:

| Platform | Files | Approx size |
| --- | --- | --- |
| Windows | `Aerion DAW.exe` + 8 VC++ runtime DLLs (`msvcp140`, `vcruntime140`, `concrt140`, etc.) | ~6 MB compressed |
| macOS   | `Aerion DAW.app` (universal arm64 + x86_64 bundle) | ~25 MB DMG |

If you ever need to confirm the payload, look at the staging directory CPack creates before it builds the installer:

```
dist/_CPack_Packages/win64/NSIS/AerionDAW-<ver>-Windows/
dist/_CPack_Packages/Darwin/DragNDrop/AerionDAW-<ver>-Darwin/
```

---

## 4. Knobs you might need to flip

Set these as `cmake -D <name>=<value>` flags:

- `AERION_ENABLE_ASIO=AUTO|NEVER|DEBUG_ONLY` — see the comment in `CMakeLists.txt`. AUTO (default) ships ASIO in Debug + Release on Windows. NEVER builds a clean SDK-free binary.
- `CMAKE_BUILD_TYPE=Release|Debug|RelWithDebInfo` — Ninja / Makefile builds. Visual Studio reads `--config <cfg>`.
- `CMAKE_OSX_ARCHITECTURES` — pass `"arm64;x86_64"` for a universal binary, or just `arm64` for an Apple-Silicon-only build.

---

## 5. Code-signing and notarisation (TODO)

Pre-Alpha installers are **unsigned**. Future releases should add:

- **Windows:** `signtool sign /a /tr http://timestamp.digicert.com /td sha256 /fd sha256 "Aerion DAW.exe"` after the build but before `cpack` so the bundled exe is signed; then `signtool sign` the resulting installer too.
- **macOS:** `codesign --deep --options=runtime --sign "Developer ID Application: Aethos Studio Ltd."` on `Aerion DAW.app`, then `xcrun notarytool submit --wait` on the DMG, then `xcrun stapler staple` to staple the notarisation ticket.

Both can be done in the GitHub Actions workflow once the certificates and Apple ID secrets are configured.

---

## 6. Verifying a release

Before publishing the GitHub Release draft:

1. Download both artefacts from the workflow run.
2. **Windows:** install on a clean Win10/11 box without Visual Studio. The 8 redist DLLs we ship cover users who never installed the VC++ Redistributable. ASIO should appear in **Settings → Audio** alongside WASAPI / DirectSound.
3. **macOS:** mount the DMG, drag to Applications, launch. Expect a one-time "unverified developer" warning (right-click → Open) until we sign + notarise.
4. Confirm the splash, About box, and a basic record/play loop work.
5. Publish the draft, copy the GitHub Release URL into your release announcement.
