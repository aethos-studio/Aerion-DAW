#pragma once

// Base class for side panels with a titled header strip.

#include "ViewShared.h"

//==============================================================================
class DAWPanel : public juce::Component
{
public:
    DAWPanel(juce::String panelName) : name(panelName) {}
    void paint(juce::Graphics& g) override
    {
        Theme::fillBackgroundGradient (g, getLocalBounds());
        g.setColour(Theme::border);
        g.drawRect(getLocalBounds(), 1);

        auto header = getLocalBounds().removeFromTop(28);
        g.setColour(Theme::surface);
        g.fillRect(header);
        g.setColour(Theme::border);
        g.drawLine(0.0f, 28.0f, (float)getWidth(), 28.0f);

        g.setColour(Theme::textMuted);
        g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
        g.drawText(name.toUpperCase(), header.reduced(12, 0), juce::Justification::centredLeft, false);
    }
protected:
    juce::String name;
};
