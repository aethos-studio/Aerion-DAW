#pragma once
#include <JuceHeader.h>

// Centralized design tokens for Aerion's Studio One-inspired theme.
// This is intentionally lightweight and header-only for now.
namespace Theme
{
    // ---- Color tokens -------------------------------------------------------
    inline const juce::Colour bgBase        = juce::Colour::fromString ("#ff080a0e"); // Deep charcoal
    inline const juce::Colour bgPanel       = juce::Colour::fromString ("#ff11141a"); // Panel base
    inline const juce::Colour surface       = juce::Colour::fromString ("#ff1c212b"); // Raised surface
    inline const juce::Colour border        = juce::Colour::fromString ("#ff2d3748"); // Divider/border
    inline const juce::Colour accent        = juce::Colour::fromString ("#ff63b3ed"); // Accent (soft)
    inline const juce::Colour active        = juce::Colour::fromString ("#ff3182ce"); // Active (primary)
    inline const juce::Colour textMain      = juce::Colour::fromString ("#fff0f4f8"); // Primary text
    inline const juce::Colour textMuted     = juce::Colour::fromString ("#ff8a99a8"); // Muted text
    inline const juce::Colour playhead      = juce::Colour::fromString ("#ffebf8ff");
    inline const juce::Colour meterGreen    = juce::Colour::fromString ("#ff48bb78");
    inline const juce::Colour meterYellow   = juce::Colour::fromString ("#ffecc94b");
    inline const juce::Colour meterRed      = juce::Colour::fromString ("#fff56565");
    inline const juce::Colour recordRed     = juce::Colour::fromString ("#ffe53e3e");

    inline const juce::Colour trackColours[6] = {
        juce::Colour::fromString ("#ff3182ce"),
        juce::Colour::fromString ("#ff4a5568"),
        juce::Colour::fromString ("#ff63b3ed"),
        juce::Colour::fromString ("#ffa0aec0"),
        juce::Colour::fromString ("#ff2c5282"),
        juce::Colour::fromString ("#ff4299e1")
    };

    inline juce::Colour colourForTrack (int idx)
    {
        return trackColours[((unsigned) idx) % 6];
    }

    // ---- Geometry tokens ----------------------------------------------------
    inline constexpr float radiusS = 3.0f;
    inline constexpr float radiusM = 4.0f;
    inline constexpr float radiusL = 6.0f;

    // ---- Typography tokens --------------------------------------------------
    /** Fallback system UI face for body copy (Cinzel is reserved for splash/branding only). */
    inline juce::String defaultSansSerifFaceName()
    {
       #if JUCE_WINDOWS
        return "Segoe UI";
       #elif JUCE_MAC
        return "Helvetica Neue";
       #elif JUCE_LINUX || JUCE_BSD
        return "DejaVu Sans";
       #else
        return {};
       #endif
    }

    /** Multiply logical point sizes so body copy stays readable (HiDPI + thin themes). */
    inline constexpr float kUiFontScale = 1.14f;

    /** Readable UI body font: system sans at a logical size (scaled). Prefer this over raw
        juce::Font (pt) so menu bars, panels, and timeline stay consistent. */
    inline juce::Font uiSize (float logicalPt)
    {
        const float h = logicalPt * kUiFontScale;
        juce::FontOptions opt = juce::FontOptions().withHeight (h);
        if (auto name = defaultSansSerifFaceName(); name.isNotEmpty())
            opt = opt.withName (name);
        return juce::Font (opt);
    }

    inline juce::Font fontLabel (float h)
    {
        return uiSize (h);
    }

    inline juce::Font fontLabelBold (float h)
    {
        auto f = uiSize (h);
        f.setBold (true);
        return f;
    }

    // ---- Drawing primitives -------------------------------------------------
    inline void drawRoundedPanel (juce::Graphics& g,
                                  juce::Rectangle<float> b,
                                  juce::Colour color = surface,
                                  float alpha = 1.0f,
                                  float r = radiusM)
    {
        g.setColour (color.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (b, r);
        g.setColour (border.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (b, r, 1.0f);
    }

    inline void fillBackgroundGradient (juce::Graphics& g, juce::Rectangle<int> area)
    {
        // Subtle depth (Studio One-ish): slightly lighter towards the top.
        juce::ColourGradient cg (bgBase.brighter (0.10f), 0.0f, (float) area.getY(),
                                 bgBase.darker   (0.05f), 0.0f, (float) area.getBottom(), false);
        g.setGradientFill (cg);
        g.fillRect (area);
    }
}

