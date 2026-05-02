#pragma once
#include <JuceHeader.h>

class LogoComponent : public juce::Component
{
public:
    LogoComponent() 
    {
        if (auto svgXml = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::logo_vert_svg, BinaryData::logo_vert_svgSize)))
            drawable = juce::Drawable::createFromSVG (*svgXml);
    }

    void paint(juce::Graphics& g) override
    {
        if (drawable != nullptr)
            drawable->drawWithin (g, getLocalBounds().toFloat(), 
                                juce::RectanglePlacement::centred, 1.0f);
    }

private:
    std::unique_ptr<juce::Drawable> drawable;
};
