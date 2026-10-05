#pragma once
#include <JuceHeader.h>

//==============================================================================
// Keeps a component's rendered pixels between paints, so repainting part of
// the window (a playhead strip on an overlay above it, a tooltip, a meter
// next to it) redraws this component from the image instead of re-running
// its paint(). Only areas the component itself invalidates with repaint()
// are re-rendered.
//
// Works like Component::setBufferedToImage, with two differences that matter
// for Aerion's graphics engine setting (UI/GraphicsEngine.h):
//
//  - The cache image matches the window's renderer. JUCE's own cache always
//    uses a native (Direct2D) image, so with the software renderer every blit
//    would read the image back from the GPU, and every cache refresh would
//    render through Direct2D whatever engine the user picked.
//  - The image is copied at whole physical pixels, which the software
//    renderer does as a plain memory copy. On a scaled display (UI Size or
//    Windows scaling) the window can place the component at a fraction of a
//    pixel; snapping it there is invisible, resampling it is slow and blurry.
//
// Install with: component.setCachedComponentImage (new CachedLayer (component));
//==============================================================================

/** True when `g` draws with JUCE's software renderer (vs Direct2D / CoreGraphics). */
inline bool isSoftwareContext (juce::Graphics& g)
{
    return dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) != nullptr;
}

/** The display scale `g` draws at: 1.25 at 125 % UI Size, 1.5 at 150 %
    Windows scaling, both multiplied together when both apply. */
inline float physicalScaleOf (juce::Graphics& g)
{
    return g.getInternalContext().getPhysicalPixelScaleFactor();
}

/** Draws `image`, whose pixels are physical pixels at `scale`, with its
    top-left `physicalOffset` physical pixels from the logical point `anchor`.
    The position is snapped to whole physical pixels, so the image is copied
    rather than resampled, and pieces drawn against the same anchor line up
    without gaps or overlaps. Assumes `g`'s origin is on a whole physical
    pixel, which holds inside a CachedLayer. */
inline void drawPhysicalImage (juce::Graphics& g, const juce::Image& image, float scale,
                               juce::Point<int> anchor, juce::Point<int> physicalOffset = {})
{
    // floor (v + 0.5), not roundToInt, which rounds halves to even: at 125 %
    // a 4 px scroll moves an anchor at x.5 by 5 px and would flip its rounding.
    const juce::Point<float> device (std::floor ((float) anchor.x * scale + 0.5f) + (float) physicalOffset.x,
                                     std::floor ((float) anchor.y * scale + 0.5f) + (float) physicalOffset.y);

    juce::Graphics::ScopedSaveState state (g);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    // drawImage uses the current colour's opacity, which otherwise depends on
    // whatever was drawn before (and so on how much is being repainted).
    g.setOpacity (1.0f);
    g.drawImageTransformed (image, juce::AffineTransform::translation (device).scaled (1.0f / scale));
}

/** An image that draws fast into contexts like `g`: a software image for the
    software renderer, a native one otherwise. Drawing the other kind means a
    conversion or GPU readback on every draw. */
inline juce::Image makeImageFor (bool software, juce::Image::PixelFormat format, int w, int h, bool clear)
{
    return software ? juce::Image (format, w, h, clear, juce::SoftwareImageType())
                    : juce::Image (format, w, h, clear, juce::NativeImageType());
}

class CachedLayer final : public juce::CachedComponentImage
{
public:
    explicit CachedLayer (juce::Component& c) noexcept : owner (c) {}

    /** What an opaque owner's pixels are reset to before they are drawn again
        on a scaled display. There, shapes meet inside physical pixels, and an
        edge pixel keeps a share of whatever it held before; starting from a
        fixed colour makes a redraw look the same as a fresh render. Pick the
        owner's main background so the share is invisible. */
    void setUnderlay (juce::Colour c) noexcept   { underlay = c; }

