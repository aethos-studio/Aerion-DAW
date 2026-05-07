#pragma once
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

class MixdownExportJob
{
public:
    struct Result
    {
        bool ok = false;
        juce::String error;
        juce::File outputFile;
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void exportProgress (float progress01) = 0;
        virtual void exportFinished (const Result&) = 0;
    };

    MixdownExportJob (tracktion::Engine& engine,
                      tracktion::Edit& editToRender,
                      const juce::File& destination,
                      tracktion::TimeRange rangeToRender,
                      double sampleRate,
                      int numChannels,
                      int tailMs,
                      bool useEditCopy = true);

    ~MixdownExportJob();

    void start();
    void cancel();
    void waitForCancel();
    bool isRunning() const;
    float getProgress() const;
    juce::String getInitError() const { return initError; }
    bool isValid() const { return initError.isEmpty() && paramsReady; }

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    tracktion::Engine& engine;
    juce::File destFile;
    juce::String initError;

    tracktion::Renderer::Parameters cachedParams;
    bool paramsReady = false;

    std::unique_ptr<tracktion::Edit> renderEditCopy;
    std::shared_ptr<tracktion::EditRenderer::Handle> renderHandle;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixdownExportJob)
    JUCE_DECLARE_WEAK_REFERENCEABLE (MixdownExportJob)
};
