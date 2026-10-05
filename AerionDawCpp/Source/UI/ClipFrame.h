#pragma once
#include <JuceHeader.h>
#include <map>
#include <tuple>
#include "ThemeTokens.h"
#include "CachedLayer.h"

//==============================================================================
// The body of a Timeline clip: drop shadow, vertical gradient, rounded outline
// and top highlight.
//
// Drawing that as paths costs about 0.1 ms per clip (anti-aliased rounded
// rectangles and strokes), which dominated full Timeline repaints. Every clip
// of the same colour, height and state looks the same apart from its width,
// and the middle of the frame is identical column after column, so
// ClipFrameCache renders each variant once and assembles clips of any width
// from it as a nine-slice: left cap, middle copied in chunks, right cap.
// All copies are unscaled at whole pixels, which is a plain blit. On a scaled
// display the variant is rendered at physical resolution and the slices are
// placed at whole physical pixels, so they are still plain copies.
//==============================================================================

/** Draws a clip frame with paths. The reference look; the cache renders from it. */
inline void paintClipFrameDirect (juce::Graphics& g, juce::Rectangle<float> cb,
                                  juce::Colour colour, bool selected, bool muted)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (cb.translated (0.0f, 1.0f), 6.0f);

    juce::ColourGradient grad (colour.brighter (0.28f), cb.getX(), cb.getY(),
                               colour.darker   (0.22f), cb.getX(), cb.getBottom(), false);
    grad.addColour (0.18, colour.brighter (0.35f));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (cb, 6.0f);

    g.setColour ((selected ? Theme::accent : Theme::border).withAlpha (selected ? 0.95f : 0.55f));
    g.drawRoundedRectangle (cb, 6.0f, selected ? 2.0f : 1.0f);

    // Top highlight
    g.setColour (juce::Colours::white.withAlpha (muted ? 0.06f : 0.12f));
    g.drawLine (cb.getX() + 2.0f, cb.getY() + 1.0f, cb.getRight() - 2.0f, cb.getY() + 1.0f, 1.0f);
}

class ClipFrameCache
{
public:
    /** Draws the frame for `cb`, which must lie on whole pixels. */
    void draw (juce::Graphics& g, juce::Rectangle<float> cb, juce::Colour colour, bool selected, bool muted)
    {
        const auto body = cb.toNearestInt();

        // Lightweight UI: a square, opaque body and a plain outline. Solid
        // rectangle fills are the cheapest thing any renderer draws.
        if (Theme::lightweightUi())
        {
            g.setColour (colour.withMultipliedBrightness (muted ? 0.6f : 1.0f));
            g.fillRect (body);
            g.setColour ((selected ? Theme::accent : Theme::border).withAlpha (selected ? 0.95f : 0.55f));
            g.drawRect (body, selected ? 2 : 1);
            return;
        }

        // Too narrow to split into caps and a middle: draw it directly.
        if (body.getWidth() < 2 * kCapW + 1)
        {
            paintClipFrameDirect (g, cb, colour, selected, muted);
            return;
        }

        const bool software = isSoftwareContext (g);
        const float scale = physicalScaleOf (g);

        if (! juce::approximatelyEqual (scale, 1.0f))
        {
            drawScaled (g, body, frameFor (colour, selected, muted, body.getHeight(), software, scale), scale);
            return;
        }

        const auto& image = frameFor (colour, selected, muted, body.getHeight(), software, 1.0f).image;

        const int imgH = image.getHeight();
        const int dy   = body.getY() - kMargin;
        const int capSrcW = kMargin + kCapW;

        // drawImage uses the current colour's opacity, which otherwise depends
        // on whatever was drawn before (and so on how much is being repainted).
        g.setOpacity (1.0f);

        // Left cap
        g.drawImage (image, body.getX() - kMargin, dy, capSrcW, imgH, 0, 0, capSrcW, imgH);

        // Middle, copied in chunks of the pre-rendered middle strip
        int x = body.getX() + kCapW;
        int remaining = body.getWidth() - 2 * kCapW;
        while (remaining > 0)
        {
            const int chunk = juce::jmin (kMiddleW, remaining);
            g.drawImage (image, x, dy, chunk, imgH, capSrcW, 0, chunk, imgH);
            x += chunk;
            remaining -= chunk;
        }

        // Right cap
        g.drawImage (image, body.getRight() - kCapW, dy, capSrcW, imgH, capSrcW + kMiddleW, 0, capSrcW, imgH);
    }

private:
    // Corner radius 6 plus up to a 2 px outline: the caps hold every curved pixel.
    static constexpr int kCapW    = 10;
    // Room outside the body for the outline's outer half, the 1 px drop shadow
    // and anti-aliasing.
    static constexpr int kMargin  = 3;
    static constexpr int kMiddleW = 128;
    static constexpr size_t kMaxVariants = 128;

