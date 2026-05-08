# Aerion DAW — manual test sequence (post-milestone)

Use this after a **Debug or Release build** of **Aerion DAW**. Goal: confirm stability and the big UX pass (**tooltips**, **panels**, **timeline / mixer / transport / inspector / browser**) without needing automated tests.

## A. Prerequisites

1. Build succeeds and `Aerion DAW.exe` runs on your machine.
2. Optional: backup or duplicate a small test project folder if you rely on live projects.
3. Plan **~45–75 minutes** for a full pass; **~15 minutes** for a tooltip-only skim.

## B. Smoke test (launch & layout)

| Step | Action | Expected |
|------|--------|----------|
| B1 | Launch the app cold | Opens without crash; main window appears. |
| B2 | Resize window wider/narrower, taller/shorter | Menubar, toolbar, timeline, mixer, inspector, browser reflow without overlapping trash. |
| B3 | Toggle **Inspector** (toolbar + side chevron if used) | Panel shows/hides; layout updates. |
| B4 | Toggle **Browser** | Same as B3. |
| B5 | Drag **mixer height** divider (resize strip above mixer) | Mixer height changes within limits; no stuck cursor. |

## C. Tooltip milestone (hover ~0.5 s; no button held)

Hover each area below and confirm a **non-empty popup** that matches the control (wording may differ slightly).

### C1 — Menu bar (top labels: File, Edit, Song, …)

- Hover each menu title.
- **Expected:** Each shows a short description of what that menu covers.

### C2 — Toolbar (tools, snap, browser/inspector, metronome, etc.)

- Hover **inspector / browser toggles**, **select / razor / comp** tools.
- Hover **snap**, **crossfade**, **metronome**, **count-in**, **punch**, **PDC** (where present).
- **Expected:** Every distinct control explains its purpose.

### C3 — Transport bar

- Hover **sample rate / buffer / CPU** block.
- Hover **rewind / forward / stop / play / record / loop**.
- Hover **tempo** and **time signature** text fields (not only the surrounding bar).
- **Expected:** Tips for telemetry, transport actions, and editable fields.

### C4 — Timeline

- **Header:** **+ Audio**, **+ MIDI**, **+ Folder**.
- **Ruler:** empty area, near **loop brackets** (if visible), **marker** flags (if any), with **punch placement** mode on/off (exercise menu/engine if you have punch flow).
- **Track header column:** **M / S / R / A**, **FX** badge, **folder chevron** (if folders exist).
- **Automation lane** (if expanded): **VOL/PAN** pill, **curve** area.
- **Clip area** with **Select** tool: body vs **trim** edges vs **fade** handles (audio clips).
- Repeat with **Razor** and **Comp** tools over clips (wording should match tool).
- **Footer:** **zoom** slider, **horizontal** scrollbar, **vertical** scrollbar.
- **Expected:** Tips everywhere above; scrollbar tips only when the pointer is on the scrollbar (child component).

### C5 — Inspector (with a track selected)

- **Arm / mute / solo**, **input routing**, **phase**, **mono** (if visible).
- **Fader** vertical area.
- **Quick filters:** **HPF / LPF** when section expanded; **section header** to collapse/expand.
- **Inserts:** **+** and each **plugin row**.
- **Expected:** Meaningful strings; rows mention open vs right-click remove where implemented.

### C6 — Browser

- **Plugins / Files / Cloud** tabs.
- **Rescan** (Plugins tab).
- **Hover a plug-in row** and a **file/folder row** (Files).
- **Expected:** Tabs + rows explain drag/drop or navigation.

### C7 — Mixer

- **Pop out / dock** header button.
- Per-strip: **M / S / MONO / FX / “i”**, **peak** area, **pan** knob zone, **fader**.
- **Expected:** Strip controls all get tips; detach button explains dock vs float.

### C8 — Window chrome helpers

- **Mixer height** resize strip.
- **Inspector / Browser** collapse chevrons.
- **Expected:** Describes resize or show/hide behavior.

## D. Timeline interaction (beyond tooltips)

| Step | Action | Expected |
|------|--------|----------|
| D1 | **+ Audio**: add track | New track appears; mixer updates. |
| D2 | Click ruler | Playhead jumps (respect snap if relevant). |
| D3 | **Select** tool: drag a clip | Clip moves or trim/fade behaves per cursor hotspot. |
| D4 | **Razor**: split a clip | Clean split at line position. |
| D5 | **M/S/R/A** on track header | State toggles and reflects in inspector/mixer where linked. |

## E. Mixer & transport integration

| Step | Action | Expected |
|------|--------|----------|
| E1 | Mixer **mute/solo** vs timeline header | Same track state consistent. |
| E2 | **Play/Stop** | Transport responds; UI state updates. |
| E3 | Edit **tempo** / **time sig** (click, type, Enter) | Engine follows; no crash on bad input (verify graceful handling). |

## F. Browser & import

| Step | Action | Expected |
|------|--------|----------|
| F1 | Files tab: navigate, select audio | Preview area updates / “loading” when applicable. |
| F2 | Double-click import (per your wiring) | Audio lands on timeline with selection rules you expect. |
| F3 | Plugins: drag a plugin toward timeline/mixer | Drop highlight or insert path works (or fails gracefully with clear state). |

## G. Performance / stress (optional)

| Step | Action | Expected |
|------|--------|----------|
| G1 | Many tracks + scroll timeline vertically | Smooth scroll; no runaway CPU in idle. |
| G2 | Zoom in/out heavily | Waveforms refresh; no permanent blank clips. |
| G3 | Rapid hover across toolbar buttons | Tooltips don’t stack or freeze the UI. |

## H. Pass / fail log (copy as you go)

Use a simple table while testing:

| Area | Pass? | Notes (build, resolution, crash log) |
|------|-------|----------------------------------------|
| Tooltips | | |
| Timeline | | |
| Mixer | | |
| Transport | | |
| Inspector | | |
| Browser | | |

## I. What “done” means for this milestone

- **Tooltips:** All major controls in sections C1–C8 show helpful text within ~0.5 s, with **no crash** during hover spam.
- **Shell:** Panels toggle, mixer resizes, app survives resize torture.
- **Core workflows:** Track add, ruler seek, clip edit basics, mute/solo, transport still work after the UX pass.
