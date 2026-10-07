#include <JuceHeader.h>
#include "../UIComponents.h"
#include "../UI/GraphicsEngine.h"
#include "AudioBenchmark.h"
#include "BenchBaseline.h"

//==============================================================================
// Headless paint benchmark (Milestone 5).
//
// Renders the real Timeline and Mixer components into offscreen images so paint
// cost can be measured without a display, a mouse, or a human. Scenarios:
//
//   timeline full      - the whole component is invalidated (scroll, zoom, edit)
//   timeline playhead  - only a 16 px strip is invalidated, which is what
//                        MainComponent::timerCallback does during playback
//   mixer full / meters - full console vs the strip body repainted for meters
//   clip drag          - what one mouse move costs while dragging a clip:
//                        the Edit update plus the repaint of the clip area
//   piano roll         - a dense MIDI clip (--notes, --cc) open in the Piano
//                        Roll, repainted in full with no notes and with all
//                        notes selected (selection is drawn per key row)
//
// Each scenario runs once per renderer: "native" (Direct2D on Windows, which is
// what app windows use) and "software" (JUCE's CPU rasteriser). Direct2D work
// is submitted when the Graphics context ends; GPU execution that completes
// after that is not included, so native numbers are message-thread cost.
//
// The clip drag model update is synchronous cost only. With no audio device
// there is no playback graph, so the rebuild the app triggers after an edit is
// not captured here.
//
// Not registered with ctest: timings are machine-dependent, so this is a tool
// you run and read, not a pass/fail gate.
//
//   AerionBench --tracks=32 --clips=20 --frames=200 [--renderer=both|native|software]
//               [--notes=2000 --cc=1000 --pianoroll-height=600]
//               [--verify]      exit code 2 if a partial repaint differs from a full one
//               [--save-baseline=<file>] [--compare=<file>]
//                               keep this run's timings, or compare with kept ones;
//                               exit code 3 if anything is 20 % slower (BenchBaseline.h)
//               [--lightweight] measure with View -> Lightweight UI on
//               [--full-only]   only the full Timeline repaint (for the profile breakdown)
//               [--scale=1.25]  paint as a window at that display scale does (View -> UI
//                               Size times Windows scaling): physical-size images, clip
//                               regions rounded out to physical pixels
//   AerionBench --tracks=4 --clips=3 --snapshots=<dir>
//               renders an icon sheet and the toolbar, transport, menu bar, Timeline
//               and Mixer at 100 % and 150 % scale to PNGs, then exits
//   AerionBench --audio [--audio-tracks=32] [--audio-blocks=3000]
//               audio block timing, audio-thread allocations and graph rebuild
//               time instead of paint (see AudioBenchmark.cpp)
//==============================================================================

namespace
{
    // Display scale every scenario paints at (--scale). Images are physical
    // size; components keep their logical size.
    float gScale = 1.0f;

    int physical (int logical)   { return (int) std::ceil ((float) logical * gScale - 0.001f); }

    /** A logical region in physical pixels, rounded out the way a window
        turns Component::repaint() areas into areas of the screen to redraw. */
    juce::RectangleList<int> physicalRegion (const juce::RectangleList<int>& logical)
    {
        juce::RectangleList<int> out;
        for (auto& r : logical)
            out.add ((r.toFloat() * gScale).getSmallestIntegerContainer());
        return out;
    }

    juce::Image makeImage (int logicalW, int logicalH, const juce::ImageType& type)
    {
        return juce::Image (juce::Image::ARGB, physical (logicalW), physical (logicalH), true, type);
    }

    /** Paints `c` into `image` like a window does: clipped to `logicalRegion`
        rounded out to physical pixels, drawn through the display scale. */
    void paintInto (juce::Image& image, juce::Component& c, const juce::RectangleList<int>& logicalRegion)
    {
        juce::Graphics g (image);
        g.reduceClipRegion (physicalRegion (logicalRegion));
        if (! juce::approximatelyEqual (gScale, 1.0f))
            g.addTransform (juce::AffineTransform::scale (gScale));
        c.paintEntireComponent (g, false);
    }

    void paintInto (juce::Image& image, juce::Component& c)
    {
        paintInto (image, c, c.getLocalBounds());
    }

    int intArg (const juce::StringArray& args, juce::StringRef name, int fallback)
    {
        for (auto& a : args)
            if (a.startsWith (name))
                return a.fromFirstOccurrenceOf ("=", false, false).getIntValue();

        return fallback;
    }

    /** Writes a short silent wav so clips reference a real file on disk and the
        waveform/thumbnail paths behave like they do in the app. */
    juce::File createScratchAudioFile (double seconds)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("aerion_bench_source.wav");
        file.deleteFile();

        const double sampleRate = 44100.0;
        const int numSamples = (int) (seconds * sampleRate);

        juce::AudioBuffer<float> buffer (1, numSamples);
        buffer.clear();

