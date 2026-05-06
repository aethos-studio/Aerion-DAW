#pragma once
#include <JuceHeader.h>

/**
 * Same role as @c juce::TooltipWindow, but fixes JUCE stock "instant swap" behaviour:
 * moving straight from control A to B could show B's tooltip without a fresh dwell.
 * This version hides any visible tip when the target text changes, then always waits
 * @c millisecondsBeforeTipAppears after the last movement / target change.
 *
 * Parent this on your main editor / root component (see @c MainComponent) so the tip
 * stays in the same coordinate space as the UI.
 */
class AerionTooltipWindow final : public juce::Component, private juce::Timer
{
public:
    explicit AerionTooltipWindow (juce::Component* parentComponent, int millisecondsBeforeTipAppears = 700);

    ~AerionTooltipWindow() override;

    void setMillisecondsBeforeTipAppears (int newTimeMs = 700) noexcept;

    void displayTip (juce::Point<int> screenPosition, const juce::String& text);
    void hideTip();

    juce::String getTipFor (juce::Component& component);

private:
    enum class ShownManually { yes, no };
    void displayTipInternal (juce::Point<int> screenPos, const juce::String& tip, ShownManually manually);
    void updatePosition (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parentArea);

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void timerCallback() override;
    float getDesktopScaleFactor() const override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::Point<float> lastMousePos {};
    juce::Component::SafePointer<juce::Component> lastComponentUnderMouse {};
    juce::String tipShowing, lastTipUnderMouse, manuallyShownTip {};
    int millisecondsBeforeTipAppears = 700;
    juce::uint32 lastCompChangeTime = 0, lastHideTime = 0;
    bool reentrant = false, dismissalMouseEventOccurred = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AerionTooltipWindow)
};
