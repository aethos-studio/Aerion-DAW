#pragma once
#include <JuceHeader.h>
#include "UI/ThemeTypefaces.h"

// Splash screen: logo emerges from a misty fog like a spectre from darkness.
// Phase 1 (0-90f  / 1.5s): fog materialises from black
// Phase 2 (40-120f/ 2.0s): logo rises out of the fog
// Phase 3 (110-165f/2.8s): "AERION" and subtitle fade in
// Phase 4 (240f+  / 4.0s): hold until ready, then fade to black

class SplashWindow : public juce::Component, private juce::Timer
{
public:
    SplashWindow (std::function<void()> onFinishedCallback)
        : onFinished (onFinishedCallback)
    {
        if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (
                BinaryData::aerion_logo_vertical_svg,
                BinaryData::aerion_logo_vertical_svgSize)))
            logoDrawable = juce::Drawable::createFromSVG (*xml);

        setOpaque (true);
        setInterceptsMouseClicks (false, false);

        const int w = 400, h = 460;
        setSize (w, h);

        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
            centreWithSize (w, h);

        addToDesktop (juce::ComponentPeer::windowIsTemporary |
                      juce::ComponentPeer::windowIgnoresMouseClicks);
        setVisible (true);
        startTimerHz (60);
    }

    ~SplashWindow() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const float W = (float) getWidth();
        const float H = (float) getHeight();
        const juce::Point<float> logoCenter (W * 0.5f, H * 0.42f);

        // Void background
        g.fillAll (juce::Colour (0xff080a0e));

        // -- Fog -------------------------------------------------------------
        // Rises over first 90 frames, then breathes slowly
        const float fogRise = juce::jmin (1.0f, elapsedFrames / 90.0f);

        // Four concentric fog halos, each rendered as a gradient of rings
        struct FogLayer { juce::Colour col; float radius; float baseAlpha; float phase; };
        const FogLayer fogLayers[] = {
            { juce::Colour (0xff6a95b5), 250.0f, 0.042f, 0.0f },
            { juce::Colour (0xff63b3ed), 170.0f, 0.075f, 1.4f },
            { juce::Colour (0xff9ecfeb), 108.0f, 0.068f, 2.3f },
            { juce::Colour (0xffd0eaf7),  56.0f, 0.058f, 3.7f },
        };

        for (const auto& fl : fogLayers)
        {
            const float pulse = 0.87f + 0.13f * std::sin (elapsedFrames * 0.022f + fl.phase);
            for (int i = 0; i < 8; i++)
            {
                const float t = i / 7.0f;
                const float r = fl.radius * (0.18f + t * 0.82f) * pulse;
                const float a = fogRise * fl.baseAlpha * std::pow (1.0f - t, 1.6f);
                g.setColour (fl.col.withAlpha (a));
                // Slightly wider than tall  -  fog hugs the ground
                g.fillEllipse (logoCenter.x - r, logoCenter.y - r * 0.50f,
                               r * 2.0f, r * 1.00f);
            }
        }

        // -- Logo -------------------------------------------------------------
        // Emerges from frame 40, fully present by frame 120
        const float logoAlpha = juce::jlimit (0.0f, 1.0f, (elapsedFrames - 40.0f) / 80.0f);
        if (logoDrawable != nullptr && logoAlpha > 0.0f)
        {
            const float logoSize = 235.0f;
            const juce::Rectangle<float> logoBounds (logoCenter.x - logoSize * 0.5f,
                                                     logoCenter.y - logoSize * 0.5f,
                                                     logoSize, logoSize);
            logoDrawable->drawWithin (g, logoBounds,
                                      juce::RectanglePlacement::centred, logoAlpha);
        }

        // -- Text -------------------------------------------------------------
        // "AERION" starts frame 110, "BY AETHOS STUDIO" 20 frames later
        const float titleAlpha    = juce::jlimit (0.0f, 1.0f, (elapsedFrames - 110.0f) / 55.0f);
        const float subtitleAlpha = juce::jlimit (0.0f, 1.0f, (elapsedFrames - 130.0f) / 50.0f);
        const float textBaseY = logoCenter.y + 126.0f;
        const auto regTF = ThemeTypefaces::cinzelRegular();

        if (titleAlpha > 0.0f)
        {
            // 1. Setup Cinzel Regular Font for "AERION" - Increased to 38.0f
            juce::Font aerionFont (regTF != nullptr
                ? juce::FontOptions (regTF).withHeight (38.0f)
                : juce::FontOptions().withHeight (38.0f));
            aerionFont.setExtraKerningFactor (0.12f);
            
            // 2. Setup Cinzel Bold Font for "DAW" - Increased to 38.0f
            auto boldTF = ThemeTypefaces::cinzelBold();
            juce::Font dawFont (boldTF != nullptr
                ? juce::FontOptions (boldTF).withHeight (38.0f)
                : juce::FontOptions().withHeight (38.0f).withStyle ("Bold"));
            dawFont.setExtraKerningFactor (0.12f);

            juce::AttributedString titleText;
            titleText.setJustification (juce::Justification::centred);

            // Append with their respective fonts and colors
            titleText.append ("AERION ", aerionFont, juce::Colour (0xffebf8ff).withAlpha (titleAlpha));
            titleText.append ("DAW", dawFont, juce::Colour (0xff3182ce).withAlpha (titleAlpha));

            // Increased bounding box height to 50.0f to fit the larger text
            titleText.draw (g, juce::Rectangle<float> (0.0f, textBaseY, (float) W, 50.0f));
        }

        if (subtitleAlpha > 0.0f)
        {
            // Set up subtitle font using the Regular Cinzel
            juce::Font subtitleFont (regTF != nullptr
                ? juce::FontOptions (regTF).withHeight (22.0f)
                : juce::FontOptions().withHeight (22.0f));
            
            subtitleFont.setExtraKerningFactor (0.22f);

            g.setFont (subtitleFont);
            g.setColour (juce::Colour (0xff63b3ed).withAlpha (subtitleAlpha * 0.60f));
            
            // Pushed the subtitle down slightly (from +46 to +54) to account for the larger title text
            g.drawText ("BY AETHOS STUDIO", 0, (int) textBaseY + 54, (int) W, 36,
                        juce::Justification::centred);
        }
    }

    void setReady() { isReady = true; }

private:
    void timerCallback() override
    {
        elapsedFrames++;
        repaint();

        // Keep a short branding beat, but do not block the UI for seconds (old: 240 frames ~= 4 s at 60 Hz).
        const int minFrames    = 24;  // ~0.4 s at 60 Hz
        const int fadeDuration = 18; // ~0.3 s fade to black

        if (isReady && elapsedFrames >= minFrames)
        {
            if (fadeBeginFrame == 0)
                fadeBeginFrame = elapsedFrames;

            const float progress = juce::jmin (1.0f,
                (float) (elapsedFrames - fadeBeginFrame) / fadeDuration);
            setAlpha (1.0f - progress);

            if (progress >= 1.0f)
            {
                stopTimer();
                auto cb = onFinished;
                juce::MessageManager::callAsync ([cb] { if (cb) cb(); });
            }
        }
    }

    std::unique_ptr<juce::Drawable> logoDrawable;
    std::function<void()> onFinished;
    int elapsedFrames  = 0;
    int fadeBeginFrame = 0;
    bool isReady       = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashWindow)
};