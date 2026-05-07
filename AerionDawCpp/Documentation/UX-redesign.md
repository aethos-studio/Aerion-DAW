# Aerion DAW: UI/UX Restructuring & Iconography Guide

## 1. The "Glacial Runic" Icon Design System
To maintain the Aethos Studio brand identity, all UI icons must adhere to the "Glacial Runic" visual concept. They should evoke the feeling of brushed steel and frozen machinery. 

When implementing these SVGs in your C++ code, observe the following color states based on your brand palette:
*   **Default/Inactive State:** Steel Gray (`#8A99A8`).
*   **Hover State:** Arctic Blue (`#63B3ED`)[cite: 1].
*   **Active/Engaged State:** Ice Blue (`#3182CE`)[cite: 1].
*   **Urgent/Arm State:** Record Red (`#E53E3E`)[cite: 1].
*   **Mute State:** Gold-Tinted Steel (`#ECC94B`)[cite: 1].
*   **Solo State:** Arctic Emerald (`#48BB78`)[cite: 1].

## 2. Toolbar & Layout Reorganization
Instead of scattering buttons, we are moving to **logical grouping** using sharp lines and clear divisions[cite: 1]. Groups are separated by exactly 1px dividers using Metallic Slate (`#2D3748`)[cite: 1]. Ensure any panels or button backgrounds use a strict 4.0f corner radius to maintain the machined metal look[cite: 1].

### Top Toolbar (Left Side)
*   **Group 1: UI Toggles**
    *   `aerion_inspector.svg` (Anchored to the far left).
    *   *1px Metallic Slate Divider*
*   **Group 2: The Toolbox**
    *   `aerion_select.svg`
    *   `aerion_cut.svg`
    *   `aerion_comp.svg`
    *   *1px Metallic Slate Divider*
*   **Group 3: Recording Setup**
    *   `aerion_punch.svg`
    *   `aerion_pdc.svg`

### Top Toolbar (Right Side)
*   **Group 4: Time & Grid**
    *   `aerion_magnet.svg` (Snap)
    *   *1px Metallic Slate Divider*
*   **Group 5: Metronome**
    *   `aerion_metronome.svg` (Click)
    *   `aerion_countin.svg`
    *   *1px Metallic Slate Divider*
*   **Group 6: UI Toggles**
    *   `aerion_browser.svg` (Anchored to the far right).

### Inspector (Track Controls)
Ditch the large text pills. Place the track controls in a tight horizontal row beneath the routing section.
*   **Group:** `aerion_arm.svg`, `aerion_mute.svg`, `aerion_solo.svg`.
*   **Background:** Dark Slate (`#1C212B`)[cite: 1].

---

## 3. SVG Asset Library
Create a new file for each of the code blocks below, naming them exactly as specified. They are built on a standard `24x24` grid with 1.5px stroke widths for surgical precision[cite: 1].

### `aerion_inspector.svg`
```xml
<svg xmlns="[http://www.w3.org/2000/svg](http://www.w3.org/2000/svg)" viewBox="0 0 24 24" fill="none" stroke="#8A99A8" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
  <rect x="3" y="3" width="18" height="18" rx="2"/>
  <line x1="9" y1="3" x2="9" y2="21"/>
</svg>