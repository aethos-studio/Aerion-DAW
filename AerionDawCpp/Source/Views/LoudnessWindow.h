#pragma once

// View > Meters: loudness (momentary, short-term, integrated, true peak)
// against a delivery target, phase correlation and a spectrum of the final
// output, after the master plugins and the master fader.

#include "ViewShared.h"

//==============================================================================
class LoudnessWindow : public juce::DocumentWindow
{
public:
    LoudnessWindow (AudioEngineManager& ae, std::function<void()> onClosed)
        : DocumentWindow ("Meters", Theme::bgPanel, DocumentWindow::closeButton),
          closed (std::move (onClosed))
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (28);
        setColour (DocumentWindow::textColourId, Theme::textMain);
        setContentOwned (new Content (ae), true);
        setResizable (true, false);
        setResizeLimits (480, 440, 1400, 900);
        centreWithSize (600, 500);
        setAlwaysOnTop (true);
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (closed) closed();   // the owner deletes this window
    }

    struct Target
    {
        const char* name;
        float lufs, truePeakDb;
    };

    static constexpr Target kTargets[] = {
        { "Spotify / YouTube  (-14 LUFS)",  -14.0f, -1.0f },
        { "Apple Music  (-16 LUFS)",        -16.0f, -1.0f },
        { "EBU R128 broadcast  (-23 LUFS)", -23.0f, -1.0f },
        { "ATSC A/85  (-24 LUFS)",          -24.0f, -2.0f },
    };
    static constexpr const char* kTargetSettingKey = "loudnessTarget";

