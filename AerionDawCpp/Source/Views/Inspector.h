#pragma once

// Left-hand Inspector panel.

#include "ViewShared.h"
#include "DAWPanel.h"

//==============================================================================
class Inspector : public DAWPanel,
                  public juce::ValueTree::Listener,
                  public AudioEngineManager::Listener
{
public:
    Inspector (AudioEngineManager& ae, ProjectData& pd)
        : DAWPanel ("Inspector"), audioEngine (ae), projectData (pd)
    {
        projectData.getProjectTree().addListener (this);
        audioEngine.addListener (this);

        if (auto svgXml = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::aerion_fader_svg, BinaryData::aerion_fader_svgSize)))
            faderKnobDrawable = juce::Drawable::createFromSVG (*svgXml);
    }

    ~Inspector() override
    {
        projectData.getProjectTree().removeListener (this);
        audioEngine.removeListener (this);
    }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { repaint(); }

    void editStateChanged() override { repaint(); }

    /** Called by MainComponent's display clock while audio is moving: repaints
        the meters, fader cap and peak readout of the selected track only. */
    void repaintMeters()
    {
        if (selectedTrack != nullptr && isShowing())
            for (auto& r : faderLiveAreas (faderArea))
                repaint (r);
    }

    juce::String trackName  { "(no selection)" };
    int          trackIndex { -1 };
    bool         armed      { false };
    bool         muted      { false };
    bool         solo       { false };
    tracktion::Track* selectedTrack = nullptr;

    std::unique_ptr<juce::Drawable> faderKnobDrawable;

    void paint (juce::Graphics& g) override
    {
        DAWPanel::paint (g);
        auto b = getLocalBounds().withTrimmedTop (28).reduced (12);

        // Fader & Meter area on the right.
        if (selectedTrack != nullptr)
        {
            auto faderMeterArea = b.removeFromRight (45);
            faderArea = faderMeterArea.withTrimmedBottom (12);
            ::paintFader (g, faderArea, audioEngine, selectedTrack, Theme::colourForTrack (trackIndex), false, faderKnobDrawable.get());
            b.removeFromRight (10); // Gap
        }
        else
        {
            faderArea = juce::Rectangle<int>();
        }

        insertRowHits.clearQuick();
        sendRows.clearQuick();

        // Track header
        auto headerB = b.removeFromTop (70);
        g.setColour (trackIndex >= 0 ? Theme::colourForTrack (trackIndex) : Theme::textMuted);
        g.fillEllipse ((float) headerB.getX(), (float) headerB.getY() + 4.0f, 12.0f, 12.0f);
        g.setColour (Theme::textMain);
        g.setFont (Theme::uiSize (15.0f).withStyle (juce::Font::bold));
        g.drawText (trackName, headerB.withTrimmedLeft (20).removeFromTop (22), juce::Justification::topLeft);

        g.setColour (Theme::textMuted);
        g.setFont (Theme::uiSize (11.0f));
        g.drawText ("In",  headerB.getX(), headerB.getY() + 32, 40, 18, juce::Justification::left);
        g.drawText ("Out", headerB.getX(), headerB.getY() + 50, 40, 18, juce::Justification::left);

        // Input routing  -  clickable panel showing current device name. If a
        // specific MIDI controller has been pinned to the track, append a
        // " | MIDI: <name>" tag so users see both routings at a glance.
        {
            juce::String inputName = "Input L+R";
            if (selectedTrack != nullptr)
            {
                int devIdx = audioEngine.getTrackInputDeviceIdx (selectedTrack);
                auto names = audioEngine.getInputDeviceNames();
                if (devIdx >= 0 && devIdx < names.size()) inputName = names[devIdx];
                else if (!names.isEmpty())                 inputName = names[0];

                int midiIdx = audioEngine.getTrackMidiInputDeviceIdx (selectedTrack);
                if (midiIdx >= 0)
                {
                    auto midis = audioEngine.getMidiInputDeviceNames();
                    if (midiIdx < midis.size())
                        inputName += " | MIDI: " + midis[midiIdx];
                }
            }
            inputRoutingBounds = juce::Rectangle<int> (headerB.getRight() - 100, headerB.getY() + 28, 100, 20);
            Theme::drawRoundedPanel (g, inputRoutingBounds.toFloat(), Theme::surface);
            g.setColour (Theme::textMain);
            g.setFont (Theme::uiSize (10.5f));
            g.drawText (inputName, inputRoutingBounds.reduced (4, 0), juce::Justification::centredRight);
        }
        g.setColour (Theme::textMain);
        g.setFont (Theme::uiSize (11.0f));
        g.drawText ("Main", headerB.getRight() - 100, headerB.getY() + 50, 100, 18, juce::Justification::right);

        // State buttons  -  compact single-letter controls
        b.removeFromTop (8);
        auto pills = b.removeFromTop (24);
        // Same order and colours as the Timeline track header: M, S, R.
        muteBounds = pills.removeFromLeft (24); paintLetterButton (g, muteBounds, "M", muted, Theme::meterYellow);
        pills.removeFromLeft (4);
        soloBounds = pills.removeFromLeft (24); paintLetterButton (g, soloBounds, "S", solo,  Theme::accent);
        pills.removeFromLeft (4);
        armBounds  = pills.removeFromLeft (24); paintLetterButton (g, armBounds,  "R", armed, Theme::recordRed);

        // Phase and Mono  -  keep text pills (no icons for these)
        if (selectedTrack != nullptr)
        {
            pills.removeFromLeft (8);
            phaseBounds = pills.removeFromLeft (24);
            bool phaseOn = audioEngine.getTrackPhase (selectedTrack);
            drawPill (g, phaseBounds, "*", phaseOn, Theme::active);

            pills.removeFromLeft (4);
            monoBounds = pills.removeFromLeft (24);
            bool monoOn = audioEngine.getTrackMono (selectedTrack);
            drawPill (g, monoBounds, "M", monoOn, Theme::active);

            // Input-monitoring override: cycles Auto -> On -> Off. The label
            // mirrors the current state so the user sees what's active without
            // hovering for a tooltip.
            pills.removeFromLeft (8);
            monBounds = pills.removeFromLeft (32);
            const auto monMode = audioEngine.getTrackMonitorMode (selectedTrack);
            const bool monActive = monMode != AudioEngineManager::MonitorMode::Off;
            const juce::Colour monColour = (monMode == AudioEngineManager::MonitorMode::On)
                ? Theme::recordRed
                : Theme::active;
            const char* monLabel = (monMode == AudioEngineManager::MonitorMode::On)  ? "ON"
                                 : (monMode == AudioEngineManager::MonitorMode::Off) ? "OFF"
                                                                                     : "AUT";
            drawPill (g, monBounds, monLabel, monActive, monColour);

            // Freeze button
            pills.removeFromLeft (4);
            freezeBounds = pills.removeFromLeft (42);
            if (auto* at = dynamic_cast<tracktion::AudioTrack*>(selectedTrack))
            {
                bool isFrozen = audioEngine.isTrackFrozen(at);
                bool isFreezing = audioEngine.isTrackFreezing(at);
                drawPill (g, freezeBounds,
                          isFreezing ? "..." : (isFrozen ? "UNFRZ" : "FREEZE"),
                          isFrozen || isFreezing,
                          isFreezing ? Theme::meterYellow : juce::Colours::skyblue);
            }
        }

        b.removeFromTop (16);

        // Quick Filters (Phase 1)
        if (selectedTrack != nullptr)
        {
            auto filterSection = b.removeFromTop (filtersExpanded ? 80 : 20);
            juce::Rectangle<int> dummy;
            drawSectionHeader (g, filterSection.removeFromTop (18), "QUICK FILTERS", dummy);
            filterAddBtn = dummy; // reuse dummy but it won't be used
            
            if (filtersExpanded)
            {
                filterSection.removeFromTop (4);
                auto hpfRow = filterSection.removeFromTop (24);
                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (10.0f));
                g.drawText ("HPF", hpfRow.removeFromLeft (30), juce::Justification::centredLeft);
                hpfBounds = hpfRow.reduced (0, 4);
                drawFilterSlider (g, hpfBounds, audioEngine.getTrackHPF (selectedTrack), 20.0f, 20000.0f);
                
                filterSection.removeFromTop (4);
                auto lpfRow = filterSection.removeFromTop (24);
                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (10.0f));
                g.drawText ("LPF", lpfRow.removeFromLeft (30), juce::Justification::centredLeft);
                lpfBounds = lpfRow.reduced (0, 4);
                drawFilterSlider (g, lpfBounds, audioEngine.getTrackLPF (selectedTrack), 20.0f, 20000.0f);
            }
            b.removeFromTop (10);
        }

        // Collect real plugins from the selected track.
        juce::Array<tracktion::ExternalPlugin*> externals;
        juce::Array<tracktion::AuxSendPlugin*>  sends;
        if (selectedTrack != nullptr)
        {
            for (auto* p : selectedTrack->pluginList)
            {
                if (auto* e = dynamic_cast<tracktion::ExternalPlugin*> (p))      externals.add (e);
                else if (auto* a = dynamic_cast<tracktion::AuxSendPlugin*> (p))  sends.add (a);
            }
        }

        // Inserts
        const int insertsH = juce::jmax (60, 40 + externals.size() * 38);
        drawInsertSection (g, b.removeFromTop (insertsH), externals);
        b.removeFromTop (10);

        // Sends
        const int sendsH = juce::jmax (60, 40 + sends.size() * 36);
        drawSendSection (g, b.removeFromTop (sendsH), sends, 0.4f);
        b.removeFromTop (20);

        // AI DSP Workflow at bottom (still scaffolded  -  buttons are visual only).
        g.setColour (Theme::recordRed.withAlpha (0.3f));
        g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
        g.drawText ("AI DSP WORKFLOW", b.getX(), b.getY(), b.getWidth(), 20, juce::Justification::left);
        b.removeFromTop (22);

        Theme::drawRoundedPanel (g, b.removeFromTop (36).toFloat(), Theme::surface.withAlpha (0.3f));
        g.setColour (Theme::textMain.withAlpha (0.3f));
        g.setFont (Theme::uiSize (11.0f));
        g.drawText ("Separate Stems", b.getX(), b.getY() - 36, b.getWidth(), 36, juce::Justification::centred);
        b.removeFromTop (8);
        Theme::drawRoundedPanel (g, b.removeFromTop (36).toFloat(), Theme::surface.withAlpha (0.3f));
        g.setColour (Theme::textMain.withAlpha (0.3f));
        g.drawText ("Audio to MIDI", b.getX(), b.getY() - 36, b.getWidth(), 36, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // ARM / MUTE / SOLO pills
        if (armBounds.contains (e.getPosition()) && selectedTrack != nullptr)
        {
            armed = !armed;
            audioEngine.setTrackArmed (selectedTrack, armed);
            repaint(); return;
        }
        if (muteBounds.contains (e.getPosition()) && selectedTrack != nullptr)
        {
            audioEngine.toggleTrackMute (selectedTrack);
            muted = selectedTrack->isMuted (false); repaint(); return;
        }
        if (soloBounds.contains (e.getPosition()) && selectedTrack != nullptr)
        {
            audioEngine.toggleTrackSolo (selectedTrack);
            solo = selectedTrack->isSolo (false); repaint(); return;
        }

        // Input routing dropdown - split into "Audio Input" and "MIDI Controller"
        // submenus so audio + MIDI tracks share one entry point on the Inspector.
        if (inputRoutingBounds.contains (e.getPosition()) && selectedTrack != nullptr)
        {
            constexpr int kAudioAll  = 1000;
            constexpr int kMidiAll   = 2000;
            constexpr int kAudioBase = 1;
            constexpr int kMidiBase  = 2001;

            const auto audioNames = audioEngine.getInputDeviceNames();
            const auto midiNames  = audioEngine.getMidiInputDeviceNames();
            const int curAudio    = audioEngine.getTrackInputDeviceIdx (selectedTrack);
            const int curMidi     = audioEngine.getTrackMidiInputDeviceIdx (selectedTrack);

            juce::PopupMenu audioMenu;
            audioMenu.addItem (kAudioAll, "All audio inputs", true, curAudio < 0);
            audioMenu.addSeparator();
            for (int i = 0; i < audioNames.size(); ++i)
                audioMenu.addItem (kAudioBase + i, audioNames[i], true, i == curAudio);

            juce::PopupMenu midiMenu;
            midiMenu.addItem (kMidiAll, "All MIDI controllers", true, curMidi < 0);
            midiMenu.addSeparator();
            if (midiNames.isEmpty())
                midiMenu.addItem (-1, "(no MIDI inputs - enable in Audio Settings)", false, false);
            else
                for (int i = 0; i < midiNames.size(); ++i)
                    midiMenu.addItem (kMidiBase + i, midiNames[i], true, i == curMidi);

            juce::PopupMenu m;
            m.addSubMenu ("Audio Input",     audioMenu);
            m.addSubMenu ("MIDI Controller", midiMenu);

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                [this](int r) {
                    if (selectedTrack == nullptr) { repaint(); return; }
                    if (r == kAudioAll)
                        audioEngine.setTrackInputDevice (selectedTrack, -1);
                    else if (r >= kAudioBase && r < kAudioAll)
                        audioEngine.setTrackInputDevice (selectedTrack, r - kAudioBase);
                    else if (r == kMidiAll)
                        audioEngine.setTrackMidiInputDevice (selectedTrack, -1);
                    else if (r >= kMidiBase && r < kMidiBase + 200)
                        audioEngine.setTrackMidiInputDevice (selectedTrack, r - kMidiBase);
                    repaint();
                });
            return;
        }

        // Fader interaction
        if (faderArea.contains (e.getPosition()))
        {
            if (auto* audio = dynamic_cast<tracktion::AudioTrack*> (selectedTrack))
            {
                ::setFaderFromY (audioEngine, audio, faderArea, e.y);
                return;
            }
        }

        // Phase and Mono (Phase 1)
        if (selectedTrack != nullptr)
        {
            if (phaseBounds.contains (e.getPosition()))
            {
                audioEngine.setTrackPhase (selectedTrack, ! audioEngine.getTrackPhase (selectedTrack));
                repaint();
                return;
            }
            if (monoBounds.contains (e.getPosition()))
            {
                audioEngine.setTrackMono (selectedTrack, ! audioEngine.getTrackMono (selectedTrack));
                repaint();
                return;
            }
            if (monBounds.contains (e.getPosition()))
            {
                using MM = AudioEngineManager::MonitorMode;
                const auto cur = audioEngine.getTrackMonitorMode (selectedTrack);
                const auto next = (cur == MM::Auto) ? MM::On
                                : (cur == MM::On)   ? MM::Off
                                                    : MM::Auto;
                audioEngine.setTrackMonitorMode (selectedTrack, next);
                repaint();
                return;
            }

            // Freeze button
            if (freezeBounds.contains (e.getPosition()))
            {
                if (auto* at = dynamic_cast<tracktion::AudioTrack*>(selectedTrack))
                {
                    if (audioEngine.isTrackFreezing(at))
                        return;

                    if (audioEngine.isTrackFrozen(at))
                        audioEngine.unfreezeTrack(at);
                    else
                        audioEngine.freezeTrack(at);
                    repaint();
                }
                return;
            }
        }

        // Quick Filters Section Header (collapse/expand)
        if (selectedTrack != nullptr)
        {
            auto headerRect = juce::Rectangle<int> (12, filterAddBtn.getY(), getWidth() - 24, 18);
            if (headerRect.contains (e.getPosition()))
            {
                filtersExpanded = ! filtersExpanded;
                repaint();
                return;
            }
        }

        // "+" buttons add a plugin / send.
        if (insertAddBtn.contains (e.getPosition()))
        {
            if (selectedTrack == nullptr) return;
            if (isInsertTrackFrozen (audioEngine, selectedTrack))
            {
                showFrozenTrackInsertAlert();
                return;
            }
            auto track = selectedTrack;
            auto sp = e.getScreenPosition();
            juce::Rectangle<int> anchor (sp.x, sp.y, 1, 1);
            PluginPicker::show (audioEngine, anchor,
                [this, track] (const juce::PluginDescription& d)
                {
                    if (auto p = audioEngine.addPluginToTrack (track, d))
                        p->showWindowExplicitly();
                    repaint();
                });
            return;
        }

        // Click an insert row -> open its editor; right-click -> context menu.
        if (handleInsertRowMouseDown (e, insertRowHits, audioEngine, selectedTrack, insertDragState,
                                      [this] { repaint(); }))
            return;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (faderArea.contains (e.getPosition()))
        {
            audioEngine.setTrackVolumeDb (selectedTrack, 0.0f);
            repaint (faderArea);
            return;
        }
        
        if (selectedTrack != nullptr)
        {
            if (hpfBounds.contains (e.getPosition())) { audioEngine.setTrackHPF (selectedTrack, 20.0f); repaint(); return; }
            if (lpfBounds.contains (e.getPosition())) { audioEngine.setTrackLPF (selectedTrack, 20000.0f); repaint(); return; }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (handleInsertRowMouseDrag (e, insertRowHits, insertDragState, [this] { repaint(); }))
            return;

        if (faderArea.contains (e.getPosition()) || (e.getMouseDownX() >= faderArea.getX() && e.getMouseDownX() < faderArea.getRight()))
        {
            ::setFaderFromY (audioEngine, selectedTrack, faderArea, e.y);
            return;
        }
        
        if (selectedTrack != nullptr)
        {
            if (hpfBounds.contains (e.getMouseDownPosition()))
            {
                float freq = getValueFromX (hpfBounds, e.x, 20.0f, 20000.0f, true);
                audioEngine.setTrackHPF (selectedTrack, freq);
                repaint();
                return;
            }
            if (lpfBounds.contains (e.getMouseDownPosition()))
            {
                float freq = getValueFromX (lpfBounds, e.x, 20.0f, 20000.0f, true);
                audioEngine.setTrackLPF (selectedTrack, freq);
                repaint();
                return;
            }
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (e);
        handleInsertRowMouseUp (audioEngine, selectedTrack, insertRowHits, insertDragState, [this] { repaint(); });
    }

private:
    AudioEngineManager& audioEngine;
    ProjectData& projectData;
    juce::Array<InsertRowHitAreas> insertRowHits;
    InsertRowDragState insertDragState;
    juce::Array<juce::Rectangle<int>> sendRows;
    juce::Array<tracktion::AuxSendPlugin*> sendPlugins;
    juce::Array<juce::Rectangle<int>> sendLevelBounds;
    juce::Rectangle<int> insertAddBtn;
    juce::Rectangle<int> faderArea;
    
    // Phase 1 Members
    juce::Rectangle<int> hpfBounds, lpfBounds, phaseBounds, monoBounds, filterAddBtn;
    juce::Rectangle<int> armBounds, muteBounds, soloBounds, inputRoutingBounds;
    juce::Rectangle<int> monBounds, freezeBounds;
    bool filtersExpanded = true;

    void drawFilterSlider (juce::Graphics& g, juce::Rectangle<int> r, float value, float min, float max)
    {
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (r.toFloat(), 2.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.toFloat(), 2.0f, 1.0f);
        
        // Logarithmic scale for frequency
        float norm = (std::log10 (value) - std::log10 (min)) / (std::log10 (max) - std::log10 (min));
        int fillW = (int) (norm * (float) r.getWidth());
        
        g.setColour (Theme::active.withAlpha (0.4f));
        g.fillRoundedRectangle (r.getX(), r.getY(), fillW, r.getHeight(), 2.0f);
        
        g.setColour (Theme::textMain);
        g.setFont (Theme::uiSize (10.0f));
        juce::String txt = (value >= 1000.0f) ? juce::String (value / 1000.0f, 1) + "k" : juce::String ((int) value);
        g.drawText (txt + " Hz", r.reduced (4, 0), juce::Justification::centredRight);
    }

    float getValueFromX (juce::Rectangle<int> r, int x, float min, float max, bool log)
    {
        float norm = juce::jlimit (0.0f, 1.0f, (float) (x - r.getX()) / (float) r.getWidth());
        if (log)
            return std::pow (10.0f, std::log10 (min) + norm * (std::log10 (max) - std::log10 (min)));
        return min + norm * (max - min);
    }

    void drawSectionHeader (juce::Graphics& g, juce::Rectangle<int> headerArea,
                            const juce::String& title, juce::Rectangle<int>& addBtnOut,
                            float alpha = 1.0f)
    {
        g.setColour (Theme::textMuted.withMultipliedAlpha (alpha));
        g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
        g.drawText (title, headerArea.getX(), headerArea.getY(), headerArea.getWidth() - 20, 18, juce::Justification::left);

        addBtnOut = juce::Rectangle<int> (headerArea.getRight() - 18, headerArea.getY(), 16, 18);
        g.setColour (Theme::active.withMultipliedAlpha (0.18f * alpha));
        g.fillRoundedRectangle (addBtnOut.toFloat(), 3.0f);
        g.setColour (Theme::active.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (addBtnOut.toFloat(), 3.0f, 1.0f);
        g.setFont (Theme::uiSize (12.0f).withStyle (juce::Font::bold));
        g.setColour (Theme::textMain.withMultipliedAlpha (alpha));
        g.drawText ("+", addBtnOut, juce::Justification::centred);
    }

    void drawInsertSection (juce::Graphics& g, juce::Rectangle<int> b,
                            const juce::Array<tracktion::ExternalPlugin*>& plugins)
    {
        drawSectionHeader (g, b.withHeight (18), "INSERTS", insertAddBtn);

        int y = b.getY() + 22;
        if (plugins.isEmpty())
        {
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (10.5f));
            g.drawText (selectedTrack == nullptr ? "Select a track."
                                                 : "No plugins. Click + to add.",
                        b.getX(), y, b.getWidth(), 24, juce::Justification::centredLeft);
            return;
        }

        for (auto* plug : plugins)
        {
            juce::Rectangle<int> row ((int) b.getX(), y, b.getWidth(), 34);
            insertRowHits.add (paintInsertRow (g, row, *plug, audioEngine));
            y += 38;
        }

        if (insertDragState.dropPreviewY >= 0 && ! insertRowHits.isEmpty())
            paintInsertDropLine (g, insertDragState.dropPreviewY,
                                 insertRowHits.getFirst().row.getX(),
                                 insertRowHits.getFirst().row.getWidth());
    }

    void drawSendSection (juce::Graphics& g, juce::Rectangle<int> b,
                          const juce::Array<tracktion::AuxSendPlugin*>& sends,
                          float alpha = 1.0f)
    {
        juce::Rectangle<int> dummy;
        drawSectionHeader (g, b.withHeight (18), "SENDS", dummy, alpha);

        int y = b.getY() + 22;
        if (sends.isEmpty())
        {
            g.setColour (Theme::textMuted.withMultipliedAlpha (alpha));
            g.setFont (Theme::uiSize (10.5f));
            g.drawText ("No sends.",
                        b.getX(), y, b.getWidth(), 24, juce::Justification::centredLeft);
            return;
        }

        for (auto* s : sends)
        {
            juce::Rectangle<int> row ((int) b.getX(), y, b.getWidth(), 30);
            Theme::drawRoundedPanel (g, row.toFloat(), Theme::bgBase, alpha);
            g.setColour (Theme::active.withMultipliedAlpha (alpha));
            g.fillEllipse ((float) row.getX() + 8.0f, (float) y + 11.0f, 6.0f, 6.0f);
            g.setColour (Theme::textMain.withMultipliedAlpha (alpha));
            g.setFont (Theme::uiSize (11.0f));
            g.drawText (s->getBusName(), row.getX() + 20, y, row.getWidth() - 70, 30, juce::Justification::centredLeft);

            float gainDb = s->getGainDb();
            auto gainR = row.removeFromRight (60).reduced (0, 4);
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (gainR.toFloat(), 2.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (gainR.toFloat(), 2.0f, 1.0f);
            
            float norm = juce::jlimit (0.0f, 1.0f, (gainDb - AudioEngineManager::kMinVolumeDb) / AudioEngineManager::kFaderRangeDb);
            g.setColour (Theme::active.withAlpha (0.4f));
            g.fillRoundedRectangle (gainR.getX(), gainR.getY(), (int)(norm * gainR.getWidth()), gainR.getHeight(), 2.0f);

            g.setColour (Theme::textMain.withMultipliedAlpha (alpha));
            g.setFont (Theme::uiSize (9.0f));
            g.drawText (juce::String::formatted ("%.1f", gainDb), gainR, juce::Justification::centred);

            sendRows.add (row);
            sendPlugins.add (s);
            sendLevelBounds.add (gainR);
            y += 36;
        }
    }
};