    void paint (juce::Graphics& g) override
    {
        const float scale = physicalScaleOf (g);
        const bool software = isSoftwareContext (g);
        lastScale = scale;

        const auto compBounds  = owner.getLocalBounds();
        const auto imageBounds = compBounds * scale;

        if (image.isNull() || image.getBounds() != imageBounds || software != imageIsSoftware)
        {
            const auto format = owner.isOpaque() ? juce::Image::RGB : juce::Image::ARGB;
            const int w = juce::jmax (1, imageBounds.getWidth());
            const int h = juce::jmax (1, imageBounds.getHeight());

            image = makeImageFor (software, format, w, h, ! owner.isOpaque());
            image.setBackupEnabled (false);
            imageIsSoftware = software;
            validArea.clear();
        }

        // A native image whose device went away has lost its contents.
        if (auto ptr = image.getPixelData())
            if (auto* extensions = ptr->getBackupExtensions())
                if (extensions->needsBackup() && ! extensions->canBackup())
                    validArea.clear();

        if (! validArea.containsRectangle (compBounds))
        {
            juce::Graphics imG (image);
            auto& lg = imG.getInternalContext();
            lg.addTransform (juce::AffineTransform::scale (scale));

            for (auto& r : validArea)
                lg.excludeClipRectangle (r);

            if (! owner.isOpaque() || ! juce::approximatelyEqual (scale, 1.0f))
            {
                lg.setFill (owner.isOpaque() ? underlay : juce::Colours::transparentBlack);
                lg.fillRect (compBounds, true);
                lg.setFill (juce::Colours::black);
            }

            owner.paintEntireComponent (imG, true);
            validArea = compBounds;
        }

        g.setColour (juce::Colours::black.withAlpha (owner.getAlpha()));

        if (juce::approximatelyEqual (scale, 1.0f))
        {
            g.drawImageAt (image, 0, 0);
        }
        else
        {
            juce::Graphics::ScopedSaveState state (g);
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale));
        }
    }

    /** The smallest step, in logical pixels, that is a whole number of
        physical pixels at the current scale: 4 at 125 %, 2 at 150 %. Scrolling
        in multiples of it lets scroll() reuse pixels on a scaled display.
        1 when no small step fits (an unusual scale such as 110 %). */
    int getWholePixelStep() const noexcept
    {
        for (int n = 1; n <= 8; ++n)
            if (isWholePhysical (n))
                return n;
        return 1;
    }

    bool invalidateAll() override
    {
        if (! keepContents)
            validArea.clear();
        return true;
    }

    bool invalidate (const juce::Rectangle<int>& area) override
    {
        if (! keepContents)
            validArea.subtract (area);
        return true;
    }

    void releaseResources() override                              { image = {}; }

    /** Moves the cached pixels inside `area` (component coordinates) by dx, dy,
        as scrolling that area does, and marks the part left uncovered as
        needing a repaint. Call repaintKeepingContents() afterwards so the
        window shows the result.

        Parts of `area` still waiting to be repainted move along with the
        pixels, so several scrolls between two paints all reuse pixels.

        Returns false and changes nothing when the pixels cannot be reused:
        nothing is cached yet, the image is not a software image (moving a
        Direct2D image's pixels means a GPU readback), the cache is for another
        size or scale, or on a scaled display dx or dy is not a whole number of
        physical pixels (see getWholePixelStep). Repaint the whole component then. */
    bool scroll (juce::Rectangle<int> area, int dx, int dy)
    {
        area = area.getIntersection (owner.getLocalBounds());

        if (area.isEmpty() || image.isNull() || ! imageIsSoftware
             || image.getBounds() != owner.getLocalBounds() * lastScale
             || ! isWholePhysical (dx) || ! isWholePhysical (dy))
            return false;

        const auto moved = area.getIntersection (area.translated (dx, dy));

        if (! moved.isEmpty())
        {
            // Physical pixels lying wholly inside the moved area.
            const auto src = (moved.translated (-dx, -dy).toFloat() * lastScale).getLargestIntegerWithin();
            const int pdx = juce::roundToInt ((float) dx * lastScale);
            const int pdy = juce::roundToInt ((float) dy * lastScale);
            image.moveImageSection (src.getX() + pdx, src.getY() + pdy, src.getX(), src.getY(),
                                    src.getWidth(), src.getHeight());
        }

        // Valid pixels inside the area moved with the content; whatever the
        // move uncovered is not valid.
        auto validInside = validArea;
        validInside.clipTo (area);
        validInside.offsetAll (dx, dy);
        validInside.clipTo (moved);

        validArea.subtract (area);
        validArea.add (validInside);

        // Scaled, the area's edges can cut through physical pixels that also
        // show what lies next to the area; those were not moved. Redraw a
        // logical pixel along each edge.
        if (! juce::approximatelyEqual (lastScale, 1.0f))
        {
            validArea.subtract (area.withHeight (1));
            validArea.subtract (area.withTrimmedTop (area.getHeight() - 1));
            validArea.subtract (area.withWidth (1));
            validArea.subtract (area.withTrimmedLeft (area.getWidth() - 1));
        }

        return true;
    }

    /** Marks an area as needing a repaint, like owner.repaint (area), without
        also asking the window to repaint it. */
    void invalidateOnly (juce::Rectangle<int> area)   { validArea.subtract (area); }

    /** Repaints the owner on screen without discarding cached pixels: only
        areas already marked invalid are rendered again. */
    void repaintKeepingContents()
    {
        const juce::ScopedValueSetter<bool> keep (keepContents, true);
        owner.repaint();
    }

private:
    bool isWholePhysical (int logical) const noexcept
    {
        const float physical = (float) logical * lastScale;
        return std::abs (physical - std::round (physical)) < 0.01f;
    }

    juce::Component& owner;
    juce::Image image;
    juce::RectangleList<int> validArea;
    float lastScale = 1.0f;
    juce::Colour underlay { juce::Colours::black };
    bool imageIsSoftware = false;
    bool keepContents = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CachedLayer)
};
