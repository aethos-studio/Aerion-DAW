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
//  - At 100 % scale the image is copied at whole pixels, which the software
//    renderer does as a plain memory copy.
//
// Install with: component.setCachedComponentImage (new CachedLayer (component));
//==============================================================================

/** True when `g` draws with JUCE's software renderer (vs Direct2D / CoreGraphics). */
inline bool isSoftwareContext (juce::Graphics& g)
{
    return dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) != nullptr;
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

    void paint (juce::Graphics& g) override
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const bool software = isSoftwareContext (g);

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

            if (! owner.isOpaque())
            {
                lg.setFill (juce::Colours::transparentBlack);
                lg.fillRect (compBounds, true);
                lg.setFill (juce::Colours::black);
            }

            owner.paintEntireComponent (imG, true);
            validArea = compBounds;
        }

        g.setColour (juce::Colours::black.withAlpha (owner.getAlpha()));

        if (juce::approximatelyEqual (scale, 1.0f))
            g.drawImageAt (image, 0, 0);
        else
            g.drawImageTransformed (image, juce::AffineTransform::scale ((float) compBounds.getWidth()  / (float) imageBounds.getWidth(),
                                                                          (float) compBounds.getHeight() / (float) imageBounds.getHeight()));
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
        Direct2D image's pixels means a GPU readback), or the display is
        scaled. Repaint the whole component then. */
    bool scroll (juce::Rectangle<int> area, int dx, int dy)
    {
        area = area.getIntersection (owner.getLocalBounds());

        if (area.isEmpty() || image.isNull() || ! imageIsSoftware
             || image.getBounds() != owner.getLocalBounds())
            return false;

        const auto moved = area.getIntersection (area.translated (dx, dy));

        if (! moved.isEmpty())
            image.moveImageSection (moved.getX(), moved.getY(), moved.getX() - dx, moved.getY() - dy,
                                    moved.getWidth(), moved.getHeight());

        // Valid pixels inside the area moved with the content; whatever the
        // move uncovered is not valid.
        auto validInside = validArea;
        validInside.clipTo (area);
        validInside.offsetAll (dx, dy);
        validInside.clipTo (moved);

        validArea.subtract (area);
        validArea.add (validInside);
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
    juce::Component& owner;
    juce::Image image;
    juce::RectangleList<int> validArea;
    bool imageIsSoftware = false;
    bool keepContents = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CachedLayer)
};