        // A quiet sine rather than pure silence, so thumbnail rendering has
        // something to draw instead of a flat line.
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (0, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi
                                                        * 220.0f * (float) i / (float) sampleRate));

        juce::WavAudioFormat format;
        if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        {
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                    format.createWriterFor (out.get(), sampleRate, 1, 16, {}, 0)))
            {
                out.release();
                writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
            }
        }

        return file;
    }

    struct Result
    {
        double avgMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
    };

    juce::String stringArg (const juce::StringArray& args, juce::StringRef name)
    {
        for (auto& a : args)
            if (a.startsWith (name))
                return a.fromFirstOccurrenceOf ("=", false, false);

        return {};
    }

    /** Dumps a full repaint to disk. Two runs that should render identically
        (for example before and after a pure culling change) can then be compared
        byte-for-byte, which is the closest thing to a visual regression test
        available without a display. */
    bool writeFullRepaintPng (juce::Component& c, int width, int height, const juce::File& dest)
    {
        juce::Image image (juce::Image::ARGB, width, height, true);
        {
            juce::Graphics g (image);
            c.paintEntireComponent (g, false);
        }

        dest.deleteFile();
        juce::PNGImageFormat png;

        if (auto out = std::unique_ptr<juce::FileOutputStream> (dest.createOutputStream()))
            return png.writeImageToStream (image, *out);

        return false;
    }

    /** How far two renders of the same thing differ. Shapes at fractional
        positions can come out a level or two apart in their anti-aliased edge
        pixels, depending on where a repaint's clip edge cuts them or where on
        screen a curve was drawn; that is invisible. A real mistake (something
        missing, misplaced or drawn twice) differs by far more.

        On a scaled display, curves (rounded button corners) flatten slightly
        differently at different absolute positions, so a corner moved by a
        scroll and the same corner drawn in place can differ by about 15 levels
        in their anti-aliased pixels; tolerance() allows for that there. */
    struct Comparison
    {
        // Levels out of 255, per channel.
        static int tolerance()   { return juce::approximatelyEqual (gScale, 1.0f) ? 4 : 24; }

        int differing = 0;   // pixels that differ at all
        int visible = 0;     // pixels that differ by more than tolerance()
        juce::Rectangle<int> visibleBounds;   // where they are

        juce::String describe() const
        {
            if (visible > 0)
                return juce::String (visible) + " pixels differ in " + visibleBounds.toString();
            if (differing > 0)
                return "ok (" + juce::String (differing) + " px within " + juce::String (tolerance()) + " levels)";
            return "ok";
        }
    };

    Comparison compareImages (const juce::Image& a, const juce::Image& b, const juce::RectangleList<int>& region)
    {
        Comparison result;

        for (auto& r : region)
        {
            const auto area = r.getIntersection (a.getBounds());
            for (int y = area.getY(); y < area.getBottom(); ++y)
                for (int x = area.getX(); x < area.getRight(); ++x)
                {
                    const auto pa = a.getPixelAt (x, y), pb = b.getPixelAt (x, y);
                    if (pa == pb)
                        continue;

                    ++result.differing;
                    const int diff = juce::jmax (juce::jmax (std::abs (pa.getRed()   - pb.getRed()),
                                                             std::abs (pa.getGreen() - pb.getGreen())),
                                                 juce::jmax (std::abs (pa.getBlue()  - pb.getBlue()),
                                                             std::abs (pa.getAlpha() - pb.getAlpha())));
                    if (diff > Comparison::tolerance())
                    {
                        ++result.visible;
                        result.visibleBounds = result.visibleBounds.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1)
                                                                              : result.visibleBounds.getUnion ({ x, y, 1, 1 });
                    }
                }
        }

        return result;
    }

    /** Checks that painting only `region` gives the same pixels there as a full
        repaint. Culling in paint() must only skip work the clip would discard
        anyway, so any visible mismatch is a culling bug. Uses the software
        renderer so the comparison is deterministic. */
    Comparison checkPartialRepaint (juce::Component& c, int width, int height,
                                    const juce::RectangleList<int>& region)
    {
        const juce::SoftwareImageType software;
        auto full = makeImage (width, height, software);
        paintInto (full, c, juce::Rectangle<int> (width, height));

        const auto physicalArea = physicalRegion (region);
        auto partial = full.createCopy();
        for (auto& r : physicalArea)
            partial.clear (r);
        paintInto (partial, c, region);

        return compareImages (full, partial, physicalArea);
    }

    /** Sends a left-button press at `from`, a drag to `to` and a release, as
        the mouse would, straight to `c`'s handlers. */
    void dragMouse (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        auto event = [&] (juce::Point<float> pos, juce::ModifierKeys mods, bool dragged)
        {
            return juce::MouseEvent (source, pos, mods, juce::MouseInputSource::defaultPressure,
                                     juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                     juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &c, &c, now, from, now, 1, dragged);
        };
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        c.mouseDown (event (from, left, false));
        c.mouseDrag (event (to, left, true));
        c.mouseUp   (event (to, {}, true));
    }

    bool writePng (const juce::Image& image, const juce::File& dest)
    {
        dest.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = std::unique_ptr<juce::FileOutputStream> (dest.createOutputStream()))
            return png.writeImageToStream (image, *out);
        return false;
    }

    /** Scrolls the Timeline the way the app does, reusing its cached pixels
        (Timeline::scrollTo), and compares the result with a full repaint at
        the new position. Paints `layers`, the Timeline's parent, because only
        a parent paints a child from its cached layer. */
    struct ScrollCheck
    {
        bool copied = false;   // false: scrollTo fell back to a full repaint
        Comparison comparison;
    };

    /** stepsBeforePaint > 1 splits the scroll into that many scrollTo calls
        with no paint in between, as fast wheel events between two frames are. */
    ScrollCheck checkScrollByCopy (juce::Component& layers, Timeline& timeline,
                                   double dxPx, int dy, int stepsBeforePaint)
    {
        const int w = layers.getWidth(), h = layers.getHeight();
        const juce::SoftwareImageType software;

        auto viaCopy = makeImage (w, h, software);
        paintInto (viaCopy, layers);   // fills the cache at the old position

        ScrollCheck result;
        result.copied = true;
        for (int i = 0; i < stepsBeforePaint; ++i)
            result.copied = timeline.scrollTo (timeline.getScrollPx() + dxPx / stepsBeforePaint,
                                               timeline.getScrollY() + dy / stepsBeforePaint) && result.copied;
        paintInto (viaCopy, layers);

        timeline.repaint();   // drop the cache
        auto fresh = makeImage (w, h, software);
        paintInto (fresh, layers);

        result.comparison = compareImages (viaCopy, fresh, viaCopy.getBounds());

        // AERION_BENCH_DUMP=<dir>: keep both images of a failed check for inspection.
        if (result.comparison.visible > 0)
            if (auto dumpDir = juce::SystemStats::getEnvironmentVariable ("AERION_BENCH_DUMP", {}); dumpDir.isNotEmpty())
            {
                static int dumpIndex = 0;
                const juce::File dir (dumpDir);
                dir.createDirectory();
                ++dumpIndex;
                writePng (viaCopy, dir.getChildFile ("scroll" + juce::String (dumpIndex) + "_copied.png"));
                writePng (fresh,   dir.getChildFile ("scroll" + juce::String (dumpIndex) + "_fresh.png"));
            }
        return result;
    }


    /** Renders a component at a display scale factor (1.5 = 150 % Windows
        scaling), so layout and icon problems can be inspected without a window. */
    void writeSnapshot (juce::Component& c, float scale, const juce::File& dest)
    {
        juce::Image image (juce::Image::ARGB,
                           juce::roundToInt ((float) c.getWidth()  * scale),
                           juce::roundToInt ((float) c.getHeight() * scale),
                           true, juce::SoftwareImageType());
        {
            juce::Graphics g (image);
            g.fillAll (juce::Colour (0xff101418));
            g.addTransform (juce::AffineTransform::scale (scale));
            c.paintEntireComponent (g, false);
        }
        std::cout << "  " << (writePng (image, dest) ? "wrote " : "FAILED ") << dest.getFileName() << std::endl;
    }

    /** One row per embedded SVG: the icon fitted by its drawn content (what
        Drawable::drawWithin does) next to the icon fitted by its viewBox, at
        two button sizes, magnified. Also prints how far each icon's content
        is from its viewBox, which decides how differently the two look. */
    void writeIconSheet (const juce::File& dest)
    {
        constexpr int kZoom = 4, kRowH = 36, kLabelW = 190;
        const int sizes[] = { 14, 24 };

        struct Icon { juce::String name; std::unique_ptr<juce::Drawable> drawable; };
        std::vector<Icon> icons;

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String file (BinaryData::originalFilenames[i]);
            if (! file.endsWithIgnoreCase (".svg"))
                continue;

            int size = 0;
            auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size)))
                if (auto d = juce::Drawable::createFromSVG (*xml))
                    icons.push_back ({ file, std::move (d) });
        }

        std::cout << std::endl << juce::String ("icon").paddedRight (' ', 34)
                  << "viewBox            content bounds (x, y, w, h)" << std::endl;

        const int cellW = 40;
        juce::Image sheet (juce::Image::ARGB, (kLabelW + cellW * 4) * kZoom / 2,
                           ((int) icons.size() + 1) * kRowH * kZoom / 2, true, juce::SoftwareImageType());
        juce::Graphics g (sheet);
        g.fillAll (juce::Colour (0xff101418));
        g.addTransform (juce::AffineTransform::scale ((float) kZoom / 2.0f));

        g.setColour (juce::Colours::white);
        g.setFont (11.0f);
        g.drawText ("drawWithin (content)   |   fitted to viewBox", kLabelW, 4, cellW * 4, 14,
                    juce::Justification::left);

        int y = kRowH;
        for (auto& icon : icons)
        {
            auto* composite = dynamic_cast<juce::DrawableComposite*> (icon.drawable.get());
            const auto viewBox = composite != nullptr ? composite->getContentArea() : juce::Rectangle<float>();
            const auto content = icon.drawable->getDrawableBounds();

            std::cout << icon.name.paddedRight (' ', 34)
                      << (juce::String (viewBox.getWidth(), 1) + " x " + juce::String (viewBox.getHeight(), 1)).paddedRight (' ', 19)
                      << juce::String (content.getX(), 1) << ", " << juce::String (content.getY(), 1) << ", "
                      << juce::String (content.getWidth(), 1) << ", " << juce::String (content.getHeight(), 1)
                      << std::endl;

            g.setColour (juce::Colours::lightgrey);
            g.drawText (icon.name, 4, y, kLabelW - 8, kRowH, juce::Justification::centredLeft);

            int x = kLabelW;
            for (int method = 0; method < 2; ++method)
            {
                for (int s : sizes)
                {
                    const juce::Rectangle<float> box ((float) x + (float) (cellW - s) / 2.0f,
                                                      (float) y + (float) (kRowH - s) / 2.0f, (float) s, (float) s);
                    g.setColour (juce::Colour (0xff1e2630));
                    g.fillRect (box);
                    g.setColour (juce::Colours::orange.withAlpha (0.8f));
                    g.drawRect (box, 0.5f);

                    if (method == 0 || viewBox.isEmpty())
                        icon.drawable->drawWithin (g, box, juce::RectanglePlacement::centred, 1.0f);
                    else
                        icon.drawable->draw (g, 1.0f, juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                                          .getTransformToFit (viewBox, box));
                    x += cellW;
                }
            }
            y += kRowH;
        }

        std::cout << "  " << (writePng (sheet, dest) ? "wrote " : "FAILED ") << dest.getFileName() << std::endl;
    }

    double elapsedMsSince (juce::int64 startTicks)
    {
        return 1000.0 * (double) (juce::Time::getHighResolutionTicks() - startTicks)
                      / (double) juce::Time::getHighResolutionTicksPerSecond();
    }

    void accumulate (Result& r, double& total, double elapsedMs)
    {
        total += elapsedMs;
        r.minMs = juce::jmin (r.minMs, elapsedMs);
        r.maxMs = juce::jmax (r.maxMs, elapsedMs);
    }

    Result timePaint (juce::Component& c, int width, int height, int frames,
                      const juce::RectangleList<int>& clipRegion, const juce::ImageType& imageType)
    {
        auto image = makeImage (width, height, imageType);

        // One untimed pass so lazily-built caches (thumbnails, fonts) are warm
        // and the measured frames reflect steady state rather than first paint.
        paintInto (image, c, clipRegion);

        Result r;
        r.minMs = std::numeric_limits<double>::max();
        double total = 0.0;

        for (int i = 0; i < frames; ++i)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            paintInto (image, c, clipRegion);
            accumulate (r, total, elapsedMsSince (start));
        }

        r.avgMs = frames > 0 ? total / (double) frames : 0.0;
        return r;
    }

    /** Mirrors a mouse-wheel scroll: each frame scrolls the Timeline by dxPx
        or dy (alternating direction, so it stays in range) and repaints the
        window's view of it. With a software image the scroll moves the cached
        pixels and draws only the strip scrolled into view; with Direct2D it
        falls back to a full repaint. */
    Result timeScroll (juce::Component& layers, Timeline& timeline, int frames,
                       double dxPx, int dy, const juce::ImageType& imageType)
    {
        auto image = makeImage (layers.getWidth(), layers.getHeight(), imageType);
        paintInto (image, layers);

        Result r;
        r.minMs = std::numeric_limits<double>::max();
        double total = 0.0;

        for (int i = 0; i < frames; ++i)
        {
            const int sign = (i % 2 == 0) ? 1 : -1;
            const auto start = juce::Time::getHighResolutionTicks();
            timeline.scrollTo (timeline.getScrollPx() + sign * dxPx, timeline.getScrollY() + sign * dy);
            paintInto (image, layers);
            accumulate (r, total, elapsedMsSince (start));
        }

        r.avgMs = frames > 0 ? total / (double) frames : 0.0;
        return r;
    }

    /** Mirrors Timeline::mouseDrag in DragMode::move: every mouse event moves the
        clip in the live Edit, then repaints the union of the clip's old and new
        area. The two halves are timed separately so the report shows whether
        input delay comes from the model update or from the repaint after it. */
    struct DragResult
    {
        Result modelUpdate, repaint, total;
    };

    DragResult timeClipDrag (Timeline& timeline, tracktion::Clip& clip,
                             int width, int height, int frames, const juce::ImageType& imageType)
    {
        auto image = makeImage (width, height, imageType);
        const double originalStart = clip.getPosition().getStart().inSeconds();

        DragResult r;
        for (auto* res : { &r.modelUpdate, &r.repaint, &r.total })
            res->minMs = std::numeric_limits<double>::max();

        double modelTotal = 0.0, paintTotal = 0.0, allTotal = 0.0;

        for (int i = 0; i < frames; ++i)
        {
            // Sweep back and forth over ~2 s, like a user nudging a clip around.
            const double offset = 0.01 * (double) ((i % 200) < 100 ? (i % 100) : 100 - (i % 100));

            const auto oldArea = timeline.getClipPaintBounds (clip);

            const auto start = juce::Time::getHighResolutionTicks();
            clip.setStart (tracktion::TimePosition::fromSeconds (originalStart + offset), false, true);
            const double modelMs = elapsedMsSince (start);

            const auto paintStart = juce::Time::getHighResolutionTicks();
            paintInto (image, timeline, oldArea.getUnion (timeline.getClipPaintBounds (clip)));
            const double paintMs = elapsedMsSince (paintStart);

            accumulate (r.modelUpdate, modelTotal, modelMs);
            accumulate (r.repaint, paintTotal, paintMs);
            accumulate (r.total, allTotal, modelMs + paintMs);
        }

        clip.setStart (tracktion::TimePosition::fromSeconds (originalStart), false, true);

        if (frames > 0)
        {
            r.modelUpdate.avgMs = modelTotal / (double) frames;
            r.repaint.avgMs     = paintTotal / (double) frames;
            r.total.avgMs       = allTotal   / (double) frames;
        }

        return r;
    }

    // One display frame at 60 Hz: the target for anything that answers input
    // or animates (playhead, meters, drags).
    constexpr double kFrameBudgetMs = 1000.0 / 60.0;

    // The renderer being measured, which prefixes each timing's baseline name.
    juce::String gRendererName;

    void report (const juce::String& label, const Result& r)
    {
        BenchBaseline::record (gRendererName + ": " + label, r.avgMs, "ms");

        std::cout << label.paddedRight (' ', 30)
                  << juce::String (r.avgMs, 3).paddedLeft (' ', 9) << " ms avg"
                  << juce::String (r.minMs, 3).paddedLeft (' ', 9) << " ms min"
                  << juce::String (r.maxMs, 3).paddedLeft (' ', 9) << " ms max"
                  << juce::String (100.0 * r.avgMs / kFrameBudgetMs, 1).paddedLeft (' ', 9) << " % of 60 Hz frame"
                  << std::endl;
    }

    struct Renderer
    {
        juce::String name;
        std::unique_ptr<juce::ImageType> type;
    };

    /** "native" is what JUCE 8 uses for windows on Windows (Direct2D, so the GPU
        or its WARP software fallback); "software" is JUCE's CPU rasteriser, the
        candidate for machines where Direct2D is slow. */
    std::vector<Renderer> renderersFromArgs (const juce::String& arg)
    {
        std::vector<Renderer> list;
        const bool both = arg.isEmpty() || arg == "both";

        if (both || arg == "native")
            list.push_back ({ "native", std::make_unique<juce::NativeImageType>() });

        if (both || arg == "software")
            list.push_back ({ "software", std::make_unique<juce::SoftwareImageType>() });

        return list;
    }

    /** Sends one mouse-wheel event to `c`, as the mouse would. */
    void wheel (juce::Component& c, float deltaY)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto pos = c.getLocalBounds().getCentre().toFloat();
        const juce::MouseEvent e (source, pos, {}, juce::MouseInputSource::defaultPressure,
                                  juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                  juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                  &c, &c, now, pos, now, 0, false);
        juce::MouseWheelDetails details;
        details.deltaX = 0.0f;
        details.deltaY = deltaY;
        details.isReversed = false;
        details.isSmooth = false;
        details.isInertial = false;
        c.mouseWheelMove (e, details);
    }

    /** A dense MIDI clip open in the Piano Roll: `numNotes` notes between C2
        and C6 and `numCC` CC1 events over 64 bars, scrolled so the notes fill
        the view. It sits on a MIDI track of its own, deleted again afterwards,
        so the Timeline and Mixer scenarios measure the same project with or
        without it. */
    struct PianoRollScene
    {
        PianoRollScene (AudioEngineManager& ae, ProjectData& pd, int numNotes, int numCC, int width, int height)
            : audioEngine (ae)
        {
            namespace te = tracktion;
            track = ae.addMidiTrack();
            if (track == nullptr)
                return;

            constexpr double beats = 64.0 * 4.0;
            auto& tempo = ae.getEdit().tempoSequence;
            clip = track->insertMIDIClip ({ te::TimePosition(), tempo.toTime (te::BeatPosition::fromBeats (beats)) }, nullptr);
            if (clip == nullptr)
                return;

            auto& seq = clip->getSequence();
            juce::Random rng (42);   // the same clip every run
            for (int i = 0; i < numNotes; ++i)
                seq.addNote (36 + rng.nextInt (49), te::BeatPosition::fromBeats (beats * i / numNotes),
                             te::BeatDuration::fromBeats (0.25 * (1 + rng.nextInt (4))), 40 + rng.nextInt (88), 0, nullptr);
            for (int i = 0; i < numCC; ++i)
                seq.addControllerEvent (te::BeatPosition::fromBeats (beats * i / numCC), 1, rng.nextInt (128), nullptr);

            editor = std::make_unique<PianoRollEditor> (*clip, ae.getEdit(), pd, ae);
            editor->setBounds (0, 0, width, height);
            wheel (*editor, 18.0f);   // from near C1, where it opens, up to C6 at the top
        }

        ~PianoRollScene()
        {
            editor.reset();
            if (track != nullptr)
                audioEngine.deleteTrack (track);
        }

        bool isReady() const   { return editor != nullptr; }

        /** Ctrl / Cmd + A, through the editor's own shortcut handling. */
        bool selectAll()
        {
            return editor->keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
        }

        AudioEngineManager& audioEngine;
        tracktion::AudioTrack* track = nullptr;
        tracktion::MidiClip* clip = nullptr;
        std::unique_ptr<PianoRollEditor> editor;
    };
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // A crash in a tool run from scripts is otherwise just an exit code.
    juce::SystemStats::setApplicationCrashHandler ([] (void*)
    {
        std::cerr << "AerionBench crashed:\n" << juce::SystemStats::getStackBacktrace() << std::endl;
    });

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (argv[i]));

    if (args.contains ("--audio"))
        return BenchBaseline::finish (args, runAudioBenchmark (args));

    // Flat fills instead of decorative gradients, as View -> Lightweight UI does.
    Theme::lightweightUi() = args.contains ("--lightweight");

    if (auto scaleArg = stringArg (args, "--scale"); scaleArg.isNotEmpty())
        gScale = juce::jlimit (0.5f, 4.0f, scaleArg.getFloatValue());

    const int numTracks    = intArg (args, "--tracks", 32);
    const int clipsPerTrack = intArg (args, "--clips",  20);
    const int frames       = intArg (args, "--frames", 200);
    const int width        = intArg (args, "--width",  1920);
    const int height       = intArg (args, "--height", 1080);
    const int numNotes     = intArg (args, "--notes",  2000);
    const int numCC        = intArg (args, "--cc",     1000);
    const int pianoRollH   = intArg (args, "--pianoroll-height", 600);

    std::cout << "Aerion Timeline paint benchmark\n"
              << "  tracks=" << numTracks
              << " clips/track=" << clipsPerTrack
              << " frames=" << frames
              << " size=" << width << "x" << height
              << " scale=" << gScale << std::endl;

    AudioEngineManager audioEngine;
    ProjectData projectData;

    // Unsaved-changes tracking needs the message loop: deferred audio device
    // init and Tracktion's change listener both arrive through it, and edits are
    // only noticed through asynchronous undo-manager notifications, which stop
    // being delivered once the loop has been told to quit. So the checks run
    // inside the loop, two seconds into startup, and the loop stops afterwards.
    // This opens the audio device, which is why it lives here, not in AerionTests.
    int startupFailures = 0;
    if (args.contains ("--verify"))
    {
        juce::Timer::callAfterDelay (2000, [&audioEngine, &startupFailures]
        {
            auto check = [&startupFailures, &audioEngine] (const juce::String& name, bool ok)
            {
                std::cout << "  " << name.paddedRight (' ', 40) << (ok ? "ok" : "FAILED") << std::endl;
                // Failed once in about 40 runs and has not been reproduced since;
                // say what state the engine was in if it happens again.
                if (! ok)
                    std::cout << "    undo history: " << (audioEngine.getEdit().getUndoManager().canUndo() ? "yes" : "empty")
                              << ", audio devices: " << (audioEngine.areAudioDevicesConnected() ? "open" : "not open")
                              << std::endl;
                startupFailures += ok ? 0 : 1;
            };

            // Deliver pending undo-manager notifications now instead of waiting
            // for the loop to get to them.
            auto flush = [&audioEngine] { audioEngine.getEdit().getUndoManager().dispatchPendingMessages(); };

            check ("fresh project after startup is saved", ! audioEngine.hasUnsavedEdits());

            auto* track = audioEngine.addAudioTrack();
            flush();
            check ("adding a track marks it unsaved", audioEngine.hasUnsavedEdits());

            audioEngine.markEditSaved();
            check ("saving marks it saved", ! audioEngine.hasUnsavedEdits());

            // A direct Edit change, as a Timeline clip drag makes, bypasses
            // AudioEngineManager's broadcasts but must still count.
            auto clipFile = createScratchAudioFile (1.0);
            if (auto* clip = audioEngine.insertAudioClipOnTrack (track, clipFile, 0.0))
            {
                flush();
                audioEngine.markEditSaved();
                clip->setStart (tracktion::TimePosition::fromSeconds (2.0), false, true);
                flush();
                check ("moving a clip marks it unsaved", audioEngine.hasUnsavedEdits());
            }

            audioEngine.deleteTrack (track);
            flush();
            audioEngine.markEditSaved();

            juce::MessageManager::getInstance()->stopDispatchLoop();
        });

        juce::MessageManager::getInstance()->runDispatchLoop();
    }

    auto sourceFile = createScratchAudioFile (2.0);
    if (! sourceFile.existsAsFile())
    {
        std::cout << "Could not create scratch audio file; aborting." << std::endl;
        return 1;
    }

    for (int t = 0; t < numTracks; ++t)
    {
        auto* track = audioEngine.addAudioTrack();
        if (track == nullptr)
            continue;

        for (int c = 0; c < clipsPerTrack; ++c)
            audioEngine.insertAudioClipOnTrack (track, sourceFile, c * 2.5);
    }

    projectData.syncWithEngine (audioEngine.getEdit());

    std::cout << "  built " << audioEngine.getAudioTracks().size() << " tracks, "
              << audioEngine.getAudioTracks().size() * clipsPerTrack << " clips" << std::endl;

    Timeline timeline (audioEngine, projectData);
    timeline.setBounds (0, 0, width, height);

    // The app's layering: the Timeline as a cached layer with the playhead
    // overlay above it, both children of one parent (MainComponent in the app).
    // Painting the Timeline directly, as the other scenarios do, bypasses the
    // cache and measures Timeline::paint itself.
    juce::Component layers;
    TimelinePlayheadOverlay playheadOverlay (timeline, audioEngine);
    layers.setBounds (0, 0, width, height);
    layers.addAndMakeVisible (timeline);
    layers.addAndMakeVisible (playheadOverlay);
    playheadOverlay.setBounds (0, 0, width, height);

    if (auto pngPath = stringArg (args, "--png"); pngPath.isNotEmpty())
    {
        const juce::File dest (pngPath);
        const bool ok = writeFullRepaintPng (timeline, width, height, dest);
        std::cout << (ok ? "  wrote " : "  FAILED to write ") << dest.getFullPathName() << std::endl;
    }

    // The docked console in the app is roughly this tall at 1080p.
    const int mixerHeight = intArg (args, "--mixer-height", 320);
    Mixer mixer (audioEngine, projectData);
    mixer.setBounds (0, 0, width, mixerHeight);

    // The playback region depends on the layout the Mixer records while
    // painting, just like in the app after its first paint.
    {
        juce::Image scratch (juce::Image::ARGB, width, mixerHeight, true, juce::SoftwareImageType());
        juce::Graphics g (scratch);
        mixer.paintEntireComponent (g, false);
    }
    // Same region Mixer::repaintStripMetersArea() invalidates on each playback tick.
    const auto mixerMeters = mixer.getPlaybackRepaintRegion();

    if (args.contains ("--verify"))
    {
        std::cout << "[verify mixer faders follow the mouse]" << std::endl;
        juce::Array<tracktion::Track*> faderTracks;
        if (auto tracks = audioEngine.getAudioTracks(); ! tracks.isEmpty())
            faderTracks.add (tracks.getFirst());
        faderTracks.add (audioEngine.getMasterTrack());

        // A press jumps the fader to the pointer, so drag to the middle first,
        // then 40 px up, which must be louder. Each value is read after the
        // message loop has run for a while, so anything that writes the volume
        // back has had its chance.
        auto runLoopFor = [] (int ms)
        {
            juce::Timer::callAfterDelay (ms, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
            juce::MessageManager::getInstance()->runDispatchLoop();
        };

        for (bool playing : { false, true })
        {
            if (playing) { audioEngine.play(); runLoopFor (300); }

            for (auto* t : faderTracks)
            {
                const auto area = mixer.getFaderArea (t);
                const float originalDb = audioEngine.getTrackVolumeDb (t);
                const auto mid = area.getCentre().toFloat();

                dragMouse (mixer, mid, mid);
                runLoopFor (500);
                const float midDb = audioEngine.getTrackVolumeDb (t);
                dragMouse (mixer, mid, mid.translated (0.0f, -40.0f));
                runLoopFor (500);
                const float upDb = audioEngine.getTrackVolumeDb (t);

                const bool ok = ! area.isEmpty() && upDb > midDb + 1.0f;
                const juce::String name = juce::String (t->isMasterTrack() ? "master fader" : "track fader")
                                        + (playing ? ", playing" : ", stopped");
                std::cout << "  " << name.paddedRight (' ', 28) << (ok ? "ok" : "FAILED")
                          << " (" << midDb << " dB -> " << upDb << " dB)" << std::endl;
                startupFailures += ok ? 0 : 1;
                audioEngine.setTrackVolumeDb (t, originalDb);
            }

            if (playing) audioEngine.stop();
        }

        // A track whose volume has automation follows its curve on every block;
        // dragging its fader must still move it (the drag overrides the curve).
        if (auto tracks = audioEngine.getAudioTracks(); ! tracks.isEmpty())
        {
            auto* t = tracks.getFirst();
            auto* vol = audioEngine.getAutomationParam (t, AudioEngineManager::AutomationParamKind::Volume);
            auto& curve = vol->getCurve();
            const float originalDb = audioEngine.getTrackVolumeDb (t);
            const float minus20 = std::exp ((-20.0f - 6.0f) / 20.0f);
            curve.addPoint (tracktion::TimePosition::fromSeconds (0.0), minus20, 0.0f);
            curve.addPoint (tracktion::TimePosition::fromSeconds (600.0), minus20, 0.0f);

            audioEngine.play();
            runLoopFor (300);

            const auto area = mixer.getFaderArea (t);
            const auto mid = area.getCentre().toFloat();
            dragMouse (mixer, mid, mid.translated (0.0f, -40.0f));
            runLoopFor (500);
            const float draggedDb = audioEngine.getTrackVolumeDb (t);
            const bool moved = draggedDb > -19.0f && audioEngine.isTrackAutomationOverridden (t);

            audioEngine.reenableTrackAutomation (t);
            runLoopFor (300);
            const float backDb = audioEngine.getTrackVolumeDb (t);
            const bool restored = std::abs (backDb + 20.0f) < 0.5f;
            audioEngine.stop();

            std::cout << "  " << juce::String ("automated track fader, playing").paddedRight (' ', 32)
                      << (moved ? "ok" : "FAILED") << " (" << draggedDb << " dB)" << std::endl;
            std::cout << "  " << juce::String ("re-enabled automation").paddedRight (' ', 32)
                      << (restored ? "ok" : "FAILED") << " (" << backDb << " dB)" << std::endl;
            startupFailures += (moved ? 0 : 1) + (restored ? 0 : 1);

            curve.clear();
            audioEngine.setTrackVolumeDb (t, originalDb);
        }

        // Dragging a lane's bottom edge in the header column resizes the
        // track, within its limits, and counts as a change to the project.
        // Every painted control has an accessible stand-in with a title and a
        // role, and the keyboard works it like the mouse does.
        std::cout << "[verify accessibility]" << std::endl;
        {
            auto check = [&] (const juce::String& name, bool ok, const juce::String& detail)
            {
                std::cout << "  " << name.paddedRight (' ', 32) << (ok ? "ok" : "FAILED") << " (" << detail << ")" << std::endl;
                startupFailures += ok ? 0 : 1;
            };

            auto allLabelled = [] (const Accessibility::ProxyPool& pool)
            {
                bool ok = pool.size() > 0;
                pool.forEach ([&] (Accessibility::Proxy& p)
                {
                    // Headless there is no window, so getAccessibilityHandler() returns
                    // nothing; build the handler a screen reader would get.
                    auto h = p.createAccessibilityHandler();
                    ok = ok && h != nullptr && p.getTitle().isNotEmpty()
                            && h->getRole() != juce::AccessibilityRole::unspecified;
                });
                return ok;
            };

            mixer.repaint();
            { juce::Image img (juce::Image::ARGB, mixer.getWidth(), mixer.getHeight(), true); juce::Graphics g (img); mixer.paintEntireComponent (g, false); }
            mixer.syncAccessibleControlsNow();
            check ("mixer controls labelled", allLabelled (mixer.getAccessibleControls()),
                   juce::String (mixer.getAccessibleControls().size()) + " controls");

            { juce::Image img (juce::Image::ARGB, timeline.getWidth(), timeline.getHeight(), true); juce::Graphics g (img); timeline.paintEntireComponent (g, false); }
            timeline.syncAccessibleControlsNow();
            check ("timeline header controls labelled", allLabelled (timeline.getAccessibleControls()),
                   juce::String (timeline.getAccessibleControls().size()) + " controls");

            Transport transportBar (audioEngine, projectData);
            transportBar.setSize (width, 56);
            check ("transport controls labelled", allLabelled (transportBar.getAccessibleControls()),
                   juce::String (transportBar.getAccessibleControls().size()) + " controls");

            DAWToolbar toolbarBar;
            toolbarBar.setSize (width, 40);
            check ("toolbar controls labelled", allLabelled (toolbarBar.getAccessibleControls()),
                   juce::String (toolbarBar.getAccessibleControls().size()) + " controls");

            if (auto tracks = audioEngine.getAudioTracks(); ! tracks.isEmpty())
            {
                auto* t = tracks.getFirst();
                const auto id = t->itemID.toString();

                if (auto* fader = mixer.getAccessibleControls().find (id + ":fader"))
                {
                    audioEngine.setTrackVolumeDb (t, -6.0f);
                    fader->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                    const float db = audioEngine.getTrackVolumeDb (t);
                    check ("Up on a fader adds 0.5 dB", std::abs (db + 5.5f) < 0.05f, juce::String (db, 2) + " dB");
                    audioEngine.setTrackVolumeDb (t, 0.0f);
                }
                else check ("Up on a fader adds 0.5 dB", false, "no fader control");

                if (auto* mute = mixer.getAccessibleControls().find (id + ":mute"))
                {
                    const bool before = t->isMuted (false);
                    mute->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
                    const bool after = t->isMuted (false);
                    check ("Space on Mute toggles mute", after != before, after ? "muted" : "unmuted");
                    if (after != before)
                        audioEngine.toggleTrackMute (t);
                }
                else check ("Space on Mute toggles mute", false, "no mute control");
            }
        }

        std::cout << "[verify track resize]" << std::endl;
        if (auto rows = timeline.getVisibleRows(); ! rows.isEmpty())
        {
            auto* track = rows.getFirst().track;
            auto edgeY = [&] { return (float) (Timeline::kRulerH + rows.getFirst().y + Timeline::getLaneHeight (track)
                                               - timeline.getScrollY()); };
            audioEngine.markEditSaved();

            const juce::Point<float> start (100.0f, edgeY());
            dragMouse (timeline, start, start.translated (0.0f, 40.0f));
            const int grown = Timeline::getLaneHeight (track);
            const bool marked = audioEngine.hasUnsavedEdits();

            const juce::Point<float> again (100.0f, edgeY());
            dragMouse (timeline, again, again.translated (0.0f, -500.0f));
            const int shrunk = Timeline::getLaneHeight (track);

            auto check = [&] (const juce::String& name, bool ok, const juce::String& detail)
            {
                std::cout << "  " << name.paddedRight (' ', 32) << (ok ? "ok" : "FAILED") << " (" << detail << ")" << std::endl;
                startupFailures += ok ? 0 : 1;
            };
            check ("drag edge down 40 px", grown == Timeline::kTrackH + 40, juce::String (grown) + " px");
            check ("drag far up stops at minimum", shrunk == Timeline::kMinTrackH, juce::String (shrunk) + " px");
            check ("resize marks project unsaved", marked, marked ? "yes" : "no");

            timeline.setLaneHeights ({ track }, Timeline::kTrackH);
        }
    }

    // A clip in the middle of the arrangement, so a drag touches a typical row.
    tracktion::Clip* dragClip = nullptr;
    {
        auto tracks = audioEngine.getAudioTracks();
        if (! tracks.isEmpty())
        {
            auto& clips = tracks[tracks.size() / 2]->getClips();
            if (! clips.isEmpty())
                dragClip = clips[clips.size() / 2];
        }
    }

    if (auto dirArg = stringArg (args, "--snapshots"); dirArg.isNotEmpty())
    {
        const juce::File dir (dirArg);
        dir.createDirectory();
        writeIconSheet (dir.getChildFile ("icons.png"));

        // Some "on" states, so active button styling shows up too.
        auto tracks = audioEngine.getAudioTracks();
        if (tracks.size() > 1) audioEngine.toggleTrackMute (tracks[0]);
        if (tracks.size() > 2) audioEngine.toggleTrackSolo (tracks[1]);
        // A Small and a Large track, to see the header layouts.
        if (tracks.size() > 3)
        {
            timeline.setLaneHeights ({ tracks[2] }, Timeline::kMinTrackH);
            timeline.setLaneHeights ({ tracks[3] }, 140);
        }

        DAWMenuBar menuBar;
        DAWToolbar toolbar;
        AboutDialog about;
        Inspector inspector (audioEngine, projectData);
        inspector.setBounds (0, 0, 240, 360);
        Transport transport (audioEngine, projectData);
        menuBar.setBounds (0, 0, 1400, 28);
        toolbar.setBounds (0, 0, 1400, 40);
        transport.setBounds (0, 0, 1400, 60);
        timeline.setBounds (0, 0, 1100, 420);
        mixer.setBounds (0, 0, 1100, 320);

        PianoRollScene pianoRoll (audioEngine, projectData, 400, 200, 1100, 520);

        for (float scale : { 1.0f, 1.5f })
        {
            const auto suffix = scale > 1.0f ? juce::String ("_150.png") : juce::String ("_100.png");
            if (pianoRoll.isReady())
                writeSnapshot (*pianoRoll.editor, scale, dir.getChildFile ("pianoroll" + suffix));
        }

        // With every note selected: highlighted rows and keys, selected note outlines.
        if (pianoRoll.isReady() && pianoRoll.selectAll())
            for (float scale : { 1.0f, 1.5f })
                writeSnapshot (*pianoRoll.editor, scale, dir.getChildFile (scale > 1.0f ? "pianoroll_selected_150.png"
                                                                                        : "pianoroll_selected_100.png"));

        for (float scale : { 1.0f, 1.5f })
        {
            const auto suffix = scale > 1.0f ? juce::String ("_150.png") : juce::String ("_100.png");
            writeSnapshot (menuBar,   scale, dir.getChildFile ("menubar"   + suffix));
            writeSnapshot (about,     scale, dir.getChildFile ("about"     + suffix));
            writeSnapshot (inspector, scale, dir.getChildFile ("inspector" + suffix));
            writeSnapshot (toolbar,   scale, dir.getChildFile ("toolbar"   + suffix));
            writeSnapshot (transport, scale, dir.getChildFile ("transport" + suffix));
            writeSnapshot (timeline,  scale, dir.getChildFile ("timeline"  + suffix));
            writeSnapshot (mixer,     scale, dir.getChildFile ("mixer"     + suffix));
        }

        sourceFile.deleteFile();
        return 0;
    }

    const int strip = 16;
    const juce::Rectangle<int> fullArea (0, 0, width, height);
    const juce::Rectangle<int> playheadStrip (width / 2 - strip / 2, 0, strip, height);

    // Put the playhead inside that strip so the overlay really draws there.
    audioEngine.setTransportPosition (timeline.xToTime ((float) (width / 2)));

    if (args.contains ("--verify"))
    {
        std::cout << std::endl << "[verify partial repaints match a full repaint]" << std::endl;

        struct Check
        {
            juce::String name;
            juce::Component* component;
            int height;
            juce::RectangleList<int> region;
        };

        juce::Array<Check> checks;
        checks.add ({ "timeline playhead 16px", &timeline, height, playheadStrip });
        checks.add ({ "layered playhead strip", &layers, height, playheadStrip });
        checks.add ({ "timeline header column", &timeline, height, juce::Rectangle<int> (0, 0, Timeline::kHeaderWidth, height) });

        if (dragClip != nullptr)
            checks.add ({ "timeline dragged clip", &timeline, height, timeline.getClipPaintBounds (*dragClip) });

        checks.add ({ "mixer meters", &mixer, mixerHeight, mixerMeters });
        // An arbitrary slice across strips, like a window uncovering part of the
        // console: every section it touches must redraw identically.
        checks.add ({ "mixer arbitrary slice", &mixer, mixerHeight, juce::Rectangle<int> (100, 60, 300, 90) });

        int failures = startupFailures;

        // Scrolling moves the Timeline's cached pixels and draws only what is
        // scrolled into view; the result must look the same as a full repaint.
        // See Comparison for the tolerance.
        {
            // Things drawn at fractional positions: a MIDI clip and an automation curve.
            auto tracks = audioEngine.getAudioTracks();
            if (tracks.size() > 3)
            {
                if (auto midi = tracks[1]->insertMIDIClip ({ tracktion::TimePosition::fromSeconds (1.3),
                                                             tracktion::TimePosition::fromSeconds (9.1) }, nullptr))
                    for (int i = 0; i < 24; ++i)
                        midi->getSequence().addNote (48 + (i * 7) % 24, tracktion::BeatPosition::fromBeats (i * 0.6),
                                                     tracktion::BeatDuration::fromBeats (0.45), 100, 0, nullptr);

                if (auto* param = audioEngine.getAutomationParam (tracks[2], AudioEngineManager::AutomationParamKind::Volume))
                    for (int i = 0; i < 12; ++i)
                        param->getCurve().addPoint (tracktion::TimePosition::fromSeconds (0.7 + i * 1.37f),
                                                    0.2f + 0.6f * (float) ((i * 5) % 7) / 6.0f, 0.0f);

                timeline.showAutomationLane (*tracks[2]);

                // Rows of different heights, as resized tracks give.
                timeline.setLaneHeights ({ tracks[2] }, 140);
                timeline.setLaneHeights ({ tracks[3] }, Timeline::kMinTrackH);
            }

            struct Step { const char* name; double dxPx; int dy; int stepsBeforePaint; };
            const Step steps[] = { { "scroll right 37 px",   37.0,  0,   1 },
                                   { "scroll left 113 px",  -113.0, 0,   1 },
                                   { "scroll right 1 px",    1.0,   0,   1 },
                                   { "scroll down 60 px",    0.0,   60,  1 },
                                   { "scroll up 23 px",      0.0,  -23,  1 },
                                   { "3 x 20 px, one paint", 60.0,  0,   3 },
                                   { "3 x 30 px down, 1 paint", 0.0, 90, 3 },
                                   { "scroll past a screen", 3000.0, 0,  1 } };

            timeline.scrollTo (200.0, 0);   // away from the left limit, so scrolling left works

            // Scaled, scrolls move in steps of whole physical pixels (4 px at
            // 125 %); the Timeline learns the scale from its first paint.
            {
                auto image = makeImage (width, height, juce::SoftwareImageType());
                paintInto (image, layers);
            }
            const int unit = timeline.scrollStep();

            for (auto& step : steps)
            {
                const auto r = checkScrollByCopy (layers, timeline, step.dxPx * unit, step.dy * unit, step.stepsBeforePaint);
                const bool ok = r.copied && r.comparison.visible == 0;
                std::cout << "  " << juce::String (step.name).paddedRight (' ', 28)
                          << (r.copied ? r.comparison.describe() : juce::String ("NOT COPIED, repainted"))
                          << std::endl;
                failures += ok ? 0 : 1;
            }

            timeline.scrollTo (0.0, 0);
        }

        // With snap on, the razor's hairline sits on the grid, not under the
        // mouse: each move must clear the line where it was drawn, or old
        // lines stay in the cached layer until something repaints everything.
        {
            timeline.activeTool = EditTool::razor;
            projectData.getProjectTree().setProperty (IDs::snapEnabled, true, nullptr);

            auto source = juce::Desktop::getInstance().getMainMouseSource();
            auto moveTo = [&] (int x)
            {
                const juce::Point<float> p ((float) (Timeline::kHeaderWidth + x), (float) (Timeline::kRulerH + 40));
                const auto now = juce::Time::getCurrentTime();
                timeline.mouseMove (juce::MouseEvent (source, p, {}, juce::MouseInputSource::defaultPressure,
                                                      juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                                      juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                                      &timeline, &timeline, now, p, now, 0, false));
            };

            const juce::SoftwareImageType software;
            moveTo (300);
            timeline.repaint();
            auto viaCache = makeImage (layers.getWidth(), layers.getHeight(), software);
            paintInto (viaCache, layers);

            for (int x : { 337, 371, 402, 455, 517, 263 })
            {
                moveTo (x);
                paintInto (viaCache, layers);
            }

            timeline.repaint();
            auto fresh = makeImage (layers.getWidth(), layers.getHeight(), software);
            paintInto (fresh, layers);

            const auto cmp = compareImages (viaCache, fresh, viaCache.getBounds());
            std::cout << "  " << juce::String ("razor line, snap on").paddedRight (' ', 28) << cmp.describe() << std::endl;
            failures += cmp.visible > 0 ? 1 : 0;

            timeline.activeTool = EditTool::select;
            timeline.repaint();
        }

        // A hidden window must follow the graphics engine choice both ways.
        {
            juce::Component probe;
            probe.setSize (64, 64);
            probe.addToDesktop (0);

            if (auto* peer = probe.getPeer())
            {
                for (auto choice : { GraphicsEngine::Choice::software, GraphicsEngine::Choice::hardware })
                {
                    GraphicsEngine::applyToAllWindows (choice);
                    const auto engine = peer->getAvailableRenderingEngines()[peer->getCurrentRenderingEngine()];
                    const bool ok = engine.containsIgnoreCase ("software") == (choice == GraphicsEngine::Choice::software);
                    std::cout << "  " << juce::String ("graphics engine -> " + engine).paddedRight (' ', 40)
                              << (ok ? "ok" : "WRONG ENGINE") << std::endl;
                    failures += ok ? 0 : 1;
                }
            }

            probe.removeFromDesktop();
        }

        for (auto& check : checks)
        {
            const auto& name = check.name;
            const auto result = checkPartialRepaint (*check.component, width, check.height, check.region);
            std::cout << "  " << name.paddedRight (' ', 28) << result.describe() << std::endl;
            failures += result.visible > 0 ? 1 : 0;
        }

        // The Piano Roll repaints in full itself, but a window uncovering part
        // of it repaints only that part. With every note selected, the key and
        // row highlights are drawn too.
        {
            PianoRollScene scene (audioEngine, projectData, numNotes, numCC, width, pianoRollH);
            if (! scene.isReady() || ! scene.selectAll())
            {
                std::cout << "  " << juce::String ("piano roll").paddedRight (' ', 28) << "FAILED to set up" << std::endl;
                ++failures;
            }
            else
            {
                const juce::Rectangle<int> slices[] = { { 20, 90, 320, 140 },                       // keys and grid
                                                        { 200, pianoRollH - 190, 400, 176 },        // CC and velocity lanes
                                                        { width - 30, 40, 30, pianoRollH - 60 } };  // right edge, scrollbar
                const char* names[] = { "piano roll keys and grid", "piano roll lower lanes", "piano roll right edge" };

                for (int i = 0; i < 3; ++i)
                {
                    const auto result = checkPartialRepaint (*scene.editor, width, pianoRollH, slices[i]);
                    std::cout << "  " << juce::String (names[i]).paddedRight (' ', 28) << result.describe() << std::endl;
                    failures += result.visible > 0 ? 1 : 0;
                }
            }
        }

        if (failures > 0)
        {
            sourceFile.deleteFile();
            return 2;
        }
    }

    for (auto& renderer : renderersFromArgs (stringArg (args, "--renderer")))
    {
        std::cout << std::endl << "[" << renderer.name << " renderer]" << std::endl;
        gRendererName = renderer.name;

        const auto full = timePaint (timeline, width, height, frames, fullArea, *renderer.type);
        report ("timeline full repaint", full);

        // With profiling on, isolates the per-zone breakdown of a full repaint.
        if (args.contains ("--full-only"))
            continue;

        const auto playhead = timePaint (timeline, width, height, frames, playheadStrip, *renderer.type);
        report ("timeline strip, no cache", playhead);

        // What playback actually costs now: the strip comes from the cached layer.
        report ("playhead move (layered)",
                timePaint (layers, width, height, frames, playheadStrip, *renderer.type));

        // A mouse-wheel step: the lanes' cached pixels move, only the strip
        // scrolled into view (and the ruler, sideways) is drawn.
        report ("scroll 40 px sideways",
                timeScroll (layers, timeline, frames, 40.0, 0, *renderer.type));
        report ("scroll 60 px down",
                timeScroll (layers, timeline, frames, 0.0, 60, *renderer.type));

        report ("mixer full repaint",
                timePaint (mixer, width, mixerHeight, frames, mixer.getLocalBounds(), *renderer.type));
        report ("mixer meters area",
                timePaint (mixer, width, mixerHeight, frames, mixerMeters, *renderer.type));

        // Icons are drawn from cached rasters; the transport repaints 30 times a
        // second during playback.
        {
            DAWToolbar benchToolbar;
            Transport benchTransport (audioEngine, projectData);
            benchToolbar.setBounds (0, 0, width, 40);
            benchTransport.setBounds (0, 0, width, 60);
            report ("toolbar full repaint",
                    timePaint (benchToolbar, width, 40, frames, benchToolbar.getLocalBounds(), *renderer.type));
            report ("transport full repaint",
                    timePaint (benchTransport, width, 60, frames, benchTransport.getLocalBounds(), *renderer.type));
        }

        if (dragClip != nullptr)
        {
            const auto drag = timeClipDrag (timeline, *dragClip, width, height, frames, *renderer.type);
            report ("clip drag: model update", drag.modelUpdate);
            report ("clip drag: repaint", drag.repaint);
            report ("clip drag: per mouse move", drag.total);
        }

        const double areaRatio = (double) (strip * height) / (double) (width * height);
        const double costRatio = full.avgMs > 0.0 ? playhead.avgMs / full.avgMs : 0.0;

        std::cout << "  playhead strip is " << juce::String (100.0 * areaRatio, 2)
                  << " % of the area but costs " << juce::String (100.0 * costRatio, 1)
                  << " % of a full repaint (the gap is wasted paint)." << std::endl;
    }

    // After the Timeline and Mixer, so its MIDI track is not in their project.
    if (! args.contains ("--full-only"))
    {
        for (auto& renderer : renderersFromArgs (stringArg (args, "--renderer")))
        {
            std::cout << std::endl << "[" << renderer.name << " renderer, piano roll: " << numNotes << " notes, "
                      << numCC << " CC events, " << width << "x" << pianoRollH << "]" << std::endl;
            gRendererName = renderer.name;
            PianoRollScene scene (audioEngine, projectData, numNotes, numCC, width, pianoRollH);
            if (! scene.isReady())
            {
                std::cout << "  could not create the Piano Roll clip" << std::endl;
                break;
            }

            auto& editor = *scene.editor;
            report ("piano roll full repaint",
                    timePaint (editor, width, pianoRollH, frames, editor.getLocalBounds(), *renderer.type));
            scene.selectAll();
            report ("piano roll, all selected",
                    timePaint (editor, width, pianoRollH, frames, editor.getLocalBounds(), *renderer.type));
        }
    }

   #if AERION_ENABLE_PROFILING
    // The benchmark has no UI timer, so flush the probes by hand. rowsDrawn vs
    // rowsInClip is the headline: how many track rows were painted against how
    // many the invalidated region actually needed.
    std::cout << std::endl
              << Aerion::Profiling::Registry::get().flushReport (1.0) << std::endl;
   #endif

    sourceFile.deleteFile();
    return BenchBaseline::finish (args, 0);
}
