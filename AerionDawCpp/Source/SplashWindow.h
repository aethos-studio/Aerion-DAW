#pragma once
#include <JuceHeader.h>
#include "UI/ThemeTypefaces.h"

// Splash screen: logo emerges from a misty fog like a spectre from darkness.
// Phase 1 (0-90f  / 1.5s): fog materialises from black
// Phase 2 (40-120f/ 2.0s): logo rises out of the fog
// Phase 3 (110-165f/2.8s): "AERION" and subtitle fade in
// Phase 4 (165f+        ): hold until setReady(), then fade to black

class SplashWindow : public juce::Component, private juce::Timer
{
public:
    // onFinished is called on the message thread once the splash has fully faded.
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

        g.fillAll (juce::Colour (0xff080a0e));

        // -- Fog -------------------------------------------------------------
        const float fogRise = juce::jmin (1.0f, elapsedFrames / 90.0f);

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
                g.fillEllipse (logoCenter.x - r, logoCenter.y - r * 0.50f,
                               r * 2.0f, r * 1.00f);
            }
        }

        // -- Logo -------------------------------------------------------------
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

        // -- Title & subtitle -------------------------------------------------
        const float titleAlpha    = juce::jlimit (0.0f, 1.0f, (elapsedFrames - 110.0f) / 55.0f);
        const float subtitleAlpha = juce::jlimit (0.0f, 1.0f, (elapsedFrames - 130.0f) / 50.0f);
        const float textBaseY = logoCenter.y + 126.0f;
        const auto regTF = ThemeTypefaces::cinzelRegular();

        if (titleAlpha > 0.0f)
        {
            juce::Font aerionFont (regTF != nullptr
                ? juce::FontOptions (regTF).withHeight (38.0f)
                : juce::FontOptions().withHeight (38.0f));
            aerionFont.setExtraKerningFactor (0.12f);

            auto boldTF = ThemeTypefaces::cinzelBold();
            juce::Font dawFont (boldTF != nullptr
                ? juce::FontOptions (boldTF).withHeight (38.0f)
                : juce::FontOptions().withHeight (38.0f).withStyle ("Bold"));
            dawFont.setExtraKerningFactor (0.12f);

            juce::AttributedString titleText;
            titleText.setJustification (juce::Justification::centred);
            titleText.append ("AERION ", aerionFont, juce::Colour (0xffebf8ff).withAlpha (titleAlpha));
            titleText.append ("DAW",     dawFont,    juce::Colour (0xff3182ce).withAlpha (titleAlpha));
            titleText.draw (g, juce::Rectangle<float> (0.0f, textBaseY, (float) W, 50.0f));
        }

        if (subtitleAlpha > 0.0f)
        {
            juce::Font subtitleFont (regTF != nullptr
                ? juce::FontOptions (regTF).withHeight (22.0f)
                : juce::FontOptions().withHeight (22.0f));
            subtitleFont.setExtraKerningFactor (0.22f);

            g.setFont (subtitleFont);
            g.setColour (juce::Colour (0xff63b3ed).withAlpha (subtitleAlpha * 0.60f));
            g.drawText ("BY AETHOS STUDIO", 0, (int) textBaseY + 54, (int) W, 36,
                        juce::Justification::centred);
        }

        // -- Status line (visible once fog is mostly up) ----------------------
        juce::String status;
        {
            const juce::ScopedLock sl (statusLock);
            status = currentStatus;
        }
        const float statusAlpha = juce::jlimit (0.0f, 1.0f, (elapsedFrames - 60.0f) / 30.0f);
        if (status.isNotEmpty() && statusAlpha > 0.0f)
        {
            juce::Font statusFont (regTF != nullptr
                ? juce::FontOptions (regTF).withHeight (12.0f)
                : juce::FontOptions().withHeight (12.0f));
            statusFont.setExtraKerningFactor (0.18f);

            const int maxWidth = (int) W - 40;
            if (statusFont.getStringWidth (status) > maxWidth)
            {
                while (status.length() > 4 && statusFont.getStringWidth (status + "...") > maxWidth)
                    status = status.dropLastCharacters (1);
                status += "...";
            }

            g.setFont (statusFont);
            g.setColour (juce::Colour (0xff63b3ed).withAlpha (statusAlpha * 0.55f));
            g.drawText (status, 0, (int) textBaseY + 96, (int) W, 24,
                        juce::Justification::centred);
        }
    }

    // Thread-safe: can be called from any thread.
    void setStatus (juce::String s)
    {
        {
            const juce::ScopedLock sl (statusLock);
            currentStatus = std::move (s);
        }
        juce::Component::SafePointer<SplashWindow> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->repaint(); });
    }

    // Call once the app is fully initialised. The splash will hold until the
    // animation has played through its intro, then fade to black and fire onFinished.
    void setReady() { isReady = true; }

private:
    void timerCallback() override
    {
        elapsedFrames++;
        repaint();

        // Require the intro to have reached at least past the subtitle reveal
        // (frame 180) so the user always sees the full animation, even on fast hardware.
        const int minFrames    = 180;
        const int fadeDuration = 18;

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
                onFinished = nullptr;
                juce::MessageManager::callAsync ([cb] { if (cb) cb(); });
            }
        }
    }

    std::unique_ptr<juce::Drawable> logoDrawable;
    std::function<void()> onFinished;
    juce::CriticalSection statusLock;
    juce::String currentStatus;
    int  elapsedFrames  = 0;
    int  fadeBeginFrame = 0;
    bool isReady        = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashWindow)
};
