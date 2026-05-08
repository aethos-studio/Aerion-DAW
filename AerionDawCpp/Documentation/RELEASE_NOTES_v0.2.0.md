# Aerion DAW v0.2.0 — Pre-Alpha

**Aethos Studio Ltd.** · May 2026

This is a **pre-alpha** build: expect rough edges, missing polish, and no guarantee of project compatibility across future versions. Feedback is welcome.

---

## Highlights

### Recording and monitoring (M3)

- **ASIO on Windows** — Steinberg ASIO SDK integrated under GPLv3; choose ASIO alongside WASAPI and DirectSound in Audio Settings. Small ASIO-compatible mark on the splash screen; full trademark line in **Help → About Aerion DAW**.
- **WinRT MIDI (Windows)** — Modern MIDI stack for USB and Bluetooth LE controllers where the OS supports it.
- **Per-track input monitoring** — Inspector **MON** pill cycles **Auto** (smart monitoring while armed), **On** (always hear input through the chain), and **Off** (e.g. when using hardware monitoring).
- **Per-track MIDI routing** — Inspector **In** menu splits into **Audio Input** and **MIDI Controller** so you can pin a specific controller to a track or use all enabled devices.
- **Live recording waveform** — Audio takes show a growing waveform under the playhead while recording, not only after stop.
- **Transport buffer readout** — Block size, one-buffer time, and driver round-trip latency are visible so mis-sized buffers are easier to spot.
- **Reset Audio Settings** — In Audio Settings, one control wipes saved device XML and reapplies safe defaults if you get stuck on a bad driver or sample rate.
- **Metronome, count-in, punch, PDC** — Toolbar and engine wiring for count-in (off / 1 / 2 bars), punch in/out over the loop range, and plugin delay compensation.

### Mixing and arrangement (M2)

- **Submix folders** — Convert a folder to a real submix (volume + meter) or back to an organisational folder; Timeline and Mixer context menus; **S** badge and colour cues in the arranger; wider, outlined strips in the Console for submix folders.
- **Cascading mute and solo** — Folder mute/solo propagates through contained tracks.
- **K-14 reference ticks** — Optional master fader scale (context menu on the master strip); persisted per project.
- **Inserts, sends, snapshots** — Inspector INSERTS and SENDS; Quick Send to a new bus; mix snapshots from the mixer strip menu.
- **Phase, mono, HPF/LPF** — Inspector and mixer strip access.

### Editing (M1)

- **MIDI CC and pitch-bend lane** — Resizable lane above velocity; paint and edit with undo; choice of controller or custom CC; persisted per clip.
- **Velocity, quantize, comping** — Existing Piano Roll and clip workflows unchanged from prior pre-alpha, now framed as part of the v0.2.0 story.

### Export (M4, partial)

- **Master mixdown** — WAV, AIFF, FLAC, or OGG; selection / loop / full project bounds; tail; format presets; filename wildcards; waveform preview with clip detection.

### Platform and packaging

- **Windows** — NSIS installer ships **Aerion DAW.exe** plus the Visual C++ runtime DLLs needed on a clean machine (no separate redistributable install required for typical setups).
- **macOS** — Universal **arm64 + x86_64** `.app` inside a Drag-and-Drop **.dmg** (install by dragging into Applications). ASIO is not applicable on macOS; CoreAudio is used.

---

## Downloads

| Platform | File |
|----------|------|
| Windows x64 | `AerionDAW-0.2.0-Windows.exe` |
| macOS (Apple Silicon + Intel) | `AerionDAW-0.2.0-Darwin.dmg` |

After publishing, optionally add SHA-256 checksums for each file in this section for verification.

---

## System requirements

| | Windows | macOS |
|---|---------|--------|
| **OS** | Windows 10 or later (64-bit) | macOS 11 (Big Sur) or later |
| **CPU** | 64-bit x64 | Apple Silicon or Intel (universal binary) |
| **Audio** | ASIO-capable interface optional; WASAPI / DirectSound supported | CoreAudio |
| **Plugins** | VST3 | VST3 and AU |

---

## Known limitations

- **Unsigned builds** — Windows SmartScreen and macOS Gatekeeper may warn on first launch. On macOS, use **right-click → Open** the first time if needed.
- **No code signing or notarisation** yet — planned for a later milestone.
- **Per-track audio / MIDI / monitor choices** are kept in memory for the session; saving and reloading those routing choices in the project file is planned.
- **Stems, bounce/freeze, tempo map, auto-save, crash recovery, custom shortcuts** — not in this build; see the project roadmap.
- **AI features** — scaffolding only (mock transcription); no ONNX models shipped.

---

## Credits and third-party software

Aerion DAW is built with **[JUCE](https://juce.com)** and **[Tracktion Engine](https://github.com/Tracktion/tracktion_engine)** (v3.2).

**Windows:** **ASIO** is a trademark and software of **Steinberg Media Technologies GmbH**. The Steinberg ASIO SDK is used under its license; see **Help → About Aerion DAW** for the ASIO-compatible notice.

**Plugins:** **VST** is a trademark of Steinberg Media Technologies GmbH. This application hosts **VST3** (and **AU** on macOS) plug-ins.

---

## Full change log (summary)

<details>
<summary>Expand for a milestone-oriented list</summary>

- M1: Clip editing, MIDI piano roll, CC/pitch lane, velocity, quantize, comping, markers, snap UI, loop range.
- M2: Phase/mono, HPF/LPF, metering and optional K-14 on master, folder tracks and submix conversion, cascading mute/solo, sends and inserts in Inspector, plugin presets, mix snapshots, mixer detach.
- M3: Metronome, count-in, punch, PDC, multi-device wave input per track, buffer/latency display, live record waveform, driver stack (ASIO on Windows, WASAPI, DirectSound, WinRT MIDI; CoreAudio on macOS; ALSA/JACK on Linux builds), reset audio settings, per-track monitor mode, per-track MIDI input pin.
- M4 (partial): Master mixdown export dialog and render job.

</details>
