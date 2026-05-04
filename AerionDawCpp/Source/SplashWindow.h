#pragma once
#include <JuceHeader.h>

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

        cinzelTypeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::CinzelRegular_ttf, (size_t) BinaryData::CinzelRegular_ttfSize);

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

        // ── Fog ─────────────────────────────────────────────────────────────
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
                // Slightly wider than tall — fog hugs the ground
                g.fillEllipse (logoCenter.x - r, logoCenter.y - r * 0.50f,
                               r * 2.0f, r * 1.00f);
            }
        }

        // ── Logo ─────────────────────────────────────────────────────────────
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

        // ── Text ─────────────────────────────────────────────────────────────
        // "AERION" starts frame 110, "BY AETHOS STUDIO" 20 frames later
        const float titleAlpha    = juce::jlimit (0.0f, 1.0f, (elapsedFrames - 110.0f) / 55.0f);
        const float subtitleAlpha = juce::jlimit (0.0f, 1.0f, (elapsedFrames - 130.0f) / 50.0f);
        const float textBaseY = logoCenter.y + 126.0f;

if (titleAlpha > 0.0f)
    {
        // Define the shared font
        juce::Font titleFont (juce::FontOptions().withHeight (25.0f).withStyle ("Bold"));
        
        juce::AttributedString titleText;
        titleText.setJustification (juce::Justification::centred);

        // 1. Append "AERION " in the original off-white color
        titleText.append ("AERION ", titleFont, juce::Colour (0xffebf8ff).withAlpha (titleAlpha));

        // 2. Append "DAW" in Ice Blue (0xFF3182CE)
        titleText.append ("DAW", titleFont, juce::Colour (0xff3182ce).withAlpha (titleAlpha));

        // 3. Draw the combined formatted string
        // Note: AttributedString::draw requires a Rectangle<float>
        titleText.draw (g, juce::Rectangle<float> (0.0f, textBaseY, (float) W, 32.0f));
    }

        if (subtitleAlpha > 0.0f)
        {
            juce::Font cinzelFont (cinzelTypeface != nullptr
                ? juce::FontOptions (cinzelTypeface).withHeight (20.0f)
                : juce::FontOptions().withHeight (16.0f));
            g.setFont (cinzelFont);
            g.setColour (juce::Colour (0xff63b3ed).withAlpha (subtitleAlpha * 0.60f));
            g.drawText ("BY AETHOS STUDIO", 0, (int) textBaseY + 36, (int) W, 24,
                        juce::Justification::centred);
        }
    }

    void setReady() { isReady = true; }

private:
    void timerCallback() override
    {
        elapsedFrames++;
        repaint();

        const int minFrames   = 240; // 4 s
        const int fadeDuration = 45; // 0.75 s fade to black

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
    juce::Typeface::Ptr cinzelTypeface;
    std::function<void()> onFinished;
    int elapsedFrames  = 0;
    int fadeBeginFrame = 0;
    bool isReady       = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashWindow)
};
