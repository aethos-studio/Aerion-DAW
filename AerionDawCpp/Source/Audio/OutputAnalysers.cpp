#include "OutputAnalysers.h"

namespace Aerion
{

//==============================================================================
SpectrumAnalyser::SpectrumAnalyser()
    : fifoData ((size_t) kFifoSize, 0.0f),
      history ((size_t) kFftSize, 0.0f),
      fftData ((size_t) kFftSize * 2, 0.0f),
      smoothedDb ((size_t) kFftSize / 2 + 1, kFloorDb)
{
}

void SpectrumAnalyser::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    fifo.reset();
}

void SpectrumAnalyser::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (smoothedDb.begin(), smoothedDb.end(), kFloorDb);
    newSamples = 0;
}

void SpectrumAnalyser::process (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const auto scope = fifo.write (juce::jmin (numSamples, fifo.getFreeSpace()));
    auto mid = [&] (int i)
    {
        return numChannels >= 2 ? 0.5f * (channels[0][i] + channels[1][i]) : channels[0][i];
    };

    for (int i = 0; i < scope.blockSize1; ++i) fifoData[(size_t) (scope.startIndex1 + i)] = mid (i);
    for (int i = 0; i < scope.blockSize2; ++i) fifoData[(size_t) (scope.startIndex2 + i)] = mid (scope.blockSize1 + i);
}

void SpectrumAnalyser::update()
{
    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        // Keep the newest kFftSize samples.
        const int take = juce::jmin (ready, kFftSize);
        const int skip = ready - take;
        if (skip > 0)
            fifo.read (skip);   // discard: the analyser fell behind

        std::move (history.begin() + take, history.end(), history.begin());
        const auto scope = fifo.read (take);
        auto dest = history.end() - take;
        for (int i = 0; i < scope.blockSize1; ++i) *dest++ = fifoData[(size_t) (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i) *dest++ = fifoData[(size_t) (scope.startIndex2 + i)];
        newSamples += ready;
    }

    // A new spectrum every half window (50 % overlap) at most.
    if (newSamples < kFftSize / 2)
        return;
    newSamples = 0;

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (history.begin(), history.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    // A full-scale sine gives a peak of kFftSize / 4 with a Hann window.
    const float reference = (float) kFftSize / 4.0f;
    for (size_t b = 0; b < smoothedDb.size(); ++b)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[b] / reference, kFloorDb);
        auto& s = smoothedDb[b];
        s = db > s ? db : s + (db - s) * 0.25f;   // rise at once, fall gently
    }
}

float SpectrumAnalyser::getLevelDbAt (float hz) const noexcept
{
    const float binF = hz * (float) kFftSize / (float) sampleRate;
    const int maxBin = (int) smoothedDb.size() - 1;
    if (binF <= 0.0f)  return smoothedDb.front();
    if (binF >= (float) maxBin) return smoothedDb.back();

    const int b = (int) binF;
    const float frac = binF - (float) b;
    return smoothedDb[(size_t) b] + frac * (smoothedDb[(size_t) b + 1] - smoothedDb[(size_t) b]);
}

//==============================================================================
void CorrelationMeter::prepare (double sampleRate)
{
    windowLength = juce::jmax (1, juce::roundToInt ((sampleRate > 0.0 ? sampleRate : 48000.0) * 0.05));
    samplesInWindow = 0;
    sumLR = sumLL = sumRR = 0;
    fifo.reset();
}

void CorrelationMeter::reset()
{
    correlation = 0.0f;
    signal = false;
    hasValue = false;
}

void CorrelationMeter::process (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float* left  = channels[0];
    const float* right = numChannels >= 2 ? channels[1] : channels[0];

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = left[i], r = right[i];
        sumLR += l * r;
        sumLL += l * l;
        sumRR += r * r;

        if (++samplesInWindow == windowLength)
        {
            const auto scope = fifo.write (1);
            if (scope.blockSize1 > 0)
                windows[(size_t) scope.startIndex1] = { (float) sumLR, (float) sumLL, (float) sumRR };

            samplesInWindow = 0;
            sumLR = sumLL = sumRR = 0;
        }
    }
}

void CorrelationMeter::update()
{
    const int ready = fifo.getNumReady();
    if (ready == 0)
        return;

    const auto scope = fifo.read (ready);
    auto take = [this] (const Window& w)
    {
        // Below about -80 dBFS RMS in both channels there is nothing to correlate.
        const double meanSquare = ((double) w.ll + (double) w.rr) / (2.0 * windowLength);
        const bool hasSignalNow = meanSquare > 1.0e-8;
        signal = hasSignalNow;
        if (! hasSignalNow)
            return;

        const double denom = std::sqrt ((double) w.ll * (double) w.rr);
        const float c = denom > 0.0 ? (float) juce::jlimit (-1.0, 1.0, (double) w.lr / denom) : 0.0f;

        // About 300 ms to settle with 50 ms windows.
        correlation = hasValue ? correlation + (c - correlation) * 0.3f : c;
        hasValue = true;
    };

    for (int i = 0; i < scope.blockSize1; ++i) take (windows[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) take (windows[(size_t) (scope.startIndex2 + i)]);
}

} // namespace Aerion
