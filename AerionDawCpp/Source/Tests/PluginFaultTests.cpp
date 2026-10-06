#include <JuceHeader.h>
#include "../PluginFaultGuard.h"
#include "../AudioEngine.h"
#include <stdexcept>

//==============================================================================
// PluginFaultMonitor: a hosted plugin that crashes in processBlock must be
// caught, silenced and skipped, not take the app down.
//
// The crash cases only run on Windows, the one platform where the guard can
// catch a fault. Under a debugger they show up as first-chance exceptions;
// continue past them.
//==============================================================================

namespace
{
    enum class Failure { none, accessViolation, cppException };

    // A minimal plugin that halves its input, or fails the way it is told to.
    class TestPlugin final : public juce::AudioPluginInstance
    {
    public:
        Failure failure = Failure::none;
        int processCalls = 0;
        juce::SpinLock processLock; // like the lock JUCE's VST3 wrapper holds

        const juce::String getName() const override { return "Test Plugin"; }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = getName(); }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            const juce::SpinLock::ScopedLockType lock (processLock);
            ++processCalls;

            if (failure == Failure::accessViolation)
            {
                volatile int* volatile nowhere = nullptr;
                *nowhere = 1;
            }

            if (failure == Failure::cppException)
                throw std::runtime_error ("plugin bug");

            buffer.applyGain (0.5f);
        }

        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };

    juce::AudioBuffer<float> makeBuffer (float value)
    {
        juce::AudioBuffer<float> b (2, 64);

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, b.getNumSamples());

        return b;
    }
}

class PluginFaultTests final : public juce::UnitTest
{
public:
    PluginFaultTests() : juce::UnitTest ("PluginFaultMonitor", "Aerion") {}

