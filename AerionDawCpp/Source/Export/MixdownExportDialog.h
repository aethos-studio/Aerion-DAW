#pragma once
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include <optional>

#include "../UIComponents.h"
#include "../UI/ThemeTokens.h"
#include "MixdownExportJob.h"
#include "WaveformPreviewComponent.h"

class MixdownExportDialog : public juce::Component,
                            private juce::Timer,
                            private MixdownExportJob::Listener
{
public:
    enum class BoundsMode { SelectionOrLoopOrFull = 0, Loop, Full };

    MixdownExportDialog (tracktion::Engine& engine,
                         tracktion::Edit& edit,
                         juce::String projectNameForWildcards,
                         std::optional<tracktion::TimeRange> selectedTimeRange = std::nullopt);

    ~MixdownExportDialog() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void requestPreviewRestart();
    void timerCallback() override;
    void exportProgress (float progress01) override;
    void exportFinished (const MixdownExportJob::Result&) override;

    void rebuildFormatList();
    void startPreviewRender();
    void startExportRender();
    void cancelRenders();
    void cancelPreview();
    void cancelExport();
    void showPresetsMenu();
    void showThemedAlert (const juce::String& title, const juce::String& message, bool isError = false);

    tracktion::TimeRange computeBounds() const;
    juce::File expandedOutputFile() const;
    juce::String applyWildcards (juce::String pattern) const;

    tracktion::Engine& engine;
    tracktion::Edit& edit;
    const juce::String projectName;
    std::optional<tracktion::TimeRange> selectionRange;

    // UI
    juce::Label title { {}, "Render to File" };

    juce::ComboBox sourceBox;
    juce::ComboBox boundsBox;
    juce::ToggleButton tailToggle { "Tail" };
    juce::Slider tailMs;

    juce::Label dirLabel { {}, "Directory" };
    juce::TextEditor dirEdit;
    juce::TextButton browseDir { "Browse…" };

    juce::Label nameLabel { {}, "File name" };
    juce::TextEditor nameEdit;
    juce::TextButton wildcardsBtn { "Wildcards" };

    juce::Label formatLabel { {}, "Format" };
    juce::ComboBox formatBox;

    juce::Label srLabel { {}, "Sample rate" };
    juce::ComboBox sampleRateBox;

    juce::Label chLabel { {}, "Channels" };
    juce::ComboBox channelsBox;

    juce::TextButton presetsBtn { "Presets" };
    juce::TextButton renderBtn  { "Render 1 file" };
    juce::TextButton cancelBtn  { "Cancel" };

    juce::ProgressBar progressBar;
    double progressValue = 0.0;

    // Preview render (true pre-render to temp file)
    juce::File previewFile;
    std::unique_ptr<MixdownExportJob> previewJob;
    std::unique_ptr<WaveformPreviewComponent> waveform;

    // Export render
    std::unique_ptr<MixdownExportJob> exportJob;
    juce::AudioFormatManager audioFormatManager;
    juce::AudioThumbnailCache thumbnailCache { 512 };
    juce::AudioThumbnail exportThumbnail { 512, audioFormatManager, thumbnailCache };

    std::atomic<uint32_t> previewRestartToken { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixdownExportDialog)
};