private:
    struct Content : public juce::Component, private juce::Timer
    {
        explicit Content (AudioEngineManager& ae) : audioEngine (ae)
        {
            if (auto* s = audioEngine.getUserSettings())
                target = juce::jlimit (0, (int) std::size (kTargets) - 1, s->getIntValue (kTargetSettingKey, 0));
            startTimerHz (30);
        }

        void timerCallback() override   { repaint(); }

        static juce::String lufsText (float v)
        {
            return std::isinf (v) || v < -99.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"))
                                                : juce::String (v, 1);
        }

        static void sectionLabel (juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& text)
        {
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
            g.drawText (text, area.removeFromTop (16), juce::Justification::centredLeft, false);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::bgPanel);
            const auto& r = audioEngine.getLoudness();
            const auto& t = kTargets[target];

            auto b = getLocalBounds().reduced (14);

            // Target chooser and reset
            auto topRow = b.removeFromTop (26);
            resetBtn = topRow.removeFromRight (80);
            topRow.removeFromRight (8);
            targetBox = topRow;
            g.setColour (Theme::surface);
            g.fillRoundedRectangle (targetBox.toFloat(), 4.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (targetBox.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (Theme::textMain);
            g.setFont (Theme::uiSize (10.0f));
            g.drawText ("Target: " + juce::String (t.name), targetBox.reduced (8, 0), juce::Justification::centredLeft, true);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")), targetBox.reduced (8, 0), juce::Justification::centredRight, false);

            g.setColour (Theme::surface);
            g.fillRoundedRectangle (resetBtn.toFloat(), 4.0f);
            g.setColour (Theme::active);
            g.drawRoundedRectangle (resetBtn.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
            g.drawText ("RESET", resetBtn, juce::Justification::centred, false);
            b.removeFromTop (12);

            // Loudness (left) and phase correlation (right)
            auto upper = b.removeFromTop (150);
            auto loud = upper.removeFromLeft (upper.getWidth() / 2);
            upper.removeFromLeft (16);
            paintLoudness (g, loud, r, t);
            paintCorrelation (g, upper);

            b.removeFromTop (10);
            auto note = b.removeFromBottom (16);
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (9.0f));
            g.drawText ("Integrated and true peak count while playing. Measures the first two output channels.",
                        note, juce::Justification::centredLeft, true);
            b.removeFromBottom (6);

            sectionLabel (g, b, "SPECTRUM");
            paintSpectrum (g, b);
        }

        void paintLoudness (juce::Graphics& g, juce::Rectangle<int> area,
                            const Aerion::LoudnessReadings& r, const Target& t)
        {
            sectionLabel (g, area, "INTEGRATED");
            g.setColour (colourFor (r.integrated, t.lufs));
            g.setFont (Theme::uiSize (24.0f).withStyle (juce::Font::bold));
            g.drawText (lufsText (r.integrated) + " LUFS", area.removeFromTop (40), juce::Justification::centredLeft, false);
            area.removeFromTop (6);

            auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
            {
                auto line = area.removeFromTop (22);
                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (10.0f));
                g.drawText (label, line, juce::Justification::centredLeft, false);
                g.setColour (c);
                g.setFont (Theme::uiSize (11.0f).withStyle (juce::Font::bold));
                g.drawText (value, line, juce::Justification::centredRight, false);
            };

            row ("Short-term (3 s)",   lufsText (r.shortTerm) + " LUFS", Theme::textMain);
            row ("Momentary (400 ms)", lufsText (r.momentary) + " LUFS", Theme::textMain);
            row ("True peak max",      lufsText (r.truePeakDb) + " dBTP",
                 ! std::isinf (r.truePeakDb) && r.truePeakDb > t.truePeakDb ? Theme::recordRed : Theme::textMain);
        }

        void paintCorrelation (juce::Graphics& g, juce::Rectangle<int> area)
        {
            const auto& c = audioEngine.getCorrelation();
            sectionLabel (g, area, "PHASE CORRELATION");

            const float value = c.getCorrelation();
            g.setColour (! c.hasSignal() ? Theme::textMuted : value < 0.0f ? Theme::recordRed : Theme::textMain);
            g.setFont (Theme::uiSize (24.0f).withStyle (juce::Font::bold));
            g.drawText (c.hasSignal() ? (value >= 0.0f ? "+" : "") + juce::String (value, 2)
                                      : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                        area.removeFromTop (40), juce::Justification::centredLeft, false);
            area.removeFromTop (10);

            // Bar from -1 to +1, marker at the reading.
            auto bar = area.removeFromTop (14).toFloat();
            g.setColour (Theme::surface);
            g.fillRoundedRectangle (bar, 3.0f);
            const float mid = bar.getCentreX();
            g.setColour (Theme::border);
            g.drawVerticalLine ((int) mid, bar.getY(), bar.getBottom());

            if (c.hasSignal())
            {
                const float x = juce::jmap (value, -1.0f, 1.0f, bar.getX(), bar.getRight());
                auto fill = value >= 0.0f ? juce::Rectangle<float> (mid, bar.getY(), x - mid, bar.getHeight())
                                          : juce::Rectangle<float> (x, bar.getY(), mid - x, bar.getHeight());
                g.setColour ((value < 0.0f ? Theme::recordRed : Theme::active).withAlpha (0.7f));
                g.fillRect (fill.reduced (0.0f, 2.0f));
                g.setColour (Theme::textMain);
                g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
            }

            auto scale = area.removeFromTop (16);
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (8.5f));
            g.drawText ("-1", scale, juce::Justification::centredLeft, false);
            g.drawText ("0", scale, juce::Justification::centred, false);
            g.drawText ("+1", scale, juce::Justification::centredRight, false);

            area.removeFromTop (6);
            g.setFont (Theme::uiSize (9.0f));
            g.drawFittedText ("+1 is mono-compatible. Below 0, parts of the mix cancel when played in mono.",
                              area.removeFromTop (30), juce::Justification::topLeft, 2);
        }

        void paintSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
        {
            const auto& spectrum = audioEngine.getSpectrum();
            auto plot = area.toFloat();
            g.setColour (Theme::bgPanel.darker (0.3f));
            g.fillRoundedRectangle (plot, 4.0f);
            plot = plot.reduced (4.0f, 4.0f).withTrimmedBottom (12.0f);

            constexpr float minHz = 20.0f, maxHz = 20000.0f, minDb = -90.0f, maxDb = 0.0f;
            auto xForHz = [&] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz); };
            auto yForDb = [&] (float db) { return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, plot.getBottom(), plot.getY()); };

            // Grid
            g.setFont (Theme::uiSize (8.0f));
            for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                const float x = xForHz (hz);
                g.setColour (Theme::border.withAlpha (0.4f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                g.setColour (Theme::textMuted);
                g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x - 16.0f, plot.getBottom() + 1.0f, 32.0f, 11.0f),
                            juce::Justification::centred, false);
            }
            for (float db : { -20.0f, -40.0f, -60.0f, -80.0f })
            {
                const float y = yForDb (db);
                g.setColour (Theme::border.withAlpha (0.4f));
                g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
                g.setColour (Theme::textMuted);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() + 2.0f, y - 11.0f, 30.0f, 10.0f),
                            juce::Justification::centredLeft, false);
            }

            // Curve
            juce::Path curve;
            const int steps = juce::jmax (2, (int) plot.getWidth());
            for (int i = 0; i <= steps; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
                const float hz = minHz * std::pow (maxHz / minHz, (float) i / (float) steps);
                const float y = yForDb (spectrum.getLevelDbAt (hz));
                if (i == 0) curve.startNewSubPath (x, y);
                else        curve.lineTo (x, y);
            }

            juce::Path fill (curve);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();
            g.setColour (Theme::active.withAlpha (0.18f));
            g.fillPath (fill);
            g.setColour (Theme::active);
            g.strokePath (curve, juce::PathStrokeType (1.5f));
        }

        /** Within 1 LU of the target is on target; louder is red, quieter yellow. */
        static juce::Colour colourFor (float lufs, float targetLufs)
        {
            if (std::isinf (lufs)) return Theme::textMuted;
            if (lufs > targetLufs + 1.0f) return Theme::recordRed;
            if (lufs < targetLufs - 1.0f) return Theme::meterYellow;
            return Theme::active;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (resetBtn.contains (e.getPosition()))
            {
                audioEngine.resetLoudness();
                repaint();
                return;
            }

            if (targetBox.contains (e.getPosition()))
            {
                juce::PopupMenu m;
                for (int i = 0; i < (int) std::size (kTargets); ++i)
                    m.addItem (i + 1, kTargets[i].name, true, i == target);

                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (targetBox)),
                    [safe = juce::Component::SafePointer<Content> (this)] (int chosen)
                    {
                        if (safe == nullptr || chosen <= 0) return;
                        safe->target = chosen - 1;
                        if (auto* s = safe->audioEngine.getUserSettings())
                            s->setValue (kTargetSettingKey, safe->target);
                        safe->repaint();
                    });
            }
        }

        AudioEngineManager& audioEngine;
        int target = 0;
        juce::Rectangle<int> targetBox, resetBtn;
    };

    std::function<void()> closed;
};
