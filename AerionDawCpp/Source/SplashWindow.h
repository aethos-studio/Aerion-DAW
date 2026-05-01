#pragma once
#include <JuceHeader.h>
#include "LogoComponent.h"

class SplashWindow : public juce::Component,
                     private juce::Timer
{
public:
    SplashWindow (std::function<void()> onFinishedCallback)
        : onFinished (onFinishedCallback)
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);

        addAndMakeVisible (logo);

        const int size = 400;
        setSize (size, size);

        // Center on screen
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            centreWithSize (size, size);

        // Make it a frameless OS window
        addToDesktop (juce::ComponentPeer::windowIsTemporary |
                      juce::ComponentPeer::windowIgnoresMouseClicks |
                      juce::ComponentPeer::windowIsSemiTransparent);
        
        setVisible (true);
        startTimerHz (60);
    }

    ~SplashWindow() override
    {
        stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        // Background is transparent, LogoComponent handles its own drawing
    }

    void resized() override
    {
        logo.setBounds (getLocalBounds());
    }

private:
    void timerCallback() override
    {
        elapsedFrames++;

        // 1. Scale animation (slowly expand from 0.9 to 1.05)
        float scale = 0.9f + (0.15f * std::min (1.0f, elapsedFrames / 120.0f));
        logo.setTransform (juce::AffineTransform::scale (scale, scale, 
                                                        getWidth() * 0.5f, 
                                                        getHeight() * 0.5f));

        // 2. Fade logic
        const int fadeStartFrame = 90; // After 1.5 seconds at 60Hz
        const int fadeDuration   = 30; // 0.5 seconds fade

        if (elapsedFrames >= fadeStartFrame)
        {
            float progress = std::min (1.0f, (float)(elapsedFrames - fadeStartFrame) / fadeDuration);
            float alpha = 1.0f - progress;
            setAlpha (alpha);

            if (progress >= 1.0f)
            {
                stopTimer();
                
                // Use callAsync to ensure the SplashWindow has finished its current
                // callback before the app destroys it and creates the main window.
                auto callback = onFinished;
                juce::MessageManager::callAsync ([callback] {
                    if (callback)
                        callback();
                });
            }
        }
    }

    LogoComponent logo;
    std::function<void()> onFinished;
    int elapsedFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashWindow)
};
