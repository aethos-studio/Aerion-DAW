# CLAP hosting: feasibility spike (M6)

**Date:** October 9, 2026 · **Verdict: not now.** Keep Aerion on VST3 (and Audio Units on macOS) for M6. Revisit CLAP after out-of-process plugin hosting, or sooner if JUCE or Tracktion ship CLAP hosting.

## Question

Can Aerion host CLAP plugins with the stack it has (JUCE 8, Tracktion Engine 3.2), and at what cost?

## Findings

1. **JUCE 8 cannot host CLAP.** `juce_audio_processors` has no CLAP format; `AudioPluginFormatManager::addDefaultFormats()` (which Aerion calls in `AudioEngine.cpp`) adds VST3 and AU only.
2. **JUCE 9 does not change that.** JUCE's 2024 roadmap promised CLAP for JUCE 9 as a way to *author* plugins, not to host them ([JUCE roadmap Q3 2024](https://juce.com/?p=1627)). JUCE 9.0.0 has shipped, and its release notes do not mention CLAP at all ([9.0.0 release](https://newreleases.io/project/github/juce-framework/JUCE/release/9.0.0)). Moving Aerion to JUCE 9 would also mean moving Tracktion Engine, which is built on JUCE 8.
3. **Tracktion Engine 3.2 has no CLAP hosting.** Its plugin layer (`ExternalPlugin`) wraps whatever `juce::AudioPluginInstance` the format manager creates, so it would host CLAP only through a JUCE `AudioPluginFormat` for CLAP.
4. **The one JUCE CLAP host format is unfinished.** [`juce_clap_hosting`](https://github.com/jatinchowdhury18/juce_clap_hosting) (MIT, a `CLAPPluginFormat` you add to the format manager) calls itself "super-alpha". It scans and processes audio through stereo plugins. MIDI, the GUI extension, latency, the state extension, parameter flushing and audio-port configurations are still on its to-do list.
5. **The best-known CLAP-JUCE project does not host.** [`clap-juce-extensions`](https://github.com/free-audio/clap-juce-extensions) builds CLAP *plugins* from JUCE projects and states that it does not support hosting.
6. **CLAP developers suggest hosting CLAP directly,** not through JUCE's `AudioProcessor` model, which flattens CLAP-specific features such as per-note modulation ([discussion](https://github.com/free-audio/clap-juce-extensions/discussions/152); [JUCE forum](https://forum.juce.com/t/fr-support-clap-for-plugins-host-client/51860)).

## Options

| Option | What it takes | Fidelity | Rough effort |
|---|---|---|---|
| A. Wait for JUCE or Tracktion | Nothing now; nothing announced for hosting | Whatever they ship | none |
| B. Finish `juce_clap_hosting` and add it to `pluginFormatManager` | MIDI and note ports, GUI embedding (HWND / NSView), state, latency, parameter flush and rescan, thread-check and timer support, audio-port layouts; then scanning in Aerion's scanner child process, crash guard and Windows/macOS testing with real CLAP plugins (Surge XT, u-he, FabFilter) | VST3-like: CLAP-only features (per-note modulation, polyphonic parameters) are lost | 3-5 weeks |
| C. A native CLAP host next to Tracktion's plugin layer | A new `tracktion::Plugin` type around the CLAP C API, its own scanner, GUI hosting and state | Full CLAP | 6-10 weeks, plus ongoing upkeep against Tracktion |

What would carry over for free with option B: Tracktion's `ExternalPlugin` (insert chains, automation, MIDI learn, sidechain wiring) and Aerion's crash guard, which hooks `ExternalPlugin` processing.

## Why not now

- No plugin a tester has reported is CLAP-only; the major CLAP vendors also ship VST3, which Aerion hosts.
- Option B is a month of work for the same features VST3 already gives, and C is larger than any remaining M6 item.
- Out-of-process plugin hosting (deferred from M5) changes how every plugin format is loaded. Adding a format before it means doing the format work twice.

## When to revisit

- JUCE or Tracktion announce CLAP *hosting*, or `juce_clap_hosting` reaches MIDI, GUI and state support.
- Out-of-process hosting is in place (then CLAP can be added to the child process once).
- Testers ask for a CLAP-only plugin.
