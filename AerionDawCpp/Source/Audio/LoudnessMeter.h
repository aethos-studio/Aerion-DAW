#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

// Loudness metering per ITU-R BS.1770-4 / EBU R128 (M6, Analysis Metering).
//
// Split in two so the audio thread only does fixed, allocation-free work:
// LoudnessAnalyser K-weights the signal, sums 100 ms blocks and tracks the
// true peak; LoudnessMeter, on the message thread, turns those blocks into
// momentary, short-term and integrated loudness.

namespace Aerion
{

/** Audio-thread half. prepare() before process(); process() is real-time safe. */
class LoudnessAnalyser
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int numChannels);

    /** Audio thread. Extra channels beyond kMaxChannels are ignored. */
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    /** Message thread: moves finished 100 ms block energies (channel-summed
        mean squares) into dest, oldest first. Returns how many. */
    int popBlocks (float* dest, int maxBlocks) noexcept;

    /** Message thread: the highest true peak (linear) since the last call. */
    float getAndResetTruePeak() noexcept    { return truePeak.exchange (0.0f); }

    /** Any thread: clears filter state and pending blocks at the next process(). */
    void requestReset() noexcept            { resetRequested.store (true); }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept   { z1 = z2 = 0; }
    };

    static constexpr int kTaps = 12;   // per phase of the BS.1770 Annex 2 true-peak filter

    void clearState() noexcept;
    float truePeakOf (int channel, float sample) noexcept;

    std::array<Biquad, kMaxChannels> shelf, highPass;
    std::array<std::array<float, kTaps>, kMaxChannels> tpHistory {};
    std::array<int, kMaxChannels> tpPos {};

    int numChannelsInUse = 2;
    int blockLength = 4800;   // samples per 100 ms
    int samplesInBlock = 0;
    double blockSum = 0.0;

    static constexpr int kFifoSize = 1024;
    juce::AbstractFifo fifo { kFifoSize };
    std::array<float, kFifoSize> blocks {};

    std::atomic<float> truePeak { 0.0f };
    std::atomic<bool> resetRequested { false };
};

struct LoudnessReadings
{
    static constexpr float kSilence = -std::numeric_limits<float>::infinity();

    float momentary  = kSilence;   // LUFS, 400 ms
    float shortTerm  = kSilence;   // LUFS, 3 s
    float integrated = kSilence;   // LUFS, gated, since the last reset
    float truePeakDb = kSilence;   // dBTP, maximum since the last reset
};

/** Message-thread half. Call update() regularly (each UI frame is fine). */
class LoudnessMeter
{
public:
    LoudnessMeter();

    /** Reads new blocks from the analyser. Integrated loudness and the true
        peak maximum only take in audio while accumulate is true (playback). */
    void update (LoudnessAnalyser& analyser, bool accumulate);

    void resetIntegrated();
    const LoudnessReadings& getReadings() const noexcept   { return readings; }

    /** Converts a mean square to LUFS (BS.1770: -0.691 + 10 log10). */
    static float toLufs (double meanSquare) noexcept;

private:
    void addBlock (float energy, bool accumulate);

    static constexpr int kShortTermBlocks = 30;
    static constexpr int kMomentaryBlocks = 4;
    std::array<float, kShortTermBlocks> recent {};
    int recentCount = 0, recentPos = 0;

    // Gating blocks (400 ms, every 100 ms) above the absolute gate, as a
    // histogram over loudness in 0.1 LU bins, so memory stays fixed.
    static constexpr float kHistMin = -70.0f, kHistMax = 10.0f, kBinLu = 0.1f;
    static constexpr int kNumBins = (int) ((kHistMax - kHistMin) / kBinLu);
    std::vector<int>    binCount;
    std::vector<double> binEnergy;

    float maxTruePeak = 0.0f;
    LoudnessReadings readings;
};

} // namespace Aerion
