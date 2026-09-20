#Requires -Version 5.1
<#
.SYNOPSIS
  Clone (or update) Aerion DAW on this machine and restore the local-only agent handoff doc.

.DESCRIPTION
  The handoff file is gitignored and never pushed to GitHub. Run this once after cloning
  on a new Windows dev PC, or anytime you want to refresh the local handoff notes.

.PARAMETER ClonePath
  Where to put the repository. Defaults to Simon's Windows dev path.

.PARAMETER RepoUrl
  Git remote to clone from.

.EXAMPLE
  .\AerionDawCpp\Tools\Setup-WindowsDevClone.ps1

.EXAMPLE
  .\Setup-WindowsDevClone.ps1 -ClonePath "D:\Dev\Aerion-DAW"
#>
[CmdletBinding()]
param(
    [string] $ClonePath = 'C:\Users\SimonWukits\Documents\Aethos Studio\Aerion DAW',
    [string] $RepoUrl   = 'https://github.com/aethos-studio/Aerion-DAW.git'
)

$ErrorActionPreference = 'Stop'

function Ensure-Git
{
    if (-not (Get-Command git -ErrorAction SilentlyContinue))
    {
        throw 'Git is not on PATH. Install Git for Windows and retry.'
    }
}

Ensure-Git

$parent = Split-Path -Parent $ClonePath
if ($parent -and -not (Test-Path $parent))
{
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}

if (Test-Path (Join-Path $ClonePath '.git'))
{
    Write-Host "Updating existing clone at:`n  $ClonePath"
    Push-Location $ClonePath
    try
    {
        git fetch origin
        git checkout main
        git pull origin main
    }
    finally
    {
        Pop-Location
    }
}
elseif (Test-Path $ClonePath)
{
    throw "Path exists but is not a git repo:`n  $ClonePath`nRemove or rename it, then rerun."
}
else
{
    Write-Host "Cloning into:`n  $ClonePath"
    git clone $RepoUrl $ClonePath
}

$handoffPath = Join-Path $ClonePath 'AerionDawCpp\Documentation\CLAUDE_CODE_HANDOFF.md'
$handoffDir  = Split-Path -Parent $handoffPath
if (-not (Test-Path $handoffDir))
{
    New-Item -ItemType Directory -Path $handoffDir -Force | Out-Null
}

$handoff = @'
# Claude Code handoff — Aerion DAW Milestone 5 performance work

**Updated:** 2026-09-20  
**Local only** — this file is gitignored. It is not on GitHub.  
**Read next:** `STATUS.md`, `ROADMAP.md`, `CURSOR_DEVELOPMENT.md`

---

## User intent

**Reaper-class UI performance** on the existing roadmap (**Milestone 5 → v0.4.0**). Performance is M5 item #1 (timeline/piano-roll profiling).

**Platform:** Windows 11 / MSVC x64 is primary. macOS is secondary (CI/release). Linux is not a product target.

**Local clone path:** `C:\Users\SimonWukits\Documents\Aethos Studio\Aerion DAW`

---

## Git state (September 2025)

All performance groundwork and lifetime fixes are on **`main`** (merged directly, not via open PRs).

| Item | Status |
|---|---|
| Profiling probes + `win-msvc-profiling` preset | On `main` |
| `AudioEngineTests` (13 smoke cases) | On `main` |
| `AerionBench` headless Timeline benchmark | On `main` |
| Timeline row culling (playback playhead path) | On `main` |
| PR #10 clip/piano-roll UAF fix | Merged into `main` |
| PR #11 freeze-time edit guards | Merged into `main` |
| README rewrite | On `main` |
| Studio One references removed | On `main` |
| This handoff doc | **Local only** (gitignored) |

Refresh `main` before starting work:

```powershell
cd "C:\Users\SimonWukits\Documents\Aethos Studio\Aerion DAW"
git pull origin main
```

---

## What was built (on main)

### Profiling (`Source/UI/Profiling.h`)

- `AERION_PROFILE_SCOPE` / `AERION_PROFILE_COUNT` — opt-in via `-DAERION_ENABLE_PROFILING=ON`
- Preset: `win-msvc-profiling` → Release in `build-profiling/`
- Sub-zones: `Timeline.grid`, `Timeline.rows` (committed)

### Tests (`Source/Tests/AudioEngineTests.cpp`)

