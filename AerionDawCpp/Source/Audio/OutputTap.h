#pragma once

#include <JuceHeader.h>
#include "LoudnessMeter.h"
#include "OutputAnalysers.h"

// Feeds the output analysers (loudness, spectrum, phase correlation) from
// Tracktion's global output processor: the final device output, after the
// master inserts and the master fader (which Tracktion applies after the
// master plugin list). Only the first two output channels are measured.
// Nothing is added to the Edit, so projects are unchanged.

namespace Aerion
{

class OutputTap : public juce::AudioProcessor
{
public:
    OutputTap (LoudnessAnalyser& l, SpectrumAnalyser& s, CorrelationMeter& c)
        : loudness (l), spectrum (s), correlation (c) {}

    const juce::String getName() const override                     { return "Aerion Output Tap"; }

    void prepareToPlay (double sampleRate, int) override
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        loudness.prepare (sr, 2);
        spectrum.prepare (sr);
        correlation.prepare (sr);
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto* const* channels = buffer.getArrayOfReadPointers();
        const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
        const int numSamples  = buffer.getNumSamples();

        loudness.process (channels, numChannels, numSamples);
        spectrum.process (channels, numChannels, numSamples);
        correlation.process (channels, numChannels, numSamples);
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
    LoudnessAnalyser& loudness;
    SpectrumAnalyser& spectrum;
    CorrelationMeter& correlation;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputTap)
};

} // namespace Aerion
