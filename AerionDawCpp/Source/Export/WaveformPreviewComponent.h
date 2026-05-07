#pragma once
#include <JuceHeader.h>

class WaveformPreviewComponent : public juce::Component
{
public:
    WaveformPreviewComponent() = default;

    void setThumbnail (juce::AudioThumbnail* t)
    {
        thumb = t;
        repaint();
    }

    void analyzeForClipping (const juce::File& audioFile, juce::AudioFormatManager& afm)
    {
        clipRegions.clear();

        if (! audioFile.existsAsFile())
            return;

        // Create a reader for the audio file
        std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (audioFile));
        if (reader == nullptr)
            return;

        const int numSamples = (int) reader->lengthInSamples;
        const int blockSize = 4096;
        const float threshold = 0.99f; // Mark as clipping if peak > 0.99

        juce::AudioBuffer<float> buffer (reader->numChannels, blockSize);

        for (int pos = 0; pos < numSamples; pos += blockSize)
        {
            const int numToRead = juce::jmin (blockSize, numSamples - pos);
            if (! reader->read (&buffer, 0, numToRead, pos, true, true))
                break;

            // Check for clipping in this block
            float maxPeak = 0.0f;
            for (int ch = 0; ch < reader->numChannels; ++ch)
            {
                auto* data = buffer.getReadPointer (ch);
                for (int i = 0; i < numToRead; ++i)
                    maxPeak = juce::jmax (maxPeak, std::abs (data[i]));
            }

            if (maxPeak > threshold)
                clipRegions.push_back ({ pos, numToRead, maxPeak });
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b0f18));

        auto b = getLocalBounds().toFloat().reduced (10.0f, 10.0f);
        g.setColour (juce::Colour (0xff1a2233));
        g.fillRoundedRectangle (b, 8.0f);

        b = b.reduced (10.0f, 10.0f);
        g.setColour (juce::Colour (0xff63b3ed).withAlpha (0.9f));

        if (thumb != nullptr && thumb->getTotalLength() > 0.0)
        {
            thumb->drawChannels (g, b.toNearestInt(), 0.0, thumb->getTotalLength(), 0.8f);

            // Draw clip indicators
            drawClipIndicators (g, b);
        }
        else
        {
            g.drawFittedText ("~~~~ Waveform Preview ~~~~", b.toNearestInt(),
                              juce::Justification::centred, 1);
        }

        g.setColour (juce::Colour (0xff1a2233).brighter (0.2f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f, 10.0f), 8.0f, 1.0f);
    }

private:
    struct ClipRegion { int startSample; int numSamples; float peakLevel; };
    std::vector<ClipRegion> clipRegions;
    juce::AudioThumbnail* thumb = nullptr;

    void drawClipIndicators (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (clipRegions.empty() || thumb == nullptr)
            return;

        const double totalLength = thumb->getTotalLength();
        if (totalLength <= 0.0)
            return;

        const double sampleRate = 44100.0; // Approximate, used for visualization
        g.setColour (juce::Colour (0xffff3333).withAlpha (0.6f));

        for (const auto& clip : clipRegions)
        {
            double clipStart = clip.startSample / sampleRate;
            double clipEnd = (clip.startSample + clip.numSamples) / sampleRate;

            if (clipEnd < 0.0 || clipStart > totalLength)
                continue;

            clipStart = juce::jmax (0.0, clipStart);
            clipEnd = juce::jmin (totalLength, clipEnd);

            float xStart = bounds.getX() + (float) ((clipStart / totalLength) * bounds.getWidth());
            float xEnd = bounds.getX() + (float) ((clipEnd / totalLength) * bounds.getWidth());

            g.fillRect (juce::Rectangle<float> (xStart, bounds.getY(), xEnd - xStart, bounds.getHeight()));
        }
    }
};