    /** One rendered variant. Scaled, the image is in physical pixels and is
        cut at whole physical pixels into a left cap [0, middleX), a middle
        [middleX, rightX) and a right cap [rightX, width). */
    struct Frame
    {
        juce::Image image, left, middle, right;
        int middleX = 0, rightX = 0;
    };

    /** The scaled nine-slice: every slice is placed a whole number of
        physical pixels from the frame's top-left corner, so they meet exactly;
        the middle's last chunk is cut to end where the right cap starts. */
    static void drawScaled (juce::Graphics& g, juce::Rectangle<int> body, const Frame& frame, float scale)
    {
        const juce::Point<int> anchor (body.getX() - kMargin, body.getY() - kMargin);
        const int totalW   = juce::roundToInt ((float) (body.getWidth() + 2 * kMargin) * scale);
        const int rightAt  = totalW - frame.right.getWidth();
        const int middleW  = frame.middle.getWidth();

        drawPhysicalImage (g, frame.left, scale, anchor);

        for (int x = frame.middleX; x < rightAt; x += middleW)
        {
            const int chunk = juce::jmin (middleW, rightAt - x);
            drawPhysicalImage (g, chunk == middleW ? frame.middle
                                                   : frame.middle.getClippedImage ({ 0, 0, chunk, frame.middle.getHeight() }),
                               scale, anchor, { x, 0 });
        }

        drawPhysicalImage (g, frame.right, scale, anchor, { rightAt, 0 });
    }

    const Frame& frameFor (juce::Colour colour, bool selected, bool muted, int height, bool software, float scale)
    {
        const auto key = std::make_tuple (colour.getARGB(), selected, muted, height, software,
                                          juce::roundToInt (scale * 1000.0f));
        if (auto it = frames.find (key); it != frames.end())
            return it->second;

        if (frames.size() >= kMaxVariants)
            frames.clear();

        const int bodyW = 2 * kCapW + kMiddleW;
        auto physical = [scale] (int logical) { return juce::roundToInt ((float) logical * scale); };

        Frame frame;
        frame.image = makeImageFor (software, juce::Image::ARGB,
                                    physical (bodyW + 2 * kMargin), physical (height + 2 * kMargin + 1), true);
        {
            juce::Graphics ig (frame.image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            paintClipFrameDirect (ig, juce::Rectangle<float> ((float) kMargin, (float) kMargin, (float) bodyW, (float) height),
                                  colour, selected, muted);
        }

        const int imageH = frame.image.getHeight();
        frame.middleX = physical (kMargin + kCapW);
        frame.rightX  = physical (kMargin + kCapW + kMiddleW);
        frame.left    = frame.image.getClippedImage ({ 0, 0, frame.middleX, imageH });
        frame.middle  = frame.image.getClippedImage ({ frame.middleX, 0, frame.rightX - frame.middleX, imageH });
        frame.right   = frame.image.getClippedImage ({ frame.rightX, 0, frame.image.getWidth() - frame.rightX, imageH });

        return frames.emplace (key, std::move (frame)).first->second;
    }

    std::map<std::tuple<juce::uint32, bool, bool, int, bool, int>, Frame> frames;
};
