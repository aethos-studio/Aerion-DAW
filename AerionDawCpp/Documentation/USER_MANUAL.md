# Aerion DAW User Manual v0.1.0

**Last Updated:** May 2026

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Project Basics](#project-basics)
3. [Timeline & Audio Editing](#timeline--audio-editing)
4. [Mixer](#mixer)
5. [Audio Export](#audio-export)
6. [MIDI Editing](#midi-editing)
7. [Plugins & Extensions](#plugins--extensions)
8. [Browser & Asset Management](#browser--asset-management)
9. [Keyboard Shortcuts](#keyboard-shortcuts)

---

## Getting Started

### Opening Aerion DAW

Launch `Aerion DAW.exe`. You'll see an animated splash screen with the Aerion logo, followed by the main window.

### The Main Interface

The Aerion DAW interface consists of:

- **Left Panel (Inspector):** Track controls, automation, and routing
- **Center (Timeline):** Audio clips, MIDI regions, and visual feedback
- **Right Panel (Browser):** File browser, plugin library, and cloud integration
- **Bottom (Mixer):** Track faders, pan controls, and level meters

All panels can be collapsed/expanded by clicking the chevron toggle strips on the sides.

---

## Project Basics

### Creating a New Project

1. Click **File** → **New**
2. Select a sample rate (44.1 kHz, 48 kHz, or 96 kHz recommended)
3. Set default track count and layout
4. Start recording or importing audio

### Opening a Project

1. Click **File** → **Open**
2. Browse to your `.aerion` project file
3. The project will load with all tracks, clips, and automation intact

### Saving Your Project

- **Save:** `Ctrl+S` saves to the current project file
- **Save As:** `Ctrl+Shift+S` creates a new project file with a custom name

The project window title shows `<ProjectName> — Aerion DAW` when saved.

---

## Timeline & Audio Editing

### Importing Audio

**Via Drag & Drop:**
1. Open the Browser panel (right side)
2. Click the **Files** tab
3. Browse to your audio folder
4. Drag a `.wav`, `.aiff`, or `.flac` file onto the Timeline
5. The audio will snap to the grid and land on the selected track

**Via File Menu:**
1. Click **File** → **Import Audio**
2. Select one or more audio files
3. Choose the target track and start position
4. Click **Import**

### Timeline Navigation

- **Scroll Horizontally:** Mouse wheel (forwards/backwards in time)
- **Scroll Vertically:** Right-side scroll bar (expand/collapse track view)
- **Zoom:** `Ctrl + Mouse Wheel` (zoom in/out on the timeline)
- **Playhead:** Click to position or drag to scrub

### Editing Audio Clips

**Select a Clip:**
- Click once on the clip to select it (blue highlight)

**Move a Clip:**
- Drag the clip body left/right to a new time position
- Clips snap to the beat grid (toggle grid snap via the Transport bar)

**Resize a Clip:**
- Drag the right edge of the clip to change its end time
- Drag the left edge to trim the start

**Delete a Clip:**
- Select the clip and press `Delete` or right-click → **Remove**

**Split a Clip:**
- Position the playhead at the desired split point
- Right-click the clip → **Split at Cursor**

### Volume & Panning

**Fader Control:**
- Click and drag the track fader in the Mixer to adjust volume
- Double-click to reset to 0 dB

**Pan Control:**
- Use the Pan slider in the Mixer to move audio left/right

**Automation:**
- Click the **Automation** button on a track to reveal automation lanes
- Volume and Pan curves are editable by dragging breakpoints
- Right-click an automation point to delete it

---

## Mixer

The Mixer panel at the bottom shows all tracks with:

- **Fader:** Master volume control
- **Pan Slider:** Stereo positioning
- **Level Meter:** Real-time peak level display (green → yellow → red = clipping)
- **Mute/Solo Buttons:** Control track audibility

### Detaching the Mixer

Click the **Detach** button (right side of the Mixer) to open a floating mixer window. This allows you to resize and position the mixer independently.

---

## Audio Export

### Opening the Export Dialog

1. Click **File** → **Export Mixdown**
2. The Export Mixdown dialog will appear

### Export Settings

**Render to File:**
- **Master Mix:** Current audio output (only option in v0.1)

**Bounds Mode:**
- **Selection/Loop/Full:** Export the current selection or loop range, or the entire project (default)
- **Loop Range:** Export only the looped section
- **Entire Project:** Export the full length from start to end

**Tail Duration:**
- Enable the **Tail** toggle to add silence at the end
- Set the tail duration in milliseconds (default: 1000 ms)

**Output Directory:**
- Click **Browse...** to select where to save the file
- Default: `C:\Users\[YourUsername]\Music\Aerion Projects\Renders`

**File Name:**
- Enter a custom name or use wildcards:
  - `$project` → Project name
  - `$date` → Current date (YYYY-MM-DD_HH-MM-SS)
  - `$bounds` → Time range (e.g., 0.000s-60.000s)
  - Example: `$project_$bounds_$date` → `MySong_0.000s-60.000s_2026-05-07_14-30-45`

**Format:**
- **WAV file** (default): Uncompressed, maximum compatibility
- **AIFF**: Mac-friendly uncompressed format
- **FLAC**: Lossless compression (smaller files, high quality)
- **OGG Vorbis** (if available): Lossy compression for web/streaming

**Sample Rate:**
- 44100 Hz (CD quality, default)
- 48000 Hz (video standard)
- 96000 Hz (high-resolution)

**Channels:**
- **Stereo** (default): 2-channel L/R
- **Mono:** Single channel (downmix)

### Waveform Preview

The preview area shows:
- **Before render:** ASCII placeholder text `~~~~ Waveform Preview ~~~~`
- **During/after render:** Live waveform display
- **Red overlay:** Indicates clipping (peaks exceeding ~0 dB, Reaper-style)

### Rendering

1. Configure the above settings
2. Click **Render 1 file**
3. The progress bar shows real-time render progress
4. Once complete, a success dialog appears with the file path
5. The rendered file is ready to use

### Canceling Export

Click **Cancel** at any time to stop the current render and close the dialog.

---

## MIDI Editing

### Opening the Piano Roll

Double-click any MIDI clip on the Timeline to open the Piano Roll Editor.

### Piano Roll Interface

- **Left Side:** 88-key piano keyboard (white/black keys)
- **Top:** Beat ruler with grid markings
- **Main Area:** 128-note grid for MIDI note editing
- **Bottom:** Snap length selector (quarter note, eighth note, sixteenth note)

### Adding Notes

1. Click and drag in the grid to create a new note
2. The note snaps to the grid length (default: quarter note)
3. Release to confirm

### Moving Notes

- Click and drag a note to a new time/pitch
- Notes snap to the grid

### Resizing Notes

- Drag the right edge of a note to change its duration
- Drag the left edge to trim the start

### Deleting Notes

- Right-click a note → **Delete**
- Or select and press `Delete`

### Quantization

- Use the snap length dropdown to enforce beat-grid quantization
- All new notes will snap to the selected grid size

---

## Plugins & Extensions

### Adding a Plugin to a Track

**Via Browser:**
1. Open the Browser panel (right side)
2. Click the **Plugins** tab
3. Browse by category or search by name
4. Drag a plugin onto a track in the Timeline or Mixer

**Plugin Window:**
- The plugin's graphical interface opens in a floating window
- Adjustments are real-time during playback
- Close the window to hide the plugin interface (the plugin remains on the track)

### Removing a Plugin

1. Click the plugin window's close button, or
2. Right-click the track in the Inspector and select **Remove Plugin**

---

## Browser & Asset Management

### Files Tab

Browse local audio files:
1. Navigate folders in the left panel
2. Click a file to preview its waveform (80 px strip)
3. Drag to import into the Timeline

### Plugins Tab

Browse installed VST3/AU plugins:
1. Browse by category or search by name
2. Drag to add to a track

### Cloud Tab (Google Drive)

Sync with Google Drive:
1. Click **Connect** to authorize Aerion with your Google account
2. Your Drive files will appear in the list
3. Click to download and import audio files

---

## Keyboard Shortcuts

| Action                | Windows Shortcut   |
|-----------------------|--------------------|
| New Project           | `Ctrl+N`           |
| Open Project          | `Ctrl+O`           |
| Save Project          | `Ctrl+S`           |
| Save As               | `Ctrl+Shift+S`     |
| Export Mixdown        | `Ctrl+Alt+E`       |
| Play/Pause            | `Spacebar`         |
| Stop                  | `Esc`              |
| Undo                  | `Ctrl+Z`           |
| Redo                  | `Ctrl+Shift+Z`     |
| Delete Selected       | `Delete`           |
| Select All            | `Ctrl+A`           |
| Zoom In               | `Ctrl++`           |
| Zoom Out              | `Ctrl+-`           |
| Rewind to Start       | `Home`             |

---

## Troubleshooting

### Audio Not Playing
- Check that an audio device is selected in **Settings** → **Audio Device**
- Ensure no tracks are muted (click the Mute button to unmute)
- Check your system volume

### Crackling or Latency
- Increase the buffer size in **Settings** → **Audio Device**
- Reduce the number of active plugins
- Close other applications to free system resources

### Plugins Not Showing
- Rescan plugins in **Settings** → **Plugin Scan**
- Ensure plugins are installed in a standard location

### Project Won't Open
- Verify the `.aerion` file is not corrupted
- Try opening from a different location
- Contact support if the issue persists

---

## Support & Feedback

- **Website:** https://aerion-daw.com
- **GitHub Issues:** https://github.com/aethos-studio/Aerion-DAW/issues
- **Documentation:** See `ARCHITECTURE.md` and `ROADMAP.md` in the project repository

---

**Aerion DAW v0.1.0** — Made by Aethos Studio
