#include <JuceHeader.h>
#include "../Audio/LoudnessMeter.h"

// LoudnessMeter against reference signals in the style of EBU Tech 3341
// (loudness) and the BS.1770 true-peak cases.

class LoudnessTests final : public juce::UnitTest
{
public:
    LoudnessTests() : juce::UnitTest ("Loudness", "Aerion") {}

    static constexpr double kSampleRate = 48000.0;

    /** Feeds `seconds` of a stereo signal, in 512-sample blocks, updating the
        meter once per second as the UI timer would. */
    template <typename SampleFn>
    static void feed (Aerion::LoudnessAnalyser& analyser, Aerion::LoudnessMeter& meter,
                      double seconds, SampleFn&& sampleAt, bool accumulate = true, int startSample = 0)
    {
        const int total = (int) (seconds * kSampleRate);
        juce::AudioBuffer<float> buffer (2, 512);

        for (int done = 0; done < total;)
        {
            const int n = juce::jmin (512, total - done);
            for (int i = 0; i < n; ++i)
            {
                const float s = sampleAt (startSample + done + i);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            analyser.process (buffer.getArrayOfReadPointers(), 2, n);
            done += n;

            if (done % (int) kSampleRate < 512 || done == total)
                meter.update (analyser, accumulate);
        }
    }

    static auto sine (double hz, float dbfs, double phase = 0.0)
    {
        const float amp = juce::Decibels::decibelsToGain (dbfs);
        return [=] (int n) { return amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * n / kSampleRate + phase); };
    }

    void runTest() override
    {
        beginTest ("a stereo 1 kHz sine at -23 dBFS reads -23 LUFS (EBU Tech 3341 case 1)");
        {
            Aerion::LoudnessAnalyser analyser;
            Aerion::LoudnessMeter meter;
            analyser.prepare (kSampleRate, 2);
            feed (analyser, meter, 20.0, sine (1000.0, -23.0f));

            const auto& r = meter.getReadings();
            expectWithinAbsoluteError (r.integrated, -23.0f, 0.1f);
            expectWithinAbsoluteError (r.shortTerm,  -23.0f, 0.1f);
            expectWithinAbsoluteError (r.momentary,  -23.0f, 0.1f);
        }

        beginTest ("a stereo 1 kHz sine at -33 dBFS reads -33 LUFS (case 2), also at 44.1 kHz");
        {
            Aerion::LoudnessAnalyser analyser;
            Aerion::LoudnessMeter meter;
            analyser.prepare (kSampleRate, 2);
            feed (analyser, meter, 20.0, sine (1000.0, -33.0f));
            expectWithinAbsoluteError (meter.getReadings().integrated, -33.0f, 0.1f);

            // The K-weighting is re-derived for other rates.
            Aerion::LoudnessAnalyser a441;
            Aerion::LoudnessMeter m441;
            a441.prepare (44100.0, 2);
            const float amp = juce::Decibels::decibelsToGain (-23.0f);
            juce::AudioBuffer<float> buffer (2, 441);
            for (int block = 0; block < 1000; ++block)   // 10 s
            {
                for (int i = 0; i < 441; ++i)
                {
                    const float s = amp * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (block * 441 + i) / 44100.0);
                    buffer.setSample (0, i, s);
                    buffer.setSample (1, i, s);
                }
                a441.process (buffer.getArrayOfReadPointers(), 2, 441);
                if (block % 100 == 99)
                    m441.update (a441, true);
            }
            expectWithinAbsoluteError (m441.getReadings().integrated, -23.0f, 0.1f);
        }

        beginTest ("silence is gated out");
        {
            Aerion::LoudnessAnalyser analyser;
            Aerion::LoudnessMeter meter;
            analyser.prepare (kSampleRate, 2);
            feed (analyser, meter, 5.0, [] (int) { return 0.0f; });
            expect (std::isinf (meter.getReadings().integrated));
            expect (std::isinf (meter.getReadings().truePeakDb));
        }

        beginTest ("quiet passages below the relative gate do not lower integrated loudness");
        {
            Aerion::LoudnessAnalyser analyser;
            Aerion::LoudnessMeter meter;
            analyser.prepare (kSampleRate, 2);
            feed (analyser, meter, 10.0, sine (1000.0, -20.0f));
            // -50 LUFS is above the absolute gate (-70) but 30 LU under the loud part.
            feed (analyser, meter, 10.0, sine (1000.0, -50.0f), true, (int) (10.0 * kSampleRate));
            expectWithinAbsoluteError (meter.getReadings().integrated, -20.0f, 0.15f);
            expectWithinAbsoluteError (meter.getReadings().shortTerm, -50.0f, 0.1f);
        }

        beginTest ("integrated loudness only counts while accumulating, and resets");
        {
            Aerion::LoudnessAnalyser analyser;
            Aerion::LoudnessMeter meter;
            analyser.prepare (kSampleRate, 2);
            feed (analyser, meter, 3.0, sine (1000.0, -23.0f), false);
            expect (std::isinf (meter.getReadings().integrated));
            expectWithinAbsoluteError (meter.getReadings().momentary, -23.0f, 0.1f);

            feed (analyser, meter, 3.0, sine (1000.0, -23.0f), true);
            expect (! std::isinf (meter.getReadings().integrated));

            meter.resetIntegrated();
            expect (std::isinf (meter.getReadings().integrated));
            expect (std::isinf (meter.getReadings().truePeakDb));
        }

        beginTest ("true peak finds the peak between samples");
        {
            // A full-scale sine at fs/4, 45 degrees out of phase: every sample is
            // at +-0.707 (-3 dB), but the waveform between them reaches 0 dB.
            Aerion::LoudnessAnalyser analyser;
            Aerion::LoudnessMeter meter;
            analyser.prepare (kSampleRate, 2);
            feed (analyser, meter, 1.0, sine (kSampleRate / 4.0, 0.0f, juce::MathConstants<double>::pi / 4.0));

            const float tp = meter.getReadings().truePeakDb;
            expect (tp > -0.6f && tp < 0.6f, "true peak was " + juce::String (tp, 2) + " dBTP");
        }
    }
};

static LoudnessTests loudnessTests;
