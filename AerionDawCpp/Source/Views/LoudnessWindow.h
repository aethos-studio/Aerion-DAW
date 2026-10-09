#pragma once

// View > Loudness Meter: momentary, short-term and integrated loudness and
// the true-peak maximum of the final output, against a delivery target.

#include "ViewShared.h"

//==============================================================================
class LoudnessWindow : public juce::DocumentWindow
{
public:
    LoudnessWindow (AudioEngineManager& ae, std::function<void()> onClosed)
        : DocumentWindow ("Loudness Meter", Theme::bgPanel, DocumentWindow::closeButton),
          closed (std::move (onClosed))
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (28);
        setColour (DocumentWindow::textColourId, Theme::textMain);
        setContentOwned (new Content (ae), true);
        setResizable (false, false);
        centreWithSize (320, 300);
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
            startTimerHz (10);
        }

        void timerCallback() override   { repaint(); }

        static juce::String lufsText (float v)
        {
            return std::isinf (v) || v < -99.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"))
                                                : juce::String (v, 1);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::bgPanel);
            const auto& r = audioEngine.getLoudness();
            const auto& t = kTargets[target];

            auto b = getLocalBounds().reduced (14);

            // Target chooser
            targetBox = b.removeFromTop (26);
            g.setColour (Theme::surface);
            g.fillRoundedRectangle (targetBox.toFloat(), 4.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (targetBox.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (Theme::textMain);
            g.setFont (Theme::uiSize (10.0f));
            g.drawText ("Target: " + juce::String (t.name), targetBox.reduced (8, 0), juce::Justification::centredLeft, true);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")), targetBox.reduced (8, 0), juce::Justification::centredRight, false);
            b.removeFromTop (12);

            // The integrated value is the one a delivery target is about.
            auto big = b.removeFromTop (70);
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
            g.drawText ("INTEGRATED", big.removeFromTop (16), juce::Justification::centredLeft, false);
            g.setColour (colourFor (r.integrated, t.lufs));
            g.setFont (Theme::uiSize (30.0f).withStyle (juce::Font::bold));
            g.drawText (lufsText (r.integrated) + " LUFS", big, juce::Justification::centredLeft, false);
            b.removeFromTop (8);

            auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
            {
                auto line = b.removeFromTop (24);
                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (10.0f));
                g.drawText (label, line, juce::Justification::centredLeft, false);
                g.setColour (c);
                g.setFont (Theme::uiSize (12.0f).withStyle (juce::Font::bold));
                g.drawText (value, line, juce::Justification::centredRight, false);
            };

            row ("Short-term (3 s)",  lufsText (r.shortTerm) + " LUFS", Theme::textMain);
            row ("Momentary (400 ms)", lufsText (r.momentary) + " LUFS", Theme::textMain);
            row ("True peak max",     lufsText (r.truePeakDb) + " dBTP",
                 ! std::isinf (r.truePeakDb) && r.truePeakDb > t.truePeakDb ? Theme::recordRed : Theme::textMain);

            b.removeFromTop (8);
            auto bottom = b.removeFromTop (24);
            resetBtn = bottom.removeFromRight (80);
            g.setColour (Theme::surface);
            g.fillRoundedRectangle (resetBtn.toFloat(), 4.0f);
            g.setColour (Theme::active);
            g.drawRoundedRectangle (resetBtn.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
            g.drawText ("RESET", resetBtn, juce::Justification::centred, false);

            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (9.0f));
            g.drawText ("Integrated and true peak count while playing.", bottom, juce::Justification::centredLeft, true);
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
