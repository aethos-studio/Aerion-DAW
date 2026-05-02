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
        g.fillAll (juce::Colours::transparentBlack);

        // Draw "Circuit Growth" evolving lines around the logo
        auto b = getLocalBounds().toFloat().reduced (10.0f);
        auto center = b.getCentre();
        
        // Growth driven by timer (pulsing animation)
        float grow = (1.0f + std::sin (elapsedFrames * 0.05f)) * 0.5f;
        
        // Colors from theme
        auto arctic = juce::Colour::fromString ("#ff63b3ed");
        auto silver = juce::Colour::fromString ("#ffa0aec0");

        // Helper to draw a growing angled line
        auto interpolate = [](juce::Point<float> p1, juce::Point<float> p2, float d) -> juce::Point<float> {
            if (p1.getDistanceFrom (p2) <= 0.001f) return p1;
            return p1 + (p2 - p1) * (d / p1.getDistanceFrom (p2));
        };

        auto drawCircuit = [&](juce::Point<float> start, juce::Point<float> mid, juce::Point<float> end, 
                              juce::Colour col, float thickness, float progress) 
        {
            if (progress <= 0) return;
            
            juce::Path p;
            p.startNewSubPath (start);
            
            float segment1Len = start.getDistanceFrom (mid);
            float segment2Len = mid.getDistanceFrom (end);
            float totalLen = segment1Len + segment2Len;
            float currentLen = totalLen * progress;

            juce::Point<float> tip;
            if (currentLen <= segment1Len) {
                tip = interpolate (start, mid, currentLen);
                p.lineTo (tip);
            } else {
                tip = interpolate (mid, end, currentLen - segment1Len);
                p.lineTo (mid);
                p.lineTo (tip);
            }

            g.setColour (col.withAlpha (0.6f));
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            
            // Draw a small glowing node at the leading tip
            g.setColour (col);
            g.fillEllipse (tip.x - 2.0f, tip.y - 2.0f, 4.0f, 4.0f);
        };

        // Top-Left path
        drawCircuit ({80, 80}, {120, 80}, {120, 40}, arctic, 1.5f, grow);
        // Bottom-Right path
        drawCircuit ({320, 320}, {280, 320}, {280, 360}, arctic, 1.5f, grow);
        // Top-Right path
        drawCircuit ({320, 80}, {320, 120}, {360, 120}, silver, 1.0f, grow * 0.8f);
        // Bottom-Left path
        drawCircuit ({80, 320}, {80, 280}, {40, 280}, silver, 1.0f, grow * 0.8f);
    }

    void resized() override
    {
        logo.setBounds (getLocalBounds().reduced (60)); // Extra padding for circuit lines
    }

    void setReady() { isReady = true; }

private:
    void timerCallback() override
    {
        elapsedFrames++;

        repaint(); // Force repaint for line animation

        const int fadeDuration = 40;  // ~0.6 seconds fade

        // Only begin fading if the app is ready
        if (isReady)
        {
            if (fadeBeginFrame == 0)
                fadeBeginFrame = elapsedFrames;

            float progress = std::min (1.0f, (float)(elapsedFrames - fadeBeginFrame) / fadeDuration);
            setAlpha (1.0f - progress);

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
    int fadeBeginFrame = 0;
    bool isReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashWindow)
};
