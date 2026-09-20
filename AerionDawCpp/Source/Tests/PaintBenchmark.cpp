#include <JuceHeader.h>
#include "../UIComponents.h"

//==============================================================================
// Headless Timeline paint benchmark (Milestone 5).
//
// Renders the real Timeline component into an offscreen image so paint cost can
// be measured without a display, a mouse, or a human. Two scenarios:
//
//   full      - the whole component is invalidated (scroll, zoom, edit)
//   playhead  - only a 16 px strip is invalidated, which is what
//               MainComponent::timerCallback actually does 25 times a second
//               during playback
//
// The playhead scenario is the interesting one. If Timeline::paint scales with
// the invalidated area, its cost should be a small fraction of the full repaint.
// If it instead walks every track and every clip regardless, the two numbers
// converge - and that gap is the thing Milestone 5 needs to close.
//
// Not registered with ctest: timings are machine-dependent, so this is a tool
// you run and read, not a pass/fail gate.
//
//   AerionBench --tracks=32 --clips=20 --frames=200
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

    Result timePaint (juce::Component& c, int width, int height, int frames,
                      juce::Rectangle<int> clipRegion)
    {
        juce::Image image (juce::Image::ARGB, width, height, true);

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
            const auto elapsedMs = 1000.0 * (double) (juce::Time::getHighResolutionTicks() - start)
                                         / (double) juce::Time::getHighResolutionTicksPerSecond();

            total += elapsedMs;
            r.minMs = juce::jmin (r.minMs, elapsedMs);
            r.maxMs = juce::jmax (r.maxMs, elapsedMs);
        }

        r.avgMs = frames > 0 ? total / (double) frames : 0.0;
        return r;
    }

    void report (const juce::String& label, const Result& r, double budgetMs)
    {
        std::cout << label.paddedRight (' ', 22)
                  << juce::String (r.avgMs, 3).paddedLeft (' ', 9) << " ms avg"
                  << juce::String (r.minMs, 3).paddedLeft (' ', 9) << " ms min"
                  << juce::String (r.maxMs, 3).paddedLeft (' ', 9) << " ms max"
                  << juce::String (r.avgMs > 0.0 ? 1000.0 / r.avgMs : 0.0, 0).paddedLeft (' ', 9) << " fps"
                  << juce::String (100.0 * r.avgMs / budgetMs, 1).paddedLeft (' ', 9) << " % of 40 ms tick"
                  << std::endl;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

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

    // 25 Hz UI timer => a 40 ms budget for everything, paint included.
    const double budgetMs = 40.0;

    std::cout << std::endl;

    const auto full = timePaint (timeline, width, height, frames,
                                 juce::Rectangle<int> (0, 0, width, height));
    report ("full repaint", full, budgetMs);

    // Mirror MainComponent::timerCallback: a 16 px wide, full height strip.
    const int strip = 16;
    const auto playhead = timePaint (timeline, width, height, frames,
                                     juce::Rectangle<int> (width / 2 - strip / 2, 0, strip, height));
    report ("playhead strip 16px", playhead, budgetMs);

    std::cout << std::endl;

    const double areaRatio = (double) (strip * height) / (double) (width * height);
    const double costRatio = full.avgMs > 0.0 ? playhead.avgMs / full.avgMs : 0.0;

    std::cout << "playhead strip covers " << juce::String (100.0 * areaRatio, 2)
              << " % of the component area but costs "
              << juce::String (100.0 * costRatio, 1) << " % of a full repaint." << std::endl;
    std::cout << "Ideal is for those two numbers to track each other; the gap is wasted paint."
              << std::endl;

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
