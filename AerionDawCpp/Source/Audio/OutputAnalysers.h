#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

// Spectrum analyser and phase correlation meter for the final output (M6,
// Analysis Metering). Same split as LoudnessMeter: the audio thread only
// copies or sums into lock-free FIFOs; the message thread does the maths.

namespace Aerion
{

/** FFT spectrum of the mid signal ((L + R) / 2), 4096 points, Hann window. */
class SpectrumAnalyser
{
public:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr float kFloorDb = -100.0f;

    SpectrumAnalyser();

    void prepare (double sampleRate);

    /** Audio thread. */
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    /** Message thread: reads new samples and, once enough have arrived,
        computes a new spectrum (smoothed: fast rise, slower fall). */
    void update();

    /** dB relative to a full-scale sine, interpolated between bins. */
    float getLevelDbAt (float hz) const noexcept;

    double getSampleRate() const noexcept    { return sampleRate; }
    void reset();

private:
    double sampleRate = 48000.0;

    static constexpr int kFifoSize = kFftSize * 4;
    juce::AbstractFifo fifo { kFifoSize };
    std::vector<float> fifoData;

    std::vector<float> history;    // last kFftSize samples, oldest first
    int newSamples = 0;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData;
    std::vector<float> smoothedDb;   // kFftSize / 2 + 1 bins
};

/** Phase correlation of left and right: +1 mono-compatible, 0 unrelated,
    -1 out of phase. Windows of 50 ms, smoothed over about 300 ms. */
class CorrelationMeter
{
public:
    void prepare (double sampleRate);

    /** Audio thread. Needs two channels; mono input reads +1. */
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    /** Message thread. */
    void update();

    float getCorrelation() const noexcept    { return correlation; }
    /** False while the output is (nearly) silent: the correlation then means nothing. */
    bool hasSignal() const noexcept          { return signal; }
    void reset();

private:
    struct Window { float lr, ll, rr; };

    int windowLength = 2400;
    int samplesInWindow = 0;
    double sumLR = 0, sumLL = 0, sumRR = 0;

    static constexpr int kFifoSize = 256;
    juce::AbstractFifo fifo { kFifoSize };
    std::array<Window, kFifoSize> windows {};

    float correlation = 0.0f;
    bool signal = false;
    bool hasValue = false;
};

} // namespace Aerion
