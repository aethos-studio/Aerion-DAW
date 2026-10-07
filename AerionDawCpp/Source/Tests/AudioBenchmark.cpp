#include <JuceHeader.h>
#include "AudioBenchmark.h"
#include "BenchBaseline.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <vector>

//==============================================================================
// Audio benchmark (AerionBench --audio, Milestone 5).
//
// Plays a generated project through Tracktion's real playback graph, block by
// block, the way an audio device would, and measures:
//   - how long each block takes, as a share of the block's duration (the time
//     the device gives us before the next one is due),
//   - heap allocations made on the calling (device) thread during a block,
//   - how long rebuilding the playback graph takes (after any edit).
// Blocks are pulled through Tracktion's HostedAudioDeviceInterface instead of a
// sound card, so it runs anywhere, including machines without audio hardware.
//
// The project: N audio tracks, each with a clip, an EQ, a compressor, a send to
// one of two reverb buses and a volume automation curve.
//
// Allocation counts cover the thread that calls processBlock. Worker threads of
// the multi-threaded graph are not counted.
//==============================================================================

namespace
{
    std::atomic<bool> countAllocations { false };
    std::atomic<std::thread::id> countingThread;
    std::atomic<long long> allocationsOnBlockThread { 0 };

    void noteAllocation() noexcept
    {
        if (countAllocations.load (std::memory_order_relaxed)
             && std::this_thread::get_id() == countingThread.load (std::memory_order_relaxed))
            allocationsOnBlockThread.fetch_add (1, std::memory_order_relaxed);
    }

    void* allocate (std::size_t size)
    {
        noteAllocation();

        if (auto* p = std::malloc (size > 0 ? size : 1))
            return p;

        throw std::bad_alloc();
    }
}