13 headless `AudioEngineManager` cases. Freeze guard behaviour intentionally not locked down in tests.

**Folder mute note:** `toggleTrackMute` sets each child’s own mute flag (not Tracktion mute-by-destination). Unmuting a folder clears all children’s mute flags.

### Benchmark (`Source/Tests/PaintBenchmark.cpp`, target `AerionBench`)

Not a ctest gate — machine-dependent timings.

```powershell
cmake --preset win-msvc-debug-tests -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug-tests
& '.\build\AerionBench_artefacts\Debug\AerionBench.exe' --tracks=32 --clips=20 --frames=100
```

Waveforms may be absent in benchmark output (async thumbnails; no message-loop pump).

### Timeline row culling

Skips off-screen rows in `drawTrackRow`; still caches `trackButtonCache` before early-out; still recurses folder children.

**Measured (Release, 32×20 clips, 1920×1080 — re-verify on Windows):**

| Scenario | Before | After |
|---|---|---|
| Playhead strip 16px | ~1.12 ms | ~0.56 ms |
| Full repaint | ~23 ms | ~23 ms |
| Clips visited / paint | 640 | 260 |

---

## Remaining hotspots (priority order)

### P0 — Full repaint ~23 ms (~57% of 25 Hz tick)

- `editStateChanged()` → `syncWithEngine()` + full repaints of browser/mixer/timeline/transport on every `broadcastChange()`
- Empty 32-track timeline still ~10.9 ms (ruler/grid/headers) — no static chrome cache
- `Theme::uiSize()` allocates fonts every call

**Next:** Cache static chrome; narrow `editStateChanged`; playhead overlay component; font cache.

### P1 — Meters polled from `paint()`

`getTrackPeak()` per strip at 25 Hz. Pre-register clients; snapshot once per timer tick.

### P2 — Piano roll

128-row loops; CC lane sort every paint; redundant `getSelectedNotes()` calls.

### P3 — `UIComponents.h` monolith (~9k lines)

Split Timeline / PianoRoll / Mixer into `.cpp` before large refactors.

---

## Build (Windows / PowerShell)

From repo root:

```powershell
cmake --preset win-msvc-debug -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug

cmake --preset win-msvc-debug-tests -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug-tests
ctest --test-dir build -C Debug --output-on-failure

cmake --preset win-msvc-profiling -S AerionDawCpp -B build-profiling
cmake --build build-profiling --preset win-msvc-profiling
```

Binary: `build\AerionDaw_artefacts\Debug\Aerion DAW.exe`

---

## CI (manual)

https://github.com/aethos-studio/Aerion-DAW/actions/workflows/build-test.yml → **Run workflow** → branch `main`

```powershell
gh workflow run build-test.yml --ref main
gh run watch
```

---

## Guardrails

- MVC: `ProjectData` ValueTree = model; `AudioEngineManager` = controller
- No audio-thread alloc/lock in app code
- Windows-first: PowerShell, `win-msvc-*` presets
- `trackButtonCache` must be set even for culled rows (M/S/R/A hit-testing)
- Folder culling must still recurse into subtracks when parent row is off-screen
- Do not reference competitor product names in user-facing docs

---

## Suggested next steps

1. Run `build-test` on `main` from Windows
2. Run `AerionBench` on Windows; confirm row culling numbers
3. Split `UIComponents.h`
4. Static chrome cache for ruler/header column
5. Narrow `editStateChanged` repaint fan-out
6. Meter snapshot layer outside `paint()`
7. Update `STATUS.md` M5 inventory when items complete

---

## Known noise

Debug teardown may log JUCE assertions inside `tracktion::Edit::~Edit` (Tracktion v3.2). Documented in `AudioEngineTests.cpp`. Does not fail tests.

'@

# UTF-8 without BOM reads cleanly in most editors; with BOM for Windows Notepad compatibility.
$utf8Bom = New-Object System.Text.UTF8Encoding($true)
[System.IO.File]::WriteAllText($handoffPath, $handoff, $utf8Bom)

Write-Host ""
Write-Host "Done."
Write-Host "  Repo:     $ClonePath"
Write-Host "  Handoff:  $handoffPath"
Write-Host ""
Write-Host "Open the repo root in Cursor/VS Code, then read the handoff doc before continuing M5 perf work."
