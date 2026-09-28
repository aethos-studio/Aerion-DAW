#pragma once
#include <JuceHeader.h>
#include "ThemeTokens.h"

//==============================================================================
// Embedded SVG icons.
//
// All icons are drawn on a 24x24 viewBox with padding built into the design.
// Drawable::drawWithin fits an icon's *drawn content* to the target area
// instead, which throws that padding away: small glyphs (arm, solo, stop)
// are blown up, wide ones touch the button edge, and stroke weights end up
// different from icon to icon. Icons::draw fits the viewBox, so every icon
// keeps the proportions and weight it was designed with.
//
// The SVGs are drawn in either #8A99A8 or white. Icons::load normalises them
// to one ink colour so Icons::draw can tint any icon to any state colour.
//==============================================================================

namespace Icons
{
    namespace detail
    {
        inline const juce::Identifier inkProperty { "aerionIconInk" };

        // A colour no icon uses, so the first tint cannot collide with artwork.
        inline const juce::Colour initialInk { 0xff010203 };

        inline juce::Colour currentInk (const juce::Drawable& d)
        {
            const auto& v = d.getProperties()[inkProperty];
            return v.isVoid() ? initialInk : juce::Colour ((juce::uint32) (juce::int64) v);
        }
    }

    inline std::unique_ptr<juce::Drawable> load (const char* data, int size)
    {
        auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size));
        if (xml == nullptr)
            return nullptr;

        auto d = juce::Drawable::createFromSVG (*xml);
        if (d == nullptr)
            return nullptr;

        for (auto c : { Theme::textMuted, juce::Colours::white, juce::Colours::black })
            d->replaceColour (c, detail::initialInk);

        d->getProperties().set (detail::inkProperty, (juce::int64) detail::initialInk.getARGB());
        return d;
    }

    /** Draws `icon` centred in `area`, scaled so its viewBox fits, in `colour`.
        Recolouring only happens when the colour changes, so drawing the same
        icon in several states within one paint stays correct. */
    inline void draw (juce::Graphics& g, juce::Drawable& icon, juce::Rectangle<float> area, juce::Colour colour)
    {
        const auto ink = detail::currentInk (icon);
        if (ink != colour)
        {
            icon.replaceColour (ink, colour);
            icon.getProperties().set (detail::inkProperty, (juce::int64) colour.getARGB());
        }

        auto* composite = dynamic_cast<juce::DrawableComposite*> (&icon);
        const auto viewBox = composite != nullptr ? composite->getContentArea() : juce::Rectangle<float>();

        if (viewBox.isEmpty())
        {
            icon.drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
            return;
        }

        icon.draw (g, 1.0f, juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                .getTransformToFit (viewBox, area));
    }

    /** Icon colour on a button: accent-tinted when active on a light tint,
        dark when active on a solid fill, muted otherwise. */
    inline juce::Colour inkFor (bool active, bool solidActiveFill, juce::Colour activeColour)
    {
        if (! active)
            return Theme::textMuted;

        return solidActiveFill ? juce::Colour (0xff0b0e13) : activeColour;
    }
}
