#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <functional>

/** Keeps a hosted plugin that crashes from taking the app down.

    process() runs the plugin's processBlock inside a guard. If the plugin
    faults (access violation, divide by zero, an escaped C++ exception...),
    that block is silenced and the plugin is skipped from then on, so its
    input passes through unchanged. onFault is called once on the message
    thread so the app can bypass the plugin and tell the user. Once the app
    has bypassed it, re-enabling the plugin lets it process again.

    Catching a fault works on Windows only (structured exceptions). The build
    uses /EHa, so catching one also runs the destructors of the frames in
    between, which releases the lock JUCE's plugin wrapper holds while the
    plugin runs. Elsewhere process() just calls the plugin.

    call() guards the other calls into a plugin: loading it, saving and
    restoring its settings, creating its editor. A crash there leaves the
    plugin broken for the rest of the session: it is never called again, by
    process() or call(), until clear().

    Plugins are identified by an opaque key (the tracktion::ExternalPlugin). */
class PluginFaultMonitor : private juce::AsyncUpdater
{
public:
    enum class Stage { processing, loading, savingState, restoringState, openingEditor };

    PluginFaultMonitor() = default;
    ~PluginFaultMonitor() override;

    /** Audio thread. pluginEnabled is the plugin's current enabled state. */
    void process (const void* key, bool pluginEnabled, juce::AudioPluginInstance&,
                  juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);

    /** Message thread. Runs fn, a call into the plugin outside audio
        processing. Returns false if fn crashed, or if the plugin is already
        broken, in which case fn is not called. */
    bool call (const void* key, Stage, const std::function<void()>& fn);

    bool hasFaulted (const void* key) const;

    /** True once a crash outside audio processing has been caught. */
    bool isBroken (const void* key) const;

    /** Message thread: forgets every fault, e.g. before the Edit is replaced. */
    void clear();

    /** Message thread, once per fault, with where it happened and a short
        description of it. For a processing fault, return true once the plugin
        is bypassed, so re-enabling it later lets it run again; return false to
        keep skipping it. A plugin broken in any other stage stays skipped. */
    std::function<bool (const void* key, Stage, const juce::String& reason)> onFault;

    /** Message thread: calls onFault for pending faults now instead of on the
        next message loop pass (for tests). */
    void deliverPendingFaults() { handleUpdateNowIfNeeded(); }

    /** Calls fn (context). On Windows a crash inside it is caught and its
        exception code returned; returns 0 when fn returned normally. */
    static unsigned int callGuarded (void (*fn) (void*), void* context);
    static juce::String describeFault (unsigned int code);

    /** "processing audio", "saving its settings"... */
    static juce::String describeStage (Stage);

private:
    enum State : int { unused, faulted, bypassedByApp, broken };

    struct Slot
    {
        std::atomic<const void*> key { nullptr };
        std::atomic<unsigned int> code { 0 };
        std::atomic<int> stage { (int) Stage::processing };
        std::atomic<int> state { unused };
        std::atomic<bool> reported { false };
    };

    // A fixed table so the audio thread never allocates or locks.
    std::array<Slot, 32> slots;
    std::atomic<int> numFaults { 0 };

    Slot* findSlot (const void* key);
    void recordFault (const void* key, unsigned int code, Stage);
    void release (Slot&);
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE (PluginFaultMonitor)
};