// Replaces the global allocator for this executable only, to count allocations.
void* operator new (std::size_t size)                       { return allocate (size); }
void* operator new[] (std::size_t size)                     { return allocate (size); }
void  operator delete (void* p) noexcept                    { std::free (p); }
void  operator delete[] (void* p) noexcept                  { std::free (p); }
void  operator delete (void* p, std::size_t) noexcept       { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept     { std::free (p); }

namespace
{
    namespace te = tracktion;

    struct BenchBehaviour final : te::EngineBehaviour
    {
        int numThreads = juce::SystemStats::getNumCpus();

        int getNumberOfCPUsToUseForAudio() override  { return numThreads; }
        bool autoInitialiseDeviceManager() override  { return false; }
    };

    juce::File writeNoiseFile (double sampleRate, double seconds)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("AerionBench_audio_source.wav");
        file.deleteFile();

        const int numSamples = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (2, numSamples);
        juce::Random random (42);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * 0.25f);

        juce::WavAudioFormat format;

        if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (format.createWriterFor (out.get(), sampleRate, 2, 24, {}, 0)))
            {
                out.release();
                writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
            }

        return file;
    }

    void buildProject (te::Edit& edit, const juce::File& source, int numTracks, double seconds)
    {
        edit.ensureNumberOfAudioTracks (numTracks + 2);
        auto tracks = te::getAudioTracks (edit);
        auto& cache = edit.getPluginCache();

        // The last two tracks are reverb buses fed by sends.
        for (int bus = 0; bus < 2; ++bus)
        {
            auto* busTrack = tracks[numTracks + bus];
            busTrack->setName ("Reverb bus " + juce::String (bus + 1));

            auto ret = cache.createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});

            if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (ret.get()))
                r->busNumber = bus;

            busTrack->pluginList.insertPlugin (ret, 0, nullptr);

            busTrack->pluginList.insertPlugin (cache.createNewPlugin (te::ReverbPlugin::xmlTypeName, {}), 1, nullptr);
        }

        for (int i = 0; i < numTracks; ++i)
        {
            auto* track = tracks[i];
            track->setName ("Track " + juce::String (i + 1));
            track->insertWaveClip ("clip", source,
                                   { { te::TimePosition(), te::TimePosition::fromSeconds (seconds) }, te::TimeDuration() },
                                   false);

            track->pluginList.insertPlugin (cache.createNewPlugin (te::EqualiserPlugin::xmlTypeName, {}), 0, nullptr);
            track->pluginList.insertPlugin (cache.createNewPlugin (te::CompressorPlugin::xmlTypeName, {}), 1, nullptr);

            auto send = cache.createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});

            if (auto* s = dynamic_cast<te::AuxSendPlugin*> (send.get()))
                s->busNumber = i % 2;

            track->pluginList.insertPlugin (send, 2, nullptr);

            if (auto vp = track->getVolumePlugin())
            {
                auto& curve = vp->volParam->getCurve();
                curve.addPoint (te::TimePosition(), 0.6f, 0.0f);
                curve.addPoint (te::TimePosition::fromSeconds (seconds), 0.9f, 0.0f);
            }
        }
    }

    struct Result
    {
        double meanPct = 0, p99Pct = 0, maxPct = 0;
        double allocationsPerBlock = 0;
        long long blocksWithAllocations = 0;
        float peak = 0;
    };

    Result measure (te::HostedAudioDeviceInterface& io, int blockSize, double sampleRate, int numBlocks)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<double> times;
        times.reserve ((size_t) numBlocks);
        std::vector<long long> allocations;
        allocations.reserve ((size_t) numBlocks);
        float peak = 0;

        countingThread = std::this_thread::get_id();

        for (int i = 0; i < numBlocks; ++i)
        {
            buffer.clear();
            const auto before = allocationsOnBlockThread.load();
            const auto start = std::chrono::steady_clock::now();

            countAllocations = true;
            io.processBlock (buffer, midi);
            countAllocations = false;

            const auto end = std::chrono::steady_clock::now();
            times.push_back (std::chrono::duration<double> (end - start).count());
            allocations.push_back (allocationsOnBlockThread.load() - before);
            peak = std::max (peak, buffer.getMagnitude (0, blockSize));
        }

        const double budget = blockSize / sampleRate;
        Result r;
        double sum = 0;

        for (auto t : times)
            sum += t;

        auto sorted = times;
        std::sort (sorted.begin(), sorted.end());
        r.meanPct = 100.0 * sum / (double) times.size() / budget;
        r.p99Pct  = 100.0 * sorted[(size_t) ((double) (sorted.size() - 1) * 0.99)] / budget;
        r.maxPct  = 100.0 * sorted.back() / budget;

        long long totalAllocations = 0;

        for (auto a : allocations)
        {
            totalAllocations += a;
            r.blocksWithAllocations += a > 0 ? 1 : 0;
        }

        r.allocationsPerBlock = (double) totalAllocations / (double) allocations.size();
        r.peak = peak;
        return r;
    }

    void printRow (const juce::String& label, const Result& r)
    {
        std::cout << "  " << label.paddedRight (' ', 34)
                  << "mean " << juce::String (r.meanPct, 1).paddedLeft (' ', 5) << " %"
                  << "   p99 " << juce::String (r.p99Pct, 1).paddedLeft (' ', 5) << " %"
                  << "   max " << juce::String (r.maxPct, 1).paddedLeft (' ', 6) << " %"
                  << "   allocs/block " << juce::String (r.allocationsPerBlock, 2)
                  << " (" << r.blocksWithAllocations << " blocks)" << std::endl;
    }

    int intArg (const juce::StringArray& args, const juce::String& name, int fallback)
    {
        for (auto& a : args)
            if (a.startsWith (name + "="))
                return a.fromFirstOccurrenceOf ("=", false, false).getIntValue();

        return fallback;
    }
}

