#include "LoudnessMeter.h"

namespace Aerion
{

namespace
{
    // ITU-R BS.1770-4 Annex 2: 48-tap, 4x oversampling interpolation filter,
    // as four 12-tap phases.
    constexpr float kTruePeakPhases[4][12] = {
        {  0.0017089843750f,  0.0109863281250f, -0.0196533203125f,  0.0332031250000f, -0.0594482421875f,  0.1373291015625f,
           0.9721679687500f, -0.1022949218750f,  0.0476074218750f, -0.0266113281250f,  0.0148925781250f, -0.0083007812500f },
        { -0.0291748046875f,  0.0292968750000f, -0.0517578125000f,  0.0891113281250f, -0.1665039062500f,  0.4650878906250f,
           0.7797851562500f, -0.2003173828125f,  0.1015625000000f, -0.0582275390625f,  0.0330810546875f, -0.0189208984375f },
        { -0.0189208984375f,  0.0330810546875f, -0.0582275390625f,  0.1015625000000f, -0.2003173828125f,  0.7797851562500f,
           0.4650878906250f, -0.1665039062500f,  0.0891113281250f, -0.0517578125000f,  0.0292968750000f, -0.0291748046875f },
        { -0.0083007812500f,  0.0148925781250f, -0.0266113281250f,  0.0476074218750f, -0.1022949218750f,  0.9721679687500f,
           0.1373291015625f, -0.0594482421875f,  0.0332031250000f, -0.0196533203125f,  0.0109863281250f,  0.0017089843750f }
    };
}

//==============================================================================
void LoudnessAnalyser::prepare (double sampleRate, int numChannels)
{
    numChannelsInUse = juce::jlimit (1, kMaxChannels, numChannels);
    blockLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.1));

    // K-weighting, BS.1770-4: a high shelf (head effects) then a high-pass
    // (RLB curve). The coefficients are given for 48 kHz; these are the
    // analogue prototypes they come from, re-derived for any sample rate.
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double k  = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const double vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;

        for (auto& s : shelf)
        {
            s.b0 = (vh + vb * k / q + k * k) / a0;
            s.b1 = 2.0 * (k * k - vh) / a0;
            s.b2 = (vh - vb * k / q + k * k) / a0;
            s.a1 = 2.0 * (k * k - 1.0) / a0;
            s.a2 = (1.0 - k / q + k * k) / a0;
        }
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k  = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const double a0 = 1.0 + k / q + k * k;

        for (auto& h : highPass)
        {
            h.b0 = 1.0;
            h.b1 = -2.0;
            h.b2 = 1.0;
            h.a1 = 2.0 * (k * k - 1.0) / a0;
            h.a2 = (1.0 - k / q + k * k) / a0;
        }
    }

    clearState();
    fifo.reset();
    truePeak.store (0.0f);
    resetRequested.store (false);
}

void LoudnessAnalyser::clearState() noexcept
{
    for (auto& s : shelf)    s.clear();
    for (auto& h : highPass) h.clear();
    for (auto& hist : tpHistory) hist.fill (0.0f);
    tpPos.fill (0);
    samplesInBlock = 0;
    blockSum = 0.0;
}

float LoudnessAnalyser::truePeakOf (int channel, float sample) noexcept
{
    auto& hist = tpHistory[(size_t) channel];
    auto& pos  = tpPos[(size_t) channel];
    hist[(size_t) pos] = sample;

    float peak = 0.0f;
    for (const auto& phase : kTruePeakPhases)
    {
        float acc = 0.0f;
        int idx = pos;
        for (int t = 0; t < kTaps; ++t)
        {
            acc += phase[t] * hist[(size_t) idx];
            idx = idx == 0 ? kTaps - 1 : idx - 1;
        }
        peak = juce::jmax (peak, std::abs (acc));
    }

    pos = pos + 1 == kTaps ? 0 : pos + 1;
    return peak;
}

void LoudnessAnalyser::process (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
        clearState();

    const int chans = juce::jmin (numChannels, numChannelsInUse);
    if (chans <= 0 || numSamples <= 0)
        return;

    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;
        for (int c = 0; c < chans; ++c)
        {
            const float x = channels[c][i];
            const double y = highPass[(size_t) c].process (shelf[(size_t) c].process ((double) x));
            sum += y * y;   // channel weight 1.0 for left and right
            peak = juce::jmax (peak, truePeakOf (c, x));
        }

        blockSum += sum;

        if (++samplesInBlock == blockLength)
        {
            const auto scope = fifo.write (1);
            if (scope.blockSize1 > 0)
                blocks[(size_t) scope.startIndex1] = (float) (blockSum / (double) blockLength);
            // When the message thread falls far behind, newer blocks are dropped.

            samplesInBlock = 0;
            blockSum = 0.0;
        }
    }

    // Lock-free max.
    auto current = truePeak.load();
    while (peak > current && ! truePeak.compare_exchange_weak (current, peak)) {}
}

