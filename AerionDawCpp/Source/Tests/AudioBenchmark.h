#pragma once
#include <JuceHeader.h>

/** AerionBench --audio: block timing, audio-thread allocations and graph
    rebuild time for a generated project. Returns the process exit code. */
int runAudioBenchmark (const juce::StringArray& args);
