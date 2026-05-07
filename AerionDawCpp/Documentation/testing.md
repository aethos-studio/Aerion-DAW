# Aerion DAW - Milestone 2 Testing Document

This document outlines the manual verification steps for the mixing and routing features implemented in Milestone 2 (v0.5.0).

## 1. Send / Return Routing & Bus Section
- [ ] **Bus Visibility:** Launch the app and open the Mixer. Verify that "Reverb Bus" and "Delay" (from mock data) are visible on the far right, separated from the audio tracks by a vertical gap.
- [ ] **Routing Flow:** Play audio through the "Lead Vocal" track. Verify that the meters on "Reverb Bus" also show signal, confirming the internal `AuxSend` -> `AuxReturn` routing is active.
- [ ] **Tracktion Logic:** Right-click an empty area of the Mixer and select "Add Bus Track". Verify a new bus appears at the end of the bus section (not intermingled with audio tracks).

## 2. Channel Strip Inline Controls
- [ ] **HPF (High Pass):** On any mixer strip, locate the "HP" value box. Click and drag **vertically** on the value. Verify the frequency updates (20 Hz - 20,000 Hz).
- [ ] **LPF (Low Pass):** Click and drag vertically on the "LP" value. Verify frequency updates.
- [ ] **Phase Invert:** Click the "Ø" button. Verify it turns blue (active). Verify that if two identical tracks are played with one inverted, the master output nulls (if source is mono).
- [ ] **Mono Sum:** Click the "O" button. Verify it turns blue. On a stereo source, verify the master meters show centered mono signal.

## 3. Insert Slot UI
- [ ] **Bypass Toggle:** Click the small circular LED icon on the left of a plugin name in a slot. Verify it turns from gray to blue (on/off).
- [ ] **Drag-to-Reorder:** Drag a plugin name from Slot 1 to Slot 3. Verify the plugin moves and the signal chain order updates (most noticeable with Serial processing like EQ before/after Distortion).
- [ ] **Inspector Sync:** Select a track in the Timeline. Verify the plugin slots in the left-hand Inspector match the Mixer strip and that bypass/reorder changes reflect in both places.

## 4. Gain Staging & Master Bus
- [ ] **Persistent Clip LED:** Play audio that exceeds 0 dBFS (e.g., crank the "Sub Bass" fader). Verify the red block at the very top of the level meter lights up and **stays red** even after the audio is stopped.
- [ ] **Clear Clip:** Click on the red clip LED or the dB readout above it. Verify the LED turns off.
- [ ] **K-Metering:** Observe the Master Bus meter. Verify it uses a different color gradient where the "Yellow" zone starts lower (approx -14 dBFS) to reflect K-14 metering standards.

## 5. Plugin Preset Browser
- [ ] **Activation:** Click on an active plugin slot (e.g., "EchoBoy" on the Delay bus). Verify the right-hand Browser panel automatically switches to the **"Presets"** tab.
- [ ] **Listing:** Verify a list of available factory/user programs for that specific plugin is displayed.
- [ ] **Loading:** Double-click a preset name. Verify the plugin's UI window refreshes (if open) and the new state is applied.

## 6. Mix Scene Snapshots
- [ ] **Save Snapshot:** Adjust several faders and pan pots. Click "Snapshots" in the Toolbar -> "Save new snapshot...". Enter a name like "Rough Mix 1".
- [ ] **Recall Snapshot:** Change the faders to different positions. Click "Snapshots" -> Select "Rough Mix 1". Verify all faders, pans, mutes, solos, and send levels return to their saved states.
- [ ] **Persistence:** Save the project, close, and reopen. Verify "Rough Mix 1" is still available in the Snapshots menu.

---
*Date Updated: May 5, 2026*
