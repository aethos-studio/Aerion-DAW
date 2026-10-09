#include <JuceHeader.h>
#include "../Audio/OutputAnalysers.h"

// Spectrum analyser and phase correlation meter against generated signals.

class OutputAnalyserTests final : public juce::UnitTest
{
public:
    OutputAnalyserTests() : juce::UnitTest ("Output analysers", "Aerion") {}

    static constexpr double kSampleRate = 48000.0;

    /** Feeds one second of stereo audio in 512-sample blocks, updating as the UI timer would. */
    template <typename Analyser, typename LeftFn, typename RightFn>
    static void feed (Analyser& a, LeftFn&& left, RightFn&& right, double seconds = 1.0)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        const int total = (int) (seconds * kSampleRate);

        for (int done = 0; done < total; done += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                buffer.setSample (0, i, left (done + i));
                buffer.setSample (1, i, right (done + i));
            }
            a.process (buffer.getArrayOfReadPointers(), 2, 512);
            a.update();
        }
    }

    static auto sine (double hz, float amp, double phase = 0.0)
    {
        return [=] (int n) { return amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * n / kSampleRate + phase); };
    }

    void runTest() override
    {
        beginTest ("the spectrum peaks at a sine's frequency and level");
        {
            Aerion::SpectrumAnalyser s;
            s.prepare (kSampleRate);
            auto tone = sine (1000.0, 0.5f);   // -6 dBFS
            feed (s, tone, tone);

            const float at1k = s.getLevelDbAt (1000.0f);
            expectWithinAbsoluteError (at1k, -6.0f, 1.5f);
            expect (s.getLevelDbAt (100.0f) < at1k - 40.0f, "100 Hz should be far below the 1 kHz peak");
            expect (s.getLevelDbAt (10000.0f) < at1k - 40.0f, "10 kHz should be far below the 1 kHz peak");

            s.reset();
            expect (s.getLevelDbAt (1000.0f) <= Aerion::SpectrumAnalyser::kFloorDb + 0.01f);
        }

        beginTest ("phase correlation: identical +1, inverted -1, unrelated near 0");
        {
            Aerion::CorrelationMeter same, inverted, unrelated, silent;
            for (auto* m : { &same, &inverted, &unrelated, &silent })
                m->prepare (kSampleRate);

            auto tone = sine (440.0, 0.5f);
            feed (same, tone, tone);
            expectWithinAbsoluteError (same.getCorrelation(), 1.0f, 0.01f);
            expect (same.hasSignal());

            feed (inverted, tone, [&] (int n) { return -tone (n); });
            expectWithinAbsoluteError (inverted.getCorrelation(), -1.0f, 0.01f);

            juce::Random a (1), b (2);
            feed (unrelated, [&] (int) { return a.nextFloat() - 0.5f; }, [&] (int) { return b.nextFloat() - 0.5f; });
            expectWithinAbsoluteError (unrelated.getCorrelation(), 0.0f, 0.1f);

            feed (silent, [] (int) { return 0.0f; }, [] (int) { return 0.0f; });
            expect (! silent.hasSignal());
        }
    }
};

static OutputAnalyserTests outputAnalyserTests;
