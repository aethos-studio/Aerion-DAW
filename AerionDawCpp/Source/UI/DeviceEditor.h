#pragma once

// Editor for devices that have no window of their own: Tracktion's built-in
// effects and instruments. One knob per automatable parameter.

#include <JuceHeader.h>
#include "ThemeTokens.h"

class DeviceEditor : public tracktion::Plugin::EditorComponent,
                     private juce::Timer
{
public:
    static constexpr int kKnobW = 84, kKnobH = 96, kMaxColumns = 8, kMaxHeight = 640, kPad = 10;

    explicit DeviceEditor (tracktion::Plugin& p) : plugin (p)
    {
        setTitle (plugin.getName());
        setFocusContainerType (FocusContainerType::focusContainer);

        for (auto* param : plugin.getAutomatableParameters())
            knobs.add (new Knob (*param));

        for (auto* k : knobs)
            content.addAndMakeVisible (k);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        const int columns = juce::jlimit (1, kMaxColumns, knobs.size());
        const int rows    = juce::jmax (1, (knobs.size() + columns - 1) / columns);
        content.setSize (columns * kKnobW + 2 * kPad, rows * kKnobH + 2 * kPad);

        const int scrollW = content.getHeight() > kMaxHeight ? viewport.getScrollBarThickness() : 0;
        setSize (content.getWidth() + scrollW, juce::jmin (kMaxHeight, content.getHeight()));

        startTimerHz (20);
    }

    bool allowWindowResizing() override                         { return false; }
    juce::ComponentBoundsConstrainer* getBoundsConstrainer() override { return nullptr; }

    void paint (juce::Graphics& g) override  { g.fillAll (Theme::bgPanel); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());

        const int columns = juce::jlimit (1, kMaxColumns, knobs.size());
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (kPad + (i % columns) * kKnobW, kPad + (i / columns) * kKnobH, kKnobW, kKnobH);
    }

    int getNumKnobs() const noexcept                      { return knobs.size(); }
    juce::Slider& getKnobSlider (int index)               { return knobs[index]->slider; }

private:
    struct Knob : public juce::Component
    {
        explicit Knob (tracktion::AutomatableParameter& p) : param (&p)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW - 4, 16);
            slider.setRange (0.0, 1.0, discreteInterval());
            slider.setTitle (p.getParameterName());
            slider.textFromValueFunction = [this] (double v) { return text (v); };
            slider.valueFromTextFunction = [this] (const juce::String& s)
            {
                return (double) param->valueRange.convertTo0to1 (param->valueRange.getRange().clipValue (param->stringToValue (s)));
            };

            if (auto def = p.getDefaultValue())
                slider.setDoubleClickReturnValue (true, param->valueRange.convertTo0to1 (*def));

            slider.onDragStart   = [this] { param->parameterChangeGestureBegin(); };
            slider.onDragEnd     = [this] { param->parameterChangeGestureEnd(); };
            slider.onValueChange = [this] { param->setNormalisedParameter ((float) slider.getValue(), juce::sendNotification); };
            slider.setValue (p.getCurrentNormalisedValue(), juce::dontSendNotification);
            addAndMakeVisible (slider);

            name.setText (p.getParameterName(), juce::dontSendNotification);
            name.setJustificationType (juce::Justification::centred);
            name.setFont (Theme::uiSize (10.0f));
            name.setColour (juce::Label::textColourId, Theme::textMuted);
            name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (name);
        }

        double discreteInterval() const
        {
            if (param->isDiscrete() && param->getNumberOfStates() > 1)
                return 1.0 / (param->getNumberOfStates() - 1);

            return 0.0;
        }

        juce::String text (double normalised) const
        {
            const float v = param->valueRange.convertFrom0to1 ((float) normalised);
            auto s = param->valueToString (v);
            auto label = param->getLabel();
            return label.isNotEmpty() && ! s.endsWith (label) ? s + " " + label : s;
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2);
            name.setBounds (r.removeFromTop (16));
            slider.setBounds (r);
        }

        void refresh()
        {
            if (! slider.isMouseButtonDown())
                slider.setValue (param->getCurrentNormalisedValue(), juce::dontSendNotification);
        }

        tracktion::AutomatableParameter::Ptr param;
        juce::Slider slider;
        juce::Label name;
    };

    // Automation and other windows move parameters too.
    void timerCallback() override
    {
        for (auto* k : knobs)
            k->refresh();
    }

    tracktion::Plugin& plugin;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Knob> knobs;
};
