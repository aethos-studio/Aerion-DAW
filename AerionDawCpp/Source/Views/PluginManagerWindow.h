#pragma once

// Window listing the plugins on a track.

#include "ViewShared.h"

//==============================================================================
// Lists plugins on a track and allows opening their editors.
class PluginManagerWindow : public juce::DocumentWindow
{
public:
    PluginManagerWindow (tracktion::Track* t, AudioEngineManager& ae)
        : DocumentWindow (t->getName() + "  -  Plugins", Theme::bgPanel,
                          DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          track (t), audioEngine (ae)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (28);
        setColour (DocumentWindow::textColourId, Theme::textMain);
        setSize (350, 450);
        
        auto* content = new Content (t, ae);
        setContentOwned (content, true);
        
        centreWithSize (350, 450);
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override { delete this; }

private:
    struct Content : public juce::Component,
                     public juce::Timer
    {
        Content (tracktion::Track* t, AudioEngineManager& ae) : track (t), audioEngine (ae) 
        {
            startTimerHz (10); // Refresh list if plugins are added/removed
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::bgPanel);
            
            auto b = getLocalBounds().reduced (10);
            auto header = b.removeFromTop (30);
            
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (12.0f).withStyle (juce::Font::bold));
            g.drawText ("ASSIGNED PLUGINS", header, juce::Justification::centredLeft);
            
            // Add button
            addBtnBounds = header.removeFromRight (60).reduced (0, 4);
            g.setColour (Theme::surface);
            g.fillRoundedRectangle (addBtnBounds.toFloat(), 4.0f);
            g.setColour (Theme::active);
            g.drawRoundedRectangle (addBtnBounds.toFloat(), 4.0f, 1.0f);
            g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
            g.drawText ("+ ADD", addBtnBounds, juce::Justification::centred);

            insertRowHits.clearQuick();
            int y = header.getBottom() + 10;

            for (auto* p : track->pluginList)
            {
                if (auto* plug = dynamic_cast<tracktion::ExternalPlugin*> (p))
                {
                    juce::Rectangle<int> row (10, y, getWidth() - 20, 34);
                    insertRowHits.add (paintInsertRow (g, row, *plug, audioEngine));
                    y += 38;
                }
            }

            if (insertRowHits.isEmpty())
            {
                g.setColour (Theme::textMuted);
                g.drawText ("No third-party plugins added.", getLocalBounds().withTrimmedTop (40), juce::Justification::centred);
            }
            else if (insertDragState.dropPreviewY >= 0)
            {
                paintInsertDropLine (g, insertDragState.dropPreviewY,
                                     insertRowHits.getFirst().row.getX(),
                                     insertRowHits.getFirst().row.getWidth());
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (addBtnBounds.contains (e.getPosition()))
            {
                if (isInsertTrackFrozen (audioEngine, track)) {
                    showFrozenTrackInsertAlert();
                    return;
                }
                auto screen = localAreaToGlobal (addBtnBounds);
                PluginPicker::show (audioEngine, screen, [this] (const juce::PluginDescription& d) {
                    if (auto p = audioEngine.addPluginToTrack (track, d))
                        p->showWindowExplicitly();
                    repaint();
                });
                return;
            }

            if (handleInsertRowMouseDown (e, insertRowHits, audioEngine, track, insertDragState,
                                          [this] { repaint(); }))
                return;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            handleInsertRowMouseDrag (e, insertRowHits, insertDragState, [this] { repaint(); });
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::ignoreUnused (e);
            handleInsertRowMouseUp (audioEngine, track, insertRowHits, insertDragState, [this] { repaint(); });
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            for (auto& hit : insertRowHits)
            {
                if (hit.labelArea.contains (e.getPosition()) && hit.plugin != nullptr)
                {
                    hit.plugin->showWindowExplicitly();
                    return;
                }
            }
        }

        void timerCallback() override { repaint(); }

        tracktion::Track* track;
        AudioEngineManager& audioEngine;
        juce::Rectangle<int> addBtnBounds;
        juce::Array<InsertRowHitAreas> insertRowHits;
        InsertRowDragState insertDragState;
    };

    tracktion::Track* track;
    AudioEngineManager& audioEngine;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManagerWindow)
};