int LoudnessAnalyser::popBlocks (float* dest, int maxBlocks) noexcept
{
    const auto scope = fifo.read (juce::jmin (maxBlocks, fifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = blocks[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = blocks[(size_t) (scope.startIndex2 + i)];
    return n;
}

//==============================================================================
LoudnessMeter::LoudnessMeter()
    : binCount ((size_t) kNumBins, 0), binEnergy ((size_t) kNumBins, 0.0)
{
}

float LoudnessMeter::toLufs (double meanSquare) noexcept
{
    return meanSquare > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare))
                            : LoudnessReadings::kSilence;
}

void LoudnessMeter::update (LoudnessAnalyser& analyser, bool accumulate)
{
    float buffer[128];
    for (int n; (n = analyser.popBlocks (buffer, (int) std::size (buffer))) > 0;)
        for (int i = 0; i < n; ++i)
            addBlock (buffer[i], accumulate);

    const float tp = analyser.getAndResetTruePeak();
    if (accumulate)
        maxTruePeak = juce::jmax (maxTruePeak, tp);
    readings.truePeakDb = maxTruePeak > 0.0f ? juce::Decibels::gainToDecibels (maxTruePeak, -300.0f)
                                             : LoudnessReadings::kSilence;
}

void LoudnessMeter::addBlock (float energy, bool accumulate)
{
    recent[(size_t) recentPos] = energy;
    recentPos = (recentPos + 1) % kShortTermBlocks;
    recentCount = juce::jmin (recentCount + 1, kShortTermBlocks);

    auto meanOfLast = [this] (int count)
    {
        double sum = 0.0;
        for (int i = 1; i <= count; ++i)
            sum += recent[(size_t) ((recentPos - i + kShortTermBlocks) % kShortTermBlocks)];
        return sum / (double) count;
    };

    if (recentCount < kMomentaryBlocks)
        return;

    const double momentaryEnergy = meanOfLast (kMomentaryBlocks);
    readings.momentary = toLufs (momentaryEnergy);
    // Short-term needs its full 3 s; until then it shows what there is.
    readings.shortTerm = toLufs (meanOfLast (recentCount));

    if (! accumulate)
        return;

    // Each 400 ms window, stepping 100 ms (75 % overlap), is one gating block.
    const float blockLufs = readings.momentary;
    if (blockLufs > kHistMin)   // absolute gate, -70 LUFS
    {
        const int bin = juce::jlimit (0, kNumBins - 1, (int) ((blockLufs - kHistMin) / kBinLu));
        ++binCount[(size_t) bin];
        binEnergy[(size_t) bin] += momentaryEnergy;
    }

    // Relative gate: 10 LU below the mean of the blocks above the absolute gate.
    double sum = 0.0;
    int count = 0;
    for (int b = 0; b < kNumBins; ++b)
    {
        sum += binEnergy[(size_t) b];
        count += binCount[(size_t) b];
    }

    if (count == 0)
    {
        readings.integrated = LoudnessReadings::kSilence;
        return;
    }

    const float relativeGate = toLufs (sum / count) - 10.0f;
    // Bins are 0.1 LU wide; a bin counts when its centre is above the gate.
    const int firstBin = juce::jlimit (0, kNumBins, (int) std::ceil ((relativeGate - kHistMin) / kBinLu - 0.5f));

    double gatedSum = 0.0;
    int gatedCount = 0;
    for (int b = firstBin; b < kNumBins; ++b)
    {
        gatedSum += binEnergy[(size_t) b];
        gatedCount += binCount[(size_t) b];
    }

    readings.integrated = gatedCount > 0 ? toLufs (gatedSum / gatedCount) : LoudnessReadings::kSilence;
}

void LoudnessMeter::resetIntegrated()
{
    std::fill (binCount.begin(), binCount.end(), 0);
    std::fill (binEnergy.begin(), binEnergy.end(), 0.0);
    maxTruePeak = 0.0f;
    readings.integrated = LoudnessReadings::kSilence;
    readings.truePeakDb = LoudnessReadings::kSilence;
}

} // namespace Aerion
