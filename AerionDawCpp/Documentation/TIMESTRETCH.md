# Time-stretch: which stretcher, and their licences

Preparation for **Audio Warping & Time-Stretch** (Milestone 6, planned for v0.6.0). Checked October 25, 2026, against the Tracktion Engine checkout Aerion builds with.

Aerion is licensed **GPLv3**, so every library linked into the app must be GPLv3-compatible.

## What Tracktion Engine can use

| Stretcher | Tracktion switch | Licence | Usable in Aerion |
|---|---|---|---|
| **SoundTouch** | `TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH` (**on today**, `CMakeLists.txt`) | LGPL 2.1 or later (header of `3rd_party/soundtouch/include/SoundTouch.h`, shipped inside Tracktion) | Yes, already built in |
| **Rubber Band** | `TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND`, with `TRACKTION_BUILD_RUBBERBAND` to compile its sources into the app | GPL (version 2 or later) or a paid commercial licence, from Breakfast Quay. Not shipped with Tracktion: the sources must be fetched | Yes under the GPL; confirm the licence text of the release we pin when adding it |
| **élastique** (zplane) | `TRACKTION_ENABLE_TIMESTRETCH_ELASTIQUE` | Proprietary SDK, commercial licence only | **No**: a proprietary library cannot be linked into a GPLv3 program that is distributed |

Tracktion's own notes on Rubber Band and élastique say the same: neither is owned by Tracktion, and each needs its own licence.

## Recommendation for v0.6.0

1. **Start with SoundTouch**, which is already compiled in. Use it for auto-tempo (clips following tempo changes) and per-clip speed and pitch, and get the editing work done: warp markers, the clip gain envelope and the Inspector controls. It is good enough on drums and loops, weaker on vocals and full mixes (phasing, smeared transients).
2. **Add Rubber Band as the quality option**, under the GPL: fetch a pinned release with FetchContent, build it with `TRACKTION_BUILD_RUBBERBAND`, and offer a per-clip (or per-project) choice of *Fast* (SoundTouch) or *High quality* (Rubber Band). Its newer engine sounds better and costs more CPU, so measure it with `AerionBench --audio` before making it the default.
3. **Leave out élastique** unless Aerion's licensing changes.

## To do when adding Rubber Band

- Pin the version and record its licence in the About dialog and the third-party notices, next to SoundTouch.
- Check that the macOS universal build and the Windows build both compile it (it has optional FFTW, SLEEF and IPP back ends; the built-in FFT avoids extra dependencies).
- Add a time-stretched clip to the `AerionBench --audio` reference project, as `PERFORMANCE.md` asks of M6 features.
