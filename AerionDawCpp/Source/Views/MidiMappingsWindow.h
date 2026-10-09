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
        centreWithSize (460, 420);
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

            // Defaults: apply to new projects and new tracks.
            auto footer = b.removeFromBottom (54);
            {
                auto buttons = footer.removeFromTop (26);
                auto drawButton = [&g] (juce::Rectangle<int> r, const juce::String& text, bool enabled)
                {
                    g.setColour (Theme::surface);
                    g.fillRoundedRectangle (r.toFloat(), 4.0f);
                    g.setColour (enabled ? Theme::active : Theme::border);
                    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
                    g.setColour (enabled ? Theme::textMain : Theme::textMuted);
                    g.setFont (Theme::uiSize (9.5f).withStyle (juce::Font::bold));
                    g.drawText (text, r, juce::Justification::centred, false);
                };

                const int numDefaults = audioEngine.getNumDefaultMidiMappings();
                saveDefaultBtn  = buttons.removeFromLeft (130);
                buttons.removeFromLeft (8);
                clearDefaultBtn = buttons.removeFromLeft (110);
                drawButton (saveDefaultBtn, "SAVE AS DEFAULT", true);
                drawButton (clearDefaultBtn, "CLEAR DEFAULTS", numDefaults > 0);

                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (9.5f));
                g.drawFittedText (footerMessage.isNotEmpty()
                                      ? footerMessage
                                      : juce::String (numDefaults) + (numDefaults == 1 ? " default mapping" : " default mappings")
                                          + " for master and track faders and pan, added to new projects and new tracks.",
                                  footer.withTrimmedTop (4), juce::Justification::topLeft, 2);
            }
            b.removeFromBottom (8);

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
            if (saveDefaultBtn.contains (e.getPosition()))
            {
                const auto r = audioEngine.saveMidiMappingsAsDefault();
                footerMessage = "Saved " + juce::String (r.saved) + (r.saved == 1 ? " mapping" : " mappings") + " as default."
                              + (r.skipped > 0 ? " " + juce::String (r.skipped) + " plugin or bus "
                                                     + (r.skipped == 1 ? "mapping stays" : "mappings stay") + " in this project only."
                                               : juce::String());
                repaint();
                return;
            }

            if (clearDefaultBtn.contains (e.getPosition()) && audioEngine.getNumDefaultMidiMappings() > 0)
            {
                audioEngine.clearDefaultMidiMappings();
                footerMessage = "Default mappings cleared. This project keeps its own.";
                repaint();
                return;
            }

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
        juce::Rectangle<int> learnBtn, saveDefaultBtn, clearDefaultBtn;
        juce::String footerMessage;
        juce::Array<juce::Rectangle<int>> removeBtns;
    };

    Content* content = nullptr;
    std::function<void()> closed;
};
