#pragma once
#include <JuceHeader.h>

class AIManager : public juce::Thread
{
public:
    AIManager(tracktion::Edit& edit);
    ~AIManager() override;

    void convertAudioToMidi(tracktion::WaveAudioClip& audioClip);

    // From juce::Thread
    void run() override;

private:
    tracktion::Edit& edit;
    tracktion::WaveAudioClip* clipToProcess = nullptr;

    void processTranscription();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIManager)
};