    void runTest() override
    {
        beginTest ("a working plugin processes normally");
        {
            PluginFaultMonitor monitor;
            TestPlugin plugin;
            juce::MidiBuffer midi;
            auto buffer = makeBuffer (1.0f);

            monitor.process (&plugin, true, plugin, buffer, midi, false);

            expectEquals (plugin.processCalls, 1);
            expectEquals (buffer.getSample (0, 0), 0.5f);
            expect (! monitor.hasFaulted (&plugin));
        }

        beginTest ("the engine installs the monitor and handles its faults");
        {
            AudioEngineManager engine;
            expect (engine.getPluginFaultMonitor().onFault != nullptr);
            // A key that is no plugin in the Edit stays skipped instead of bypassed.
            int notAPlugin = 0;
            expect (! engine.getPluginFaultMonitor().onFault (&notAPlugin, PluginFaultMonitor::Stage::processing, "test"));
        }

       #if JUCE_WINDOWS
        beginTest ("a crash is caught, silenced and unwinds the plugin's lock");
        {
            PluginFaultMonitor monitor;
            TestPlugin plugin;
            plugin.failure = Failure::accessViolation;
            juce::MidiBuffer midi;
            auto buffer = makeBuffer (1.0f);

            monitor.process (&plugin, true, plugin, buffer, midi, false);

            expect (monitor.hasFaulted (&plugin));
            expectEquals (buffer.getMagnitude (0, buffer.getNumSamples()), 0.0f);
            expect (plugin.processLock.tryEnter(), "the lock held during the crash must be released");
            plugin.processLock.exit();
        }

        beginTest ("an escaped C++ exception is caught too");
        {
            PluginFaultMonitor monitor;
            TestPlugin plugin;
            plugin.failure = Failure::cppException;
            juce::MidiBuffer midi;
            auto buffer = makeBuffer (1.0f);

            monitor.process (&plugin, true, plugin, buffer, midi, false);
            expect (monitor.hasFaulted (&plugin));
        }

        beginTest ("a faulted plugin is skipped and reported once");
        {
            PluginFaultMonitor monitor;
            int reports = 0;
            juce::String reason;
            monitor.onFault = [&] (const void*, PluginFaultMonitor::Stage, const juce::String& r) { ++reports; reason = r; return true; };

            TestPlugin plugin;
            plugin.failure = Failure::accessViolation;
            juce::MidiBuffer midi;
            auto buffer = makeBuffer (1.0f);
            monitor.process (&plugin, true, plugin, buffer, midi, false);

            // Until the app has handled it, the plugin is skipped even though enabled.
            auto next = makeBuffer (1.0f);
            monitor.process (&plugin, true, plugin, next, midi, false);
            expectEquals (plugin.processCalls, 1);
            expectEquals (next.getSample (1, 10), 1.0f, "a skipped plugin passes its input through");

            monitor.deliverPendingFaults();
            monitor.deliverPendingFaults();
            expectEquals (reports, 1);
            expectEquals (reason, juce::String ("invalid memory access"));

            // Bypassed by the app: skipped while disabled...
            monitor.process (&plugin, false, plugin, next, midi, true);
            expectEquals (plugin.processCalls, 1);

            // ...and back in once the user turns it on again.
            plugin.failure = Failure::none;
            monitor.process (&plugin, true, plugin, next, midi, false);
            expectEquals (plugin.processCalls, 2);
            expect (! monitor.hasFaulted (&plugin));
        }

        beginTest ("a fault the app could not bypass stays skipped");
        {
            PluginFaultMonitor monitor;
            monitor.onFault = [] (const void*, PluginFaultMonitor::Stage, const juce::String&) { return false; };

            TestPlugin plugin;
            plugin.failure = Failure::accessViolation;
            juce::MidiBuffer midi;
            auto buffer = makeBuffer (1.0f);
            monitor.process (&plugin, true, plugin, buffer, midi, false);
            monitor.deliverPendingFaults();

            plugin.failure = Failure::none;
            monitor.process (&plugin, true, plugin, buffer, midi, false);
            expectEquals (plugin.processCalls, 1);
            expect (monitor.hasFaulted (&plugin));

            monitor.clear();
            expect (! monitor.hasFaulted (&plugin));
            monitor.process (&plugin, true, plugin, buffer, midi, false);
            expectEquals (plugin.processCalls, 2);
        }

        beginTest ("faults in different plugins are tracked separately");
        {
            PluginFaultMonitor monitor;
            TestPlugin crashing, healthy;
            crashing.failure = Failure::accessViolation;
            juce::MidiBuffer midi;
            auto buffer = makeBuffer (1.0f);

            monitor.process (&crashing, true, crashing, buffer, midi, false);
            monitor.process (&healthy, true, healthy, buffer, midi, false);

            expect (monitor.hasFaulted (&crashing));
            expect (! monitor.hasFaulted (&healthy));
            expectEquals (healthy.processCalls, 1);
        }

        beginTest ("a crash outside processing breaks the plugin for the session");
        {
            PluginFaultMonitor monitor;
            int reports = 0;
            PluginFaultMonitor::Stage reportedStage {};
            monitor.onFault = [&] (const void*, PluginFaultMonitor::Stage s, const juce::String&)
            {
                ++reports;
                reportedStage = s;
                return true;
            };

            TestPlugin plugin;
            int calls = 0;
            const bool ok = monitor.call (&plugin, PluginFaultMonitor::Stage::savingState, [&]
            {
                ++calls;
                volatile int* volatile nowhere = nullptr;
                *nowhere = 1;
            });

            expect (! ok);
            expect (monitor.isBroken (&plugin));
            monitor.deliverPendingFaults();
            expectEquals (reports, 1);
            expect (reportedStage == PluginFaultMonitor::Stage::savingState);

            // Never called again: not to save, and not to process even when enabled.
            expect (! monitor.call (&plugin, PluginFaultMonitor::Stage::savingState, [&] { ++calls; }));
            expectEquals (calls, 1);

            juce::MidiBuffer midi;
            auto buffer = makeBuffer (1.0f);
            monitor.process (&plugin, true, plugin, buffer, midi, false);
            expectEquals (plugin.processCalls, 0);
            expectEquals (buffer.getSample (0, 0), 1.0f, "a broken plugin passes its input through");

            monitor.clear();
            expect (monitor.call (&plugin, PluginFaultMonitor::Stage::savingState, [&] { ++calls; }));
            expectEquals (calls, 2);
        }

        beginTest ("a processing fault followed by a crash while saving becomes broken");
        {
            PluginFaultMonitor monitor;
            monitor.onFault = [] (const void*, PluginFaultMonitor::Stage, const juce::String&) { return true; };

            TestPlugin plugin;
            plugin.failure = Failure::accessViolation;
            juce::MidiBuffer midi;
            auto buffer = makeBuffer (1.0f);
            monitor.process (&plugin, true, plugin, buffer, midi, false);
            monitor.deliverPendingFaults();
            expect (! monitor.isBroken (&plugin));

            expect (! monitor.call (&plugin, PluginFaultMonitor::Stage::savingState,
                                    [] { throw std::runtime_error ("state bug"); }));
            expect (monitor.isBroken (&plugin));

            // Re-enabling no longer brings it back.
            plugin.failure = Failure::none;
            monitor.process (&plugin, true, plugin, buffer, midi, false);
            expectEquals (plugin.processCalls, 1);
        }
       #endif

        beginTest ("a call that does not crash runs and reports success");
        {
            PluginFaultMonitor monitor;
            int value = 0;
            expect (monitor.call (&value, PluginFaultMonitor::Stage::loading, [&] { value = 7; }));
            expectEquals (value, 7);
            expect (! monitor.hasFaulted (&value));
        }
    }
};

static PluginFaultTests pluginFaultTests;
