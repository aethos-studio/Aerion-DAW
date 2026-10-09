#pragma once

#include <JuceHeader.h>
#include "LoudnessMeter.h"

// Feeds a LoudnessAnalyser from Tracktion's global output processor: the
// final device output, after the master inserts and the master fader (which
// Tracktion applies after the master plugin list). Only the first two output
// channels are measured. Nothing is added to the Edit, so projects are unchanged.

namespace Aerion
{

class LoudnessTap : public juce::AudioProcessor
{
public:
    explicit LoudnessTap (LoudnessAnalyser& a) : analyser (a) {}

    const juce::String getName() const override                     { return "Aerion Loudness Tap"; }
    void prepareToPlay (double sampleRate, int) override            { analyser.prepare (sampleRate > 0.0 ? sampleRate : 48000.0, 2); }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        analyser.process (buffer.getArrayOfReadPointers(),
                          juce::jmin (buffer.getNumChannels(), LoudnessAnalyser::kMaxChannels),
                          buffer.getNumSamples());
    }

    double getTailLengthSeconds() const override                    { return 0.0; }
    bool acceptsMidi() const override                               { return false; }
    bool producesMidi() const override                              { return false; }
    juce::AudioProcessorEditor* createEditor() override             { return nullptr; }
    bool hasEditor() const override                                 { return false; }
    int getNumPrograms() override                                   { return 1; }
    int getCurrentProgram() override                                { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override                { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    LoudnessAnalyser& analyser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessTap)
};

} // namespace Aerion
