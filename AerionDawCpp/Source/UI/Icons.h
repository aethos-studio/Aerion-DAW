#pragma once
#include <JuceHeader.h>
#include <map>
#include <tuple>
#include "ThemeTokens.h"
#include "CachedLayer.h"

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
        inline const juce::Identifier idProperty  { "aerionIconId" };

        // A colour no icon uses, so the first tint cannot collide with artwork.
        inline const juce::Colour initialInk { 0xff010203 };

        inline juce::Colour currentInk (const juce::Drawable& d)
        {
            const auto& v = d.getProperties()[inkProperty];
            return v.isVoid() ? initialInk : juce::Colour ((juce::uint32) (juce::int64) v);
        }

        inline void setInk (juce::Drawable& d, juce::Colour colour)
        {
            const auto ink = currentInk (d);
            if (ink != colour)
            {
                d.replaceColour (ink, colour);
                d.getProperties().set (inkProperty, (juce::int64) colour.getARGB());
            }
        }

        /** A stable id per drawable, so cached rasters can't be confused by a
            drawable that reuses a freed one's address. */
        inline juce::int64 idOf (juce::Drawable& d)
        {
            static juce::int64 nextId = 0;
            auto& props = d.getProperties();
            if (! props.contains (idProperty))
                props.set (idProperty, ++nextId);
            return (juce::int64) props[idProperty];
        }

        // Rasterised drawables: id, ink, raster size, display scale x 100, renderer.
        using RasterKey = std::tuple<juce::int64, juce::uint32, int, int, int, bool, int>;

        inline std::map<RasterKey, juce::Image>& rasterCache()
        {
            static std::map<RasterKey, juce::Image> cache;
            return cache;
        }
    }

    /** Draws `d` with `contentTransform` into the whole-pixel box at `origin`
        (`size` logical pixels), from a raster rendered once per drawable, ink
        colour, size, display scale and renderer. Rasterising SVG paths on every
        paint was the main cost of icons and fader caps; the copy is a blit.
        `variant` distinguishes different transforms into the same box. */
    inline void drawRasterised (juce::Graphics& g, juce::Drawable& d, juce::Point<int> origin,
                                juce::Point<int> size, const juce::AffineTransform& contentTransform,
                                int variant = 0)
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int pw = juce::roundToInt ((float) size.x * scale);
        const int ph = juce::roundToInt ((float) size.y * scale);

        // Unusual sizes aren't worth a cache entry.
        if (pw <= 0 || ph <= 0 || pw > 512 || ph > 512)
        {
            d.draw (g, 1.0f, contentTransform.translated ((float) origin.x, (float) origin.y));
            return;
        }

        const bool software = isSoftwareContext (g);
        const detail::RasterKey key { detail::idOf (d), detail::currentInk (d).getARGB(), pw, ph,
                                      juce::roundToInt (scale * 100.0f), software, variant };

        auto& cache = detail::rasterCache();
        auto it = cache.find (key);
        if (it == cache.end())
        {
            if (cache.size() > 512)
                cache.clear();

            auto image = makeImageFor (software, juce::Image::ARGB, pw, ph, true);
            {
                juce::Graphics ig (image);
                ig.addTransform (juce::AffineTransform::scale (scale));
                d.draw (ig, 1.0f, contentTransform);
            }
            it = cache.emplace (key, std::move (image)).first;
        }

        // A copy at whole physical pixels; the icon's own ink is already in the raster.
        drawPhysicalImage (g, it->second, scale, origin);
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
        icon in several states within one paint stays correct. The icon box is
        snapped to whole pixels so the cached raster is copied, not resampled. */
    inline void draw (juce::Graphics& g, juce::Drawable& icon, juce::Rectangle<float> area, juce::Colour colour)
    {
        detail::setInk (icon, colour);

        auto* composite = dynamic_cast<juce::DrawableComposite*> (&icon);
        const auto viewBox = composite != nullptr ? composite->getContentArea() : juce::Rectangle<float>();

        if (viewBox.isEmpty())
        {
            icon.drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
            return;
        }

        const auto box = area.toNearestInt();
        const auto fit = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                             .getTransformToFit (viewBox, box.withZeroOrigin().toFloat());
        drawRasterised (g, icon, box.getPosition(), { box.getWidth(), box.getHeight() }, fit);
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
