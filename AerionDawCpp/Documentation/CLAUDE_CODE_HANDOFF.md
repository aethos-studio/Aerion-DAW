# Claude Code handoff — Aerion DAW Milestone 5 performance work

**Written:** 2026-09-20  
**Purpose:** Seamless pickup after Cursor Cloud Agent session. Read this first, then `STATUS.md`, `ROADMAP.md`, and `CURSOR_DEVELOPMENT.md`.

---

## User intent

The user wants **Reaper-class UI performance** while staying on the existing roadmap (**Milestone 5: Polish & Stability → v0.4.0**). Performance is not a detour — it is explicitly item #1 in M5 (“timeline/piano-roll paint profiling”).

**Platform priority:** **Windows 11 / MSVC x64 is primary.** macOS is secondary (CI/release). **Linux is not a product target** — do not add Linux presets/CI unless asked. A throwaway Linux sandbox was used only to compile-check changes.

**User cannot merge PRs or trigger CI** and asked the agent to “just do it.” Agent merged #10/#11 **locally into the perf branch** so work could continue without waiting for GitHub merges. Those PRs are still **open drafts on GitHub**; only the perf branch contains them until PR #13 merges.

---

## Git state (as of handoff)

| Ref | Commit | Notes |
|---|---|---|
| `main` | `9a15f3f` | Last merged PR: **#9** (detached piano roll crash). **No perf work on main.** |
| `cursor/timeline-paint-profiling-3add` | `1cc8761` | **All work lives here.** Pushed to `origin`. |
| Open draft PR | [#13](https://github.com/aethos-studio/Aerion-DAW/pull/13) | Title: *Milestone 5: paint measurement, AudioEngine tests, and Timeline row culling* |

### Commits on perf branch (not on `main`)

```
1cc8761 Merge PR #11: prevent freeze-time edits from discarding track data
8ac4550 Merge PR #10: fix use-after-free after clip deletion with piano roll/selection
3bde9b0 Cull off-screen track rows in Timeline::paint
368cc79 Add headless Timeline paint benchmark
5d5aadd Add AudioEngineManager smoke tests
b33132b Add opt-in paint/hot-path profiling probes
(+ commits from merged PR #10/#11 branches)
```

### Uncommitted local work (NOT pushed at handoff)

**File:** `AerionDawCpp/Source/UIComponents.h`  
**Change:** Wrap grid drawing in `AERION_PROFILE_SCOPE("Timeline.grid")` and the row loop in `AERION_PROFILE_SCOPE("Timeline.rows")` to split the ~10.9 ms empty-timeline cost.  
**Action for pickup:** Review, run benchmark with profiling ON, commit if useful, push.

---

## Open PRs (GitHub)

| # | Branch | State | Relationship to perf work |
|---|---|---|---|
| **13** | `cursor/timeline-paint-profiling-3add` | Draft OPEN | **Primary deliverable** — includes merged #10+#11 |
| 11 | `cursor/critical-bug-investigation-6f4f` | Draft OPEN | **Already merged into #13 branch** — can close after #13 merges |
| 10 | `cursor/critical-bug-investigation-98a2` | Draft OPEN | **Already merged into #13 branch** — can close after #13 merges |
| 12 | `cursor/setup-linux-dev-environment-3ff6` | Draft OPEN | Unrelated — user said Linux is side for now |

**User merge path (simplest):** Mark PR #13 **Ready for review** → **Merge**. That lands stability fixes + perf work in one shot. Optionally run CI first (see below).

---

## What was built on the perf branch

### 1. Profiling probes (`Source/UI/Profiling.h`)

- `AERION_PROFILE_SCOPE("name")` — wall-clock per scope  
- `AERION_PROFILE_COUNT("name", n)` — items touched  
- CMake: `-DAERION_ENABLE_PROFILING=ON` (default OFF; zero cost when off)  
- Preset: `win-msvc-profiling` → Release build in `build-profiling/`  
- Reporter ticks from existing 25 Hz `MainComponent::timerCallback`  

**Instrumented:** `Timeline::paint`, `PianoRollEditor::paint`, `Mixer::paint`, `Transport::paint`, `ProjectData::syncWithEngine`, `MainComponent::editStateChanged`.

### 2. AudioEngine smoke tests (`Source/Tests/AudioEngineTests.cpp`)

13 headless cases. **Not** testing freeze guards (PR #11 territory — behaviour may still evolve).

**Finding pinned in tests:** Folder mute “cascade” is **not** Tracktion mute-by-destination. `toggleTrackMute` sets **each child’s own mute flag**:

```cpp
// AudioEngine.cpp ~1067
for (auto* child : f->getAllAudioSubTracks (true))
    child->setMute (newState);
```

**Product note:** Unmuting a folder clears every child’s mute flag — per-child mute state does not survive folder mute/unmute.

### 3. Headless benchmark (`Source/Tests/PaintBenchmark.cpp`, target `AerionBench`)

Renders real `Timeline` offscreen. **Not** registered with `ctest` (machine-dependent timings).

```powershell
cmake --preset win-msvc-debug-tests -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug-tests
& '.\build\AerionBench_artefacts\Debug\AerionBench.exe' --tracks=32 --clips=20 --frames=100
& '.\build\AerionBench_artefacts\Debug\AerionBench.exe' --png=C:\temp\timeline.png'  # visual regression
```

**Caveat:** `SmartThumbnail` loads async; benchmark does not pump the message loop → **clips render without waveforms**. Numbers are a **floor**; real sessions with waveforms cost more.

### 4. Timeline row culling (`UIComponents.h`, `drawTrackRow`)

**Problem:** During playback, `MainComponent::timerCallback` repaints only a **16 px playhead strip**, but `Timeline::paint` walked **every track and every clip** — no use of `g.getClipBounds()`.

**Fix:** Early-out rows outside clip bounds; still cache `trackButtonCache` **before** skip (hit-testing depends on it); still recurse folder children when parent row is off-screen.

**Measured (Release, 32 tracks × 20 clips, 1920×1080, sandbox Linux — re-verify on Windows):**

| Scenario | Before | After |
|---|---|---|
| Playhead strip 16px | 1.12 ms | **0.56 ms** |
| Full repaint | 23.4 ms | 22.8 ms |
| Clips visited / paint | 640 | 260 |

**Correctness:** Full-repaint PNG **byte-identical** before/after culling.

---

## Performance audit — remaining hotspots (priority order)

These were identified by code audit + benchmark; not all fixed yet.

### P0 — Full repaint ~23 ms (57% of 25 Hz tick)

- **`MainComponent::editStateChanged()`** calls `syncWithEngine()` + **full repaints** of browser, mixer, timeline, transport on **every** `broadcastChange()` (~40+ sites + catch-all `EditListener`).
- **Empty 32-track timeline** still costs **~10.9 ms** full repaint (0 clips) — ruler, grid, headers dominate. No `setBufferedToImage` anywhere in codebase.
- **`Theme::uiSize()`** allocates a new `juce::Font` on every call — hot in ruler/grid.

**Next wins:** Cache static chrome (ruler/grid/header column) to image; narrow `editStateChanged` to dirty regions; split playhead into overlay component; cache fonts.

### P1 — Meter polling in `paint()`

- `Mixer::paint` → `getTrackPeak()` per strip at 25 Hz — string map keys, plugin list scan (~825 calls/s @ 32 tracks).
- **Fix:** Pre-register meter clients; snapshot peaks once per timer tick; per-strip meter subcomponents.

### P2 — Piano roll

- 128-row loops every frame; CC lane full sort every paint; `getSelectedNotes()` called 4× per paint.

### P3 — Code structure blocker

- **`UIComponents.h` is ~9,044 lines**, 18 classes inline. Any paint edit rebuilds 3 heavy TUs. **Split `Timeline`, `PianoRollEditor`, `Mixer` into `.cpp` files** before large refactors (now safer after #10/#11 merged on branch).

### Audio engine (not UI)

- Tracktion default threading — no `setNumberOfThreads` in repo. Real-time path delegated to Tracktion/JUCE; no custom `processBlock` in app.

---

## Key files

| File | Role |
|---|---|
| `Source/UIComponents.h` | **Monolith** — Timeline (~3500 lines), PianoRoll, Mixer, Transport |
| `Source/MainComponent.cpp` | 25 Hz timer, playhead partial repaint, `editStateChanged` |
| `Source/AudioEngine.cpp` | Engine, mute cascade, freeze guards (PR #11) |
| `Source/ProjectData.cpp` | `syncWithEngine()` — called from every edit |
| `Source/UI/Profiling.h` | Opt-in probes |
| `Source/Tests/PaintBenchmark.cpp` | AerionBench |
| `Source/Tests/AudioEngineTests.cpp` | Engine smoke tests |
| `CMakeLists.txt` | `AERION_ENABLE_PROFILING`, `AerionBench`, test sources |
| `CMakePresets.json` | `win-msvc-profiling` preset |

---

## Build & test (Windows — primary)

From **repo root**, **PowerShell**:

```powershell
# Daily debug
cmake --preset win-msvc-debug -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug

# Smoke tests (includes AudioEngine tests)
cmake --preset win-msvc-debug-tests -S AerionDawCpp -B build
cmake --build build --preset win-msvc-debug-tests
ctest --test-dir build -C Debug --output-on-failure

# Profiling build (Release, separate tree)
cmake --preset win-msvc-profiling -S AerionDawCpp -B build-profiling
cmake --build build-profiling --preset win-msvc-profiling
& '.\build-profiling\AerionDaw_artefacts\Release\Aerion DAW.exe'

# Benchmark
& '.\build\AerionBench_artefacts\Debug\AerionBench.exe' --tracks=32 --clips=20 --frames=100
```

Output binary: `build\AerionDaw_artefacts\Debug\Aerion DAW.exe`

---

## CI (manual — user or you with GitHub access)

Workflow: `.github/workflows/build-test.yml` — **`workflow_dispatch` only**.

1. https://github.com/aethos-studio/Aerion-DAW/actions/workflows/build-test.yml  
2. **Run workflow** → branch `cursor/timeline-paint-profiling-3add`  
3. Green on Windows + macOS before merging PR #13  

```powershell
gh workflow run build-test.yml --ref cursor/timeline-paint-profiling-3add
gh run watch
```

Cloud agents **cannot** merge PRs or trigger workflows reliably (read-only integration).

---

## Architecture guardrails (do not break)

- **MVC:** `ProjectData` ValueTree = model; `AudioEngineManager` = controller; UI observes ValueTree.
- **No audio-thread alloc/lock** in app code.
- **Windows-first** shell commands (PowerShell, `win-msvc-*` presets).
- **Minimize diff scope** — no drive-by refactors.
- **`trackButtonCache`:** Must be populated even for culled rows — `mouseDown` hit-tests M/S/R/A from cache populated during paint.
- **Folder row culling:** Must still recurse into `folder->getAllAudioSubTracks(false)` when parent row is off-screen.

---

## Suggested next steps (ordered)

1. **Commit/push** uncommitted `Timeline.grid` / `Timeline.rows` profiling scopes if benchmark confirms usefulness.
2. **Run `build-test` on Windows** for branch `cursor/timeline-paint-profiling-3add`.
3. **User merges PR #13** (or you if you have merge rights).
4. **Split `UIComponents.h`** — extract Timeline/PianoRoll/Mixer to `.cpp` (compile-time + safety for next edits).
5. **Static chrome cache** — `setBufferedToImage` or manual `juce::Image` for ruler + header column; invalidate on zoom/scroll/layout only.
6. **Narrow `editStateChanged`** — typed change notifications; stop full-tree repaints on parameter tweaks.
7. **Meter snapshot layer** — decouple from `paint()`.
8. Update `STATUS.md` M5 inventory when milestones shift (profiling + AudioEngine tests + partial perf done).

---

## Known noise (ignore)

- Debug teardown: JUCE assertions in `tracktion::Edit::~Edit` → `Selectable::notifyListenersOfDeletion` (Tracktion v3.2). Documented in `AudioEngineTests.cpp`. Does not fail tests.

---

## Branch naming (Cloud Agent rules)

If creating new branches: `cursor/<descriptive-name>-3add` (lowercase, suffix `-3add`).

---

## Contact / context

- Repo: `aethos-studio/Aerion-DAW`  
- App version: **0.3.0** (CMake), targeting **0.4.0** for M5  
- Engine: JUCE 8 + Tracktion Engine v3.2  
- User transitioning from Cursor to **Claude Code** — prefers Windows dev, wants actionable perf work with measured outcomes.
