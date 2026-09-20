#pragma once

#include <JuceHeader.h>

//==============================================================================
// Aerion paint/hot-path profiling (Milestone 5).
//
// Opt-in via -DAERION_ENABLE_PROFILING=ON (see CMakeLists.txt). When the option
// is OFF every macro below expands to nothing, so profiled call sites cost
// exactly zero in shipping builds and can be left in the source permanently.
//
// Two kinds of probe:
//   AERION_PROFILE_SCOPE("Timeline::paint")  - wall-clock of the enclosing scope
//   AERION_PROFILE_COUNT("Timeline.rows", 1) - how many items a pass touched
//
// Counters are what turn "the timeline feels heavy" into "we painted 640 clips
// to move a 16 px playhead strip", so paint passes should count the work they
// iterate over, not just time themselves.
//
// Everything here is message-thread only. Do not profile the audio thread with
// these macros - reporting formats juce::String and writes to the logger.
//==============================================================================

#if AERION_ENABLE_PROFILING

namespace Aerion::Profiling
{
    /** Fixed ceiling so the registry is a flat array and sampling never allocates
        or rehashes on a paint path. Raise if you genuinely run out of zones. */
    inline constexpr int kMaxZones = 64;

    struct Zone
    {
        const char*  name     = nullptr;
        juce::uint64 calls    = 0;
        double       totalMs  = 0.0;
        double       maxMs    = 0.0;
        juce::int64  count    = 0;   // items touched (clips, rows, notes, ...)
    };

    class Registry
    {
    public:
        static Registry& get()
        {
            static Registry instance;
            return instance;
        }

        /** Registers a named zone and returns its slot. Call once per site via a
            function-local static so the lookup stays out of the hot path. */
        int registerZone (const char* name)
        {
            // Reuse the slot when the same label is profiled from several sites
            // (e.g. an overload painted from two places) so totals stay merged.
            for (int i = 0; i < numZones; ++i)
                if (std::strcmp (zones[i].name, name) == 0)
                    return i;

            if (numZones >= kMaxZones)
                return -1;

            zones[numZones].name = name;
            return numZones++;
        }

        void addSample (int id, double elapsedMs) noexcept
        {
            if (id < 0)
                return;

            auto& z = zones[id];
            ++z.calls;
            z.totalMs += elapsedMs;
            z.maxMs = juce::jmax (z.maxMs, elapsedMs);
        }

        void addCount (int id, juce::int64 n) noexcept
        {
            if (id >= 0)
                zones[id].count += n;
        }

        /** Formats accumulated stats and clears them, so each report describes one
            window rather than the whole session. */
        juce::String flushReport (double windowSeconds)
        {
            juce::String s;
            s << "Aerion profile (" << juce::String (windowSeconds, 1) << " s window)\n";
            s << juce::String ("zone").paddedRight (' ', 34)
              << juce::String ("calls").paddedLeft (' ', 8)
              << juce::String ("cal/s").paddedLeft (' ', 8)
              << juce::String ("avg ms").paddedLeft (' ', 9)
              << juce::String ("max ms").paddedLeft (' ', 9)
              << juce::String ("ms/s").paddedLeft (' ', 8)
              << juce::String ("items/call").paddedLeft (' ', 12) << "\n";

            for (int i = 0; i < numZones; ++i)
            {
                auto& z = zones[i];
                if (z.calls == 0 && z.count == 0)
                    continue;

                const double calls = (double) z.calls;
                const double perCall = calls > 0.0 ? z.totalMs / calls : 0.0;
                const double itemsPerCall = calls > 0.0 ? (double) z.count / calls : 0.0;

                s << juce::String (z.name).paddedRight (' ', 34)
                  << juce::String (z.calls).paddedLeft (' ', 8)
                  << juce::String (windowSeconds > 0.0 ? calls / windowSeconds : 0.0, 1).paddedLeft (' ', 8)
                  << juce::String (perCall, 3).paddedLeft (' ', 9)
                  << juce::String (z.maxMs, 3).paddedLeft (' ', 9)
                  << juce::String (windowSeconds > 0.0 ? z.totalMs / windowSeconds : 0.0, 2).paddedLeft (' ', 8)
                  << juce::String (itemsPerCall, 1).paddedLeft (' ', 12) << "\n";

                z.calls = 0;
                z.totalMs = 0.0;
                z.maxMs = 0.0;
                z.count = 0;
            }

            return s;
        }

        int getNumZones() const noexcept { return numZones; }

    private:
        Registry() = default;

        Zone zones[kMaxZones];
        int  numZones = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Registry)
    };

    class ScopedZone
    {
    public:
        explicit ScopedZone (int zoneId) noexcept
            : id (zoneId), startTicks (juce::Time::getHighResolutionTicks()) {}

        ~ScopedZone() noexcept
        {
            const auto elapsed = juce::Time::getHighResolutionTicks() - startTicks;
            Registry::get().addSample (id, 1000.0 * (double) elapsed
                                             / (double) juce::Time::getHighResolutionTicksPerSecond());
        }

    private:
        const int id;
        const juce::int64 startTicks;

        JUCE_DECLARE_NON_COPYABLE (ScopedZone)
    };

    /** Drives periodic reporting from an existing UI timer, so profiling adds no
        timer of its own. Logs at most once per interval. */
    class PeriodicReporter
    {
    public:
        void tick (double intervalSeconds = 5.0)
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();

            if (lastReportMs <= 0.0)
            {
                lastReportMs = now;
                return;
            }

            const double elapsedSeconds = (now - lastReportMs) / 1000.0;
            if (elapsedSeconds < intervalSeconds)
                return;

            lastReportMs = now;
            juce::Logger::writeToLog (Registry::get().flushReport (elapsedSeconds));
        }

    private:
        double lastReportMs = 0.0;
    };
}

#define AERION_PROFILE_SCOPE(zoneName)                                                        \
    static const int JUCE_JOIN_MACRO (aerionZoneId_, __LINE__)                                \
        = ::Aerion::Profiling::Registry::get().registerZone (zoneName);                       \
    const ::Aerion::Profiling::ScopedZone JUCE_JOIN_MACRO (aerionZone_, __LINE__)             \
        (JUCE_JOIN_MACRO (aerionZoneId_, __LINE__));

#define AERION_PROFILE_COUNT(zoneName, n)                                                     \
    do {                                                                                      \
        static const int JUCE_JOIN_MACRO (aerionCountId_, __LINE__)                           \
            = ::Aerion::Profiling::Registry::get().registerZone (zoneName);                   \
        ::Aerion::Profiling::Registry::get().addCount (                                       \
            JUCE_JOIN_MACRO (aerionCountId_, __LINE__), (juce::int64) (n));                   \
    } while (false)

#define AERION_PROFILE_REPORTER(name) ::Aerion::Profiling::PeriodicReporter name
#define AERION_PROFILE_TICK(name)     (name).tick()

#else // ! AERION_ENABLE_PROFILING

#define AERION_PROFILE_SCOPE(zoneName)      ((void) 0)
#define AERION_PROFILE_COUNT(zoneName, n)   ((void) 0)
#define AERION_PROFILE_REPORTER(name)       static_assert (true, "")
#define AERION_PROFILE_TICK(name)           ((void) 0)

#endif