int runAudioBenchmark (const juce::StringArray& args)
{
    const int numTracks = intArg (args, "--audio-tracks", 32);
    const int numBlocks = intArg (args, "--audio-blocks", 3000);
    const double sampleRate = 48000.0;
    const double seconds = 60.0;

    std::cout << "Aerion audio benchmark\n  tracks=" << numTracks << " (EQ, compressor, send, volume automation each)"
              << " + 2 reverb buses, blocks=" << numBlocks << ", " << sampleRate << " Hz, "
              << juce::SystemStats::getNumCpus() << " CPUs" << std::endl;

    auto behaviour = std::make_unique<BenchBehaviour>();
    auto* bench = behaviour.get();
    te::Engine engine ("AerionBench", nullptr, std::move (behaviour));

    auto source = writeNoiseFile (sampleRate, seconds);
    auto edit = te::Edit::createSingleTrackEdit (engine);
    buildProject (*edit, source, numTracks, seconds);

    auto& io = engine.getDeviceManager().getHostedAudioDeviceInterface();
    int failures = 0;

    // The counter must see an allocation made where it is looking.
    {
        countingThread = std::this_thread::get_id();
        const auto before = allocationsOnBlockThread.load();
        countAllocations = true;
        auto probe = std::make_unique<int> (1);
        countAllocations = false;

        if (allocationsOnBlockThread.load() == before)
        {
            std::cout << "  allocation counter does not see allocations: FAILED" << std::endl;
            ++failures;
        }
    }

    for (int blockSize : { 128, 256 })
    {
        te::HostedAudioDeviceInterface::Parameters params;
        params.sampleRate = sampleRate;
        params.blockSize = blockSize;
        params.inputChannels = 0;
        params.outputChannels = 2;
        io.initialise (params);
        io.prepareToPlay (sampleRate, blockSize);
        engine.getDeviceManager().dispatchPendingUpdates();

        std::cout << "[" << blockSize << " samples, budget " << juce::String (1000.0 * blockSize / sampleRate, 2) << " ms]" << std::endl;

        for (bool pooled : { false, true })
        {
            te::EditPlaybackContext::enablePooledMemory (pooled);
            te::EditPlaybackContext::enableNodeMemorySharing (pooled);

            for (int threads : { 1, juce::SystemStats::getNumCpus() })
            {
                bench->numThreads = threads;

                auto& transport = edit->getTransport();
                transport.stop (false, true);
                transport.freePlaybackContext();
                transport.setPosition (te::TimePosition());
                transport.ensureContextAllocated (true);
                transport.play (false);

                // Let file readers fill their caches and the graph settle.
                measure (io, blockSize, sampleRate, 400);
                const auto r = measure (io, blockSize, sampleRate, numBlocks);

                const auto label = juce::String (threads) + (threads == 1 ? " thread" : " threads")
                                 + (pooled ? ", pooled memory" : "");
                printRow (label, r);

                const auto name = "audio " + juce::String (blockSize) + " samples, " + label;
                BenchBaseline::record (name + ": mean", r.meanPct, "%");
                BenchBaseline::record (name + ": p99",  r.p99Pct,  "%");

                // Silence would mean the clips never played and the numbers
                // describe an idle graph.
                if (r.peak <= 0.0f)
                {
                    std::cout << "    output was silent: FAILED" << std::endl;
                    ++failures;
                }
            }
        }
    }

    // Graph rebuild: what every edit during playback costs on the message thread.
    {
        te::EditPlaybackContext::enablePooledMemory (false);
        te::EditPlaybackContext::enableNodeMemorySharing (false);
        bench->numThreads = juce::SystemStats::getNumCpus();
        auto& transport = edit->getTransport();
        transport.ensureContextAllocated (true);
        transport.play (false);

        std::vector<double> rebuildMs;

        for (int i = 0; i < 10; ++i)
        {
            const auto start = std::chrono::steady_clock::now();

            if (auto* ctx = edit->getCurrentPlaybackContext())
                ctx->createPlayAudioNodes (transport.getPosition());

            rebuildMs.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count());
        }

        std::sort (rebuildMs.begin(), rebuildMs.end());
        BenchBaseline::record ("audio graph rebuild: median", rebuildMs[rebuildMs.size() / 2], "ms");
        std::cout << "[graph rebuild]\n  median " << juce::String (rebuildMs[rebuildMs.size() / 2], 1)
                  << " ms, max " << juce::String (rebuildMs.back(), 1) << " ms" << std::endl;

        transport.stop (false, true);
        transport.freePlaybackContext();
    }

    edit.reset();
    engine.getDeviceManager().deviceManager.closeAudioDevice();
    engine.getDeviceManager().removeHostedAudioDeviceInterface();
    source.deleteFile();
    return failures > 0 ? 2 : 0;
}
