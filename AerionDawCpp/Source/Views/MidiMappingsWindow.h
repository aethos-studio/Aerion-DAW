#pragma once

// Audio > MIDI Mappings...: the project's MIDI controller mappings, with a
// Learn toggle and a remove button per mapping.

#include "ViewShared.h"

//==============================================================================
class MidiMappingsWindow : public juce::DocumentWindow
{
public:
    MidiMappingsWindow (AudioEngineManager& ae, std::function<void()> onClosed)
        : DocumentWindow ("MIDI Mappings", Theme::bgPanel, DocumentWindow::closeButton),
          closed (std::move (onClosed))
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (28);
        setColour (DocumentWindow::textColourId, Theme::textMain);
        content = new Content (ae);
        setContentOwned (content, true);
        centreWithSize (460, 380);
        setVisible (true);
        toFront (true);
    }

    /** Call when mappings or the learn state change. */
    void refresh()   { content->repaint(); }

    void closeButtonPressed() override
    {
        if (closed) closed();   // the owner deletes this window
    }

private:
    struct Content : public juce::Component
    {
        explicit Content (AudioEngineManager& ae) : audioEngine (ae) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::bgPanel);

            auto b = getLocalBounds().reduced (12);
            auto header = b.removeFromTop (28);

            const bool learning = audioEngine.isMidiLearnActive();
            learnBtn = header.removeFromRight (90).reduced (0, 3);
            g.setColour (learning ? Theme::accent.withAlpha (0.25f) : Theme::surface);
            g.fillRoundedRectangle (learnBtn.toFloat(), 4.0f);
            g.setColour (learning ? Theme::accent : Theme::border);
            g.drawRoundedRectangle (learnBtn.toFloat(), 4.0f, 1.0f);
            g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
            g.drawText (learning ? "LEARNING" : "LEARN", learnBtn, juce::Justification::centred);

            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (12.0f).withStyle (juce::Font::bold));
            g.drawText ("CONTROLLER MAPPINGS", header, juce::Justification::centredLeft);

            b.removeFromTop (6);
            g.setFont (Theme::uiSize (11.0f));
            g.setColour (Theme::textMuted);
            g.drawFittedText (learning ? "Move a fader or a plugin control, then the hardware control to map to it."
                                       : "Click Learn, touch a control, then move a knob or fader on your MIDI controller.",
                              b.removeFromTop (30), juce::Justification::topLeft, 2);

            removeBtns.clearQuick();
            const auto rows = audioEngine.getMidiMappings();

            if (rows.isEmpty())
            {
                g.drawText ("No mappings in this project.", b, juce::Justification::centred);
                return;
            }

            for (auto& row : rows)
            {
                auto r = b.removeFromTop (30);
                b.removeFromTop (4);
                g.setColour (Theme::surface);
                g.fillRoundedRectangle (r.toFloat(), 4.0f);

                auto remove = r.removeFromRight (30).reduced (6);
                removeBtns.add (remove);
                g.setColour (Theme::textMuted);
                g.drawLine ((float) remove.getX(), (float) remove.getY(), (float) remove.getRight(), (float) remove.getBottom(), 1.5f);
                g.drawLine ((float) remove.getRight(), (float) remove.getY(), (float) remove.getX(), (float) remove.getBottom(), 1.5f);

                r.removeFromLeft (10);
                g.setColour (Theme::accent);
                g.setFont (Theme::uiSize (11.0f).withStyle (juce::Font::bold));
                g.drawText (row.controller, r.removeFromLeft (110), juce::Justification::centredLeft, true);
                g.setColour (Theme::textMain);
                g.setFont (Theme::uiSize (11.0f));
                g.drawText (row.parameter, r, juce::Justification::centredLeft, true);

                if (b.getHeight() < 30)
                    break;
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (learnBtn.contains (e.getPosition()))
            {
                audioEngine.setMidiLearnActive (! audioEngine.isMidiLearnActive());
                repaint();
                return;
            }

            for (int i = 0; i < removeBtns.size(); ++i)
                if (removeBtns[i].expanded (4).contains (e.getPosition()))
                {
                    audioEngine.removeMidiMapping (i);
                    repaint();
                    return;
                }
        }

        AudioEngineManager& audioEngine;
        juce::Rectangle<int> learnBtn;
        juce::Array<juce::Rectangle<int>> removeBtns;
    };

    Content* content = nullptr;
    std::function<void()> closed;
};
