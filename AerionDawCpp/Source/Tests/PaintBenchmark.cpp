#include <JuceHeader.h>
#include "../UIComponents.h"

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
//               [--verify]   exit code 2 if a partial repaint differs from a full one
//   AerionBench --tracks=4 --clips=3 --snapshots=<dir>
//               renders an icon sheet and the toolbar, transport, menu bar, Timeline
//               and Mixer at 100 % and 150 % scale to PNGs, then exits
//==============================================================================

namespace
{
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

    /** Checks that painting only `region` gives the same pixels there as a full
        repaint. Culling in paint() must only skip work the clip would discard
        anyway, so any mismatch is a culling bug. Uses the software renderer so
        the comparison is deterministic. Returns the number of differing pixels. */
    int countPartialRepaintMismatches (juce::Component& c, int width, int height,
                                       const juce::RectangleList<int>& region)
    {
        const juce::SoftwareImageType software;
        juce::Image full (juce::Image::ARGB, width, height, true, software);
        {
            juce::Graphics g (full);
            c.paintEntireComponent (g, false);
        }

        auto partial = full.createCopy();
        for (auto& r : region)
            partial.clear (r);
        {
            juce::Graphics g (partial);
            g.reduceClipRegion (region);
            c.paintEntireComponent (g, false);
        }

        int mismatches = 0;
        for (auto& r : region)
        {
            const auto area = r.getIntersection ({ 0, 0, width, height });
            for (int y = area.getY(); y < area.getBottom(); ++y)
                for (int x = area.getX(); x < area.getRight(); ++x)
                    if (full.getPixelAt (x, y) != partial.getPixelAt (x, y))
                        ++mismatches;
        }

        return mismatches;
    }

    bool writePng (const juce::Image& image, const juce::File& dest)
    {
        dest.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = std::unique_ptr<juce::FileOutputStream> (dest.createOutputStream()))
            return png.writeImageToStream (image, *out);
        return false;
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
        juce::Image image (juce::Image::ARGB, width, height, true, imageType);

        // One untimed pass so lazily-built caches (thumbnails, fonts) are warm
        // and the measured frames reflect steady state rather than first paint.
        {
            juce::Graphics g (image);
            g.reduceClipRegion (clipRegion);
            c.paintEntireComponent (g, false);
        }

        Result r;
        r.minMs = std::numeric_limits<double>::max();
        double total = 0.0;

        for (int i = 0; i < frames; ++i)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            {
                juce::Graphics g (image);
                g.reduceClipRegion (clipRegion);
                c.paintEntireComponent (g, false);
            }
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
        juce::Image image (juce::Image::ARGB, width, height, true, imageType);
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
            {
                juce::Graphics g (image);
                g.reduceClipRegion (oldArea.getUnion (timeline.getClipPaintBounds (clip)));
                timeline.paintEntireComponent (g, false);
            }
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

    void report (const juce::String& label, const Result& r)
    {
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

    const int numTracks    = intArg (args, "--tracks", 32);
    const int clipsPerTrack = intArg (args, "--clips",  20);
    const int frames       = intArg (args, "--frames", 200);
    const int width        = intArg (args, "--width",  1920);
    const int height       = intArg (args, "--height", 1080);

    std::cout << "Aerion Timeline paint benchmark\n"
              << "  tracks=" << numTracks
              << " clips/track=" << clipsPerTrack
              << " frames=" << frames
              << " size=" << width << "x" << height << std::endl;

    AudioEngineManager audioEngine;
    ProjectData projectData;

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
        checks.add ({ "timeline header column", &timeline, height, juce::Rectangle<int> (0, 0, Timeline::kHeaderWidth, height) });

        if (dragClip != nullptr)
            checks.add ({ "timeline dragged clip", &timeline, height, timeline.getClipPaintBounds (*dragClip) });

        checks.add ({ "mixer meters", &mixer, mixerHeight, mixerMeters });
        // An arbitrary slice across strips, like a window uncovering part of the
        // console: every section it touches must redraw identically.
        checks.add ({ "mixer arbitrary slice", &mixer, mixerHeight, juce::Rectangle<int> (100, 60, 300, 90) });

        int failures = 0;
        for (auto& check : checks)
        {
            const auto& name = check.name;
            const int mismatches = countPartialRepaintMismatches (*check.component, width, check.height, check.region);
            std::cout << "  " << name.paddedRight (' ', 28)
                      << (mismatches == 0 ? juce::String ("ok")
                                          : juce::String (mismatches) + " pixels differ")
                      << std::endl;
            failures += mismatches > 0 ? 1 : 0;
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

        const auto full = timePaint (timeline, width, height, frames, fullArea, *renderer.type);
        report ("timeline full repaint", full);

        const auto playhead = timePaint (timeline, width, height, frames, playheadStrip, *renderer.type);
        report ("timeline playhead 16px", playhead);

        report ("mixer full repaint",
                timePaint (mixer, width, mixerHeight, frames, mixer.getLocalBounds(), *renderer.type));
        report ("mixer meters area",
                timePaint (mixer, width, mixerHeight, frames, mixerMeters, *renderer.type));

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

   #if AERION_ENABLE_PROFILING
    // The benchmark has no UI timer, so flush the probes by hand. rowsDrawn vs
    // rowsInClip is the headline: how many track rows were painted against how
    // many the invalidated region actually needed.
    std::cout << std::endl
              << Aerion::Profiling::Registry::get().flushReport (1.0) << std::endl;
   #endif

    sourceFile.deleteFile();
    return 0;
}
