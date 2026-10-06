#include "PluginFaultGuard.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
   #if JUCE_WINDOWS
    // Code of an escaped Microsoft C++ exception ('msc').
    constexpr unsigned int cppExceptionCode = 0xE06D7363u;
   #endif

    struct PluginCall
    {
        juce::AudioPluginInstance* instance;
        juce::AudioBuffer<float>* buffer;
        juce::MidiBuffer* midi;
        bool bypassed;
    };

    void runPlugin (void* context)
    {
        auto& call = *static_cast<PluginCall*> (context);

        if (call.bypassed)
            call.instance->processBlockBypassed (*call.buffer, *call.midi);
        else
            call.instance->processBlock (*call.buffer, *call.midi);
    }

    void runFunction (void* context)
    {
        (*static_cast<const std::function<void()>*> (context))();
    }

   #if JUCE_WINDOWS
    // Faults a plugin can cause and we can continue after. Breakpoints and
    // single steps belong to the debugger; a stack overflow leaves no stack to
    // continue on.
    int faultFilter (unsigned int code)
    {
        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            case EXCEPTION_DATATYPE_MISALIGNMENT:
            case EXCEPTION_FLT_DENORMAL_OPERAND:
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            case EXCEPTION_FLT_INEXACT_RESULT:
            case EXCEPTION_FLT_INVALID_OPERATION:
            case EXCEPTION_FLT_OVERFLOW:
            case EXCEPTION_FLT_STACK_CHECK:
            case EXCEPTION_FLT_UNDERFLOW:
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_IN_PAGE_ERROR:
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
            case EXCEPTION_INT_OVERFLOW:
            case EXCEPTION_PRIV_INSTRUCTION:
            case cppExceptionCode:
                return EXCEPTION_EXECUTE_HANDLER;

            default:
                return EXCEPTION_CONTINUE_SEARCH;
        }
    }
   #endif
}

// No C++ objects with destructors may live in this function: __try cannot be
// combined with them.
unsigned int PluginFaultMonitor::callGuarded (void (*fn) (void*), void* context)
{
   #if JUCE_WINDOWS
    unsigned int code = 0;

    __try
    {
        fn (context);
    }
    __except (faultFilter (code = GetExceptionCode()))
    {
        return code;
    }

    return 0;
   #else
    fn (context);
    return 0;
   #endif
}

juce::String PluginFaultMonitor::describeFault (unsigned int code)
{
   #if JUCE_WINDOWS
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:      return "invalid memory access";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "division by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:      return "invalid instruction";
        case cppExceptionCode:                return "unhandled C++ exception";
        default: break;
    }
   #endif

    return "exception 0x" + juce::String::toHexString ((juce::int64) code).paddedLeft ('0', 8).toUpperCase();
}

juce::String PluginFaultMonitor::describeStage (Stage stage)
{
    switch (stage)
    {
        case Stage::processing:     return "processing audio";
        case Stage::loading:        return "loading";
        case Stage::savingState:    return "saving its settings";
        case Stage::restoringState: return "restoring its settings";
        case Stage::openingEditor:  return "opening its editor";
    }

    return {};
}

//==============================================================================
PluginFaultMonitor::~PluginFaultMonitor()
{
    cancelPendingUpdate();
}

void PluginFaultMonitor::process (const void* key, bool pluginEnabled, juce::AudioPluginInstance& instance,
                                  juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
{
    if (numFaults.load (std::memory_order_acquire) > 0)
    {
        if (auto* slot = findSlot (key))
        {
            // Skip it, letting the input pass through, until the app has
            // bypassed it and the user turns it back on. A broken one stays out.
            if (! (slot->state.load (std::memory_order_acquire) == bypassedByApp && pluginEnabled))
                return;

            release (*slot);
        }
    }

    PluginCall call { &instance, &buffer, &midi, bypassed };

    if (const auto code = callGuarded (runPlugin, &call))
    {
        buffer.clear();
        midi.clear();
        recordFault (key, code, Stage::processing);
    }
}

bool PluginFaultMonitor::call (const void* key, Stage stage, const std::function<void()>& fn)
{
    if (isBroken (key))
        return false;

    if (const auto code = callGuarded (runFunction, const_cast<std::function<void()>*> (&fn)))
    {
        // A plugin that faulted in processBlock and crashes again here is
        // already in the table: mark that slot broken.
        if (auto* slot = findSlot (key))
        {
            slot->code.store (code);
            slot->stage.store ((int) stage);
            slot->reported.store (false);
            slot->state.store (broken, std::memory_order_release);
            triggerAsyncUpdate();
        }
        else
        {
            recordFault (key, code, stage);
        }

        return false;
    }

    return true;
}

bool PluginFaultMonitor::isBroken (const void* key) const
{
    for (auto& slot : slots)
        if (slot.key.load (std::memory_order_acquire) == key && slot.state.load (std::memory_order_acquire) == broken)
            return true;

    return false;
}

bool PluginFaultMonitor::hasFaulted (const void* key) const
{
    for (auto& slot : slots)
        if (slot.key.load (std::memory_order_acquire) == key && slot.state.load (std::memory_order_acquire) != unused)
            return true;

    return false;
}

void PluginFaultMonitor::clear()
{
    cancelPendingUpdate();

    for (auto& slot : slots)
    {
        slot.state.store (unused, std::memory_order_release);
        slot.reported.store (false);
        slot.key.store (nullptr, std::memory_order_release);
    }

    numFaults.store (0, std::memory_order_release);
}

PluginFaultMonitor::Slot* PluginFaultMonitor::findSlot (const void* key)
{
    for (auto& slot : slots)
        if (slot.key.load (std::memory_order_acquire) == key && slot.state.load (std::memory_order_acquire) != unused)
            return &slot;

    return nullptr;
}

void PluginFaultMonitor::recordFault (const void* key, unsigned int code, Stage stage)
{
    for (auto& slot : slots)
    {
        const void* expected = nullptr;

        if (slot.key.compare_exchange_strong (expected, key, std::memory_order_acq_rel))
        {
            slot.code.store (code);
            slot.stage.store ((int) stage);
            slot.reported.store (false);
            slot.state.store (stage == Stage::processing ? faulted : broken, std::memory_order_release);
            numFaults.fetch_add (1, std::memory_order_acq_rel);
            triggerAsyncUpdate();
            return;
        }
    }

    // Table full: the plugin keeps running and keeps being caught, one silent
    // block at a time. Needs 32 crashed plugins in one session.
}

void PluginFaultMonitor::release (Slot& slot)
{
    slot.state.store (unused, std::memory_order_release);
    slot.key.store (nullptr, std::memory_order_release);
    numFaults.fetch_sub (1, std::memory_order_acq_rel);
}

void PluginFaultMonitor::handleAsyncUpdate()
{
    for (auto& slot : slots)
    {
        const auto state = slot.state.load (std::memory_order_acquire);

        if ((state != faulted && state != broken) || slot.reported.exchange (true))
            continue;

        const auto* key = slot.key.load (std::memory_order_acquire);
        const auto stage = (Stage) slot.stage.load();
        const bool bypassed = onFault != nullptr && onFault (key, stage, describeFault (slot.code.load()));

        if (bypassed && state == faulted)
            slot.state.store (bypassedByApp, std::memory_order_release);
    }
}
