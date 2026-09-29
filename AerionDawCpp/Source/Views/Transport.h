#pragma once

// Bottom transport bar and the metronome settings popup.

#include "ViewShared.h"

//==============================================================================
// Bottom transport bar  -  buttons + bars/beats counter + tempo + time signature.
class Transport : public juce::Component,
                  public juce::ValueTree::Listener
{
public:
    Transport(AudioEngineManager& ae, ProjectData& pd) : audioEngine(ae), projectData(pd)
    {
        projectData.getProjectTree().addListener (this);

        auto loadIcon = &Icons::load;
        icons[(int)Glyph::play]    = loadIcon (BinaryData::aerion_transport_play_svg,    BinaryData::aerion_transport_play_svgSize);
        icons[(int)Glyph::stop]    = loadIcon (BinaryData::aerion_transport_stop_svg,    BinaryData::aerion_transport_stop_svgSize);
        icons[(int)Glyph::record]  = loadIcon (BinaryData::aerion_transport_record_svg,  BinaryData::aerion_transport_record_svgSize);
        icons[(int)Glyph::rewind]  = loadIcon (BinaryData::aerion_transport_rewind_svg,  BinaryData::aerion_transport_rewind_svgSize);
        icons[(int)Glyph::forward] = loadIcon (BinaryData::aerion_transport_forward_svg, BinaryData::aerion_transport_forward_svgSize);
        icons[(int)Glyph::loop]    = loadIcon (BinaryData::aerion_transport_loop_svg,    BinaryData::aerion_transport_loop_svgSize);

        auto setupLabel = [this] (juce::Label& l)
        {
            l.setFont (Theme::uiSize (13.0f).withStyle (juce::Font::bold));
            l.setJustificationType (juce::Justification::centred);
            l.setColour (juce::Label::textColourId, Theme::accent);
            l.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            l.setEditable (true, false, false);
            l.setColour (juce::Label::textWhenEditingColourId, Theme::textMain);
            l.setColour (juce::Label::backgroundWhenEditingColourId, Theme::surface);
            addAndMakeVisible (l);
        };

        setupLabel (tempoLabel);
        tempoLabel.onTextChange = [this] {
            double bpm = tempoLabel.getText().getDoubleValue();
            if (bpm > 0) audioEngine.setTempo (bpm);
        };

        setupLabel (timeSigLabel);
        timeSigLabel.onTextChange = [this] {
            juce::String s = timeSigLabel.getText();
            int n = s.upToFirstOccurrenceOf ("/", false, false).getIntValue();
            int d = s.fromFirstOccurrenceOf ("/", false, false).getIntValue();
            if (n > 0 && d > 0)
                audioEngine.setTimeSigAtPosition (audioEngine.getTransportPosition(), n, d);
        };
    }

    ~Transport() override { projectData.getProjectTree().removeListener (this); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { repaint(); }

    void resized() override
    {
        const int panelH = 36;
        const int panelY = (getHeight() - panelH) / 2;

        // Tempo display: 96px wide, right-anchored
        tempoBounds = { getWidth() - 182, panelY, 94, panelH };
        tempoLabel.setBounds (tempoBounds.reduced (4, 8));

        // Time sig display: 68px wide, rightmost
        timeSigBounds = { getWidth() - 80, panelY, 72, panelH };
        timeSigLabel.setBounds (timeSigBounds.reduced (4, 8));
    }

    void paint (juce::Graphics& g) override
    {
        AERION_PROFILE_SCOPE ("Transport::paint");

        Theme::fillBackgroundGradient (g, getLocalBounds());
        g.setColour (Theme::border.withAlpha (0.6f));
        g.drawLine (0.0f, 0.0f, (float)getWidth(), 0.0f);

        double pos = audioEngine.getTransportPosition();
        const int W = getWidth();
        const int H = getHeight();
        const int panelH = 36;
        const int panelY = (H - panelH) / 2;

        // -- Left: SR / Buffer / CPU --------------------------------------------
        {
            auto bi = audioEngine.getBufferInfo();
            juce::String srStr  = juce::String (bi.sampleRate / 1000.0, 1) + " kHz";
            juce::String bufStr = juce::String::formatted ("%d@%.1fms", bi.blockSize, bi.oneBlockMs);
            float cpu = juce::jlimit (0.0f, 1.0f, bi.cpuUsage);
            juce::Colour cpuCol = cpu > 0.85f ? Theme::meterRed
                                : cpu > 0.60f ? Theme::meterYellow
                                              : Theme::accent;

            g.setColour (Theme::textMuted.withAlpha (0.45f));
            g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
            g.drawText ("SR",  10, panelY + 2,  26, 12, juce::Justification::right);
            g.drawText ("BUF", 10, panelY + 18, 26, 12, juce::Justification::right);
            g.setColour (Theme::accent.withAlpha (0.9f));
            g.setFont (Theme::uiSize (11.0f).withStyle (juce::Font::bold));
            g.drawText (srStr,  40, panelY,      88, 17, juce::Justification::centredLeft);
            g.drawText (bufStr, 40, panelY + 17, 88, 17, juce::Justification::centredLeft);
            g.setColour (Theme::surface.withAlpha (0.7f));
            g.fillRoundedRectangle (130.0f, (float)(panelY + 5), 50.0f, 5.0f, 2.0f);
            g.setColour (cpuCol.withAlpha (0.85f));
            g.fillRoundedRectangle (130.0f, (float)(panelY + 5), 50.0f * cpu, 5.0f, 2.0f);
            g.setColour (Theme::textMuted.withAlpha (0.35f));
            g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
            g.drawText ("CPU", 130, panelY + 14, 50, 10, juce::Justification::centred);
        }

        drawSectionDivider (g, 190, panelY - 4, panelH + 8);

        // -- Center: Transport buttons ------------------------------------------
        const int btnS = 34, btnGap = 8;
        const int totalBtnW = 6 * btnS + 5 * btnGap;   // 6 buttons
        int cx = W / 2 - totalBtnW / 2;
        int cy = (H - btnS) / 2;

        rewindBounds  = { cx,                        cy, btnS, btnS }; cx += btnS + btnGap;
        forwardBounds = { cx,                        cy, btnS, btnS }; cx += btnS + btnGap;
        stopBounds    = { cx,                        cy, btnS, btnS }; cx += btnS + btnGap;
        playBounds    = { cx,                        cy, btnS, btnS }; cx += btnS + btnGap;
        recBounds     = { cx,                        cy, btnS, btnS }; cx += btnS + btnGap;
        loopBounds    = { cx,                        cy, btnS, btnS };

        drawBtn (g, rewindBounds.toFloat(),  Glyph::rewind);
        drawBtn (g, forwardBounds.toFloat(), Glyph::forward);
        drawBtn (g, stopBounds.toFloat(),    Glyph::stop);
        drawBtn (g, playBounds.toFloat(),    Glyph::play,   audioEngine.isPlaying());
        drawBtn (g, recBounds.toFloat(),     Glyph::record, audioEngine.isRecording());
        drawBtn (g, loopBounds.toFloat(),    Glyph::loop,   audioEngine.getEdit().getTransport().looping);

        drawSectionDivider (g, W - 186, panelY - 4, panelH + 8);

        // -- Tempo display ------------------------------------------------------
        drawDisplayPanel (g, tempoBounds);
        if (! tempoLabel.isBeingEdited())
            tempoLabel.setText (juce::String::formatted ("%.2f", audioEngine.getTempoAtPosition (pos)),
                                juce::dontSendNotification);
        g.setColour (Theme::textMuted.withAlpha (0.45f));
        g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
        g.drawText ("TEMPO",
                    tempoBounds.getX(), tempoBounds.getBottom() - 11, tempoBounds.getWidth(), 11,
                    juce::Justification::centred);

        drawSectionDivider (g, W - 84, panelY - 4, panelH + 8);

        // -- Time Sig display ---------------------------------------------------
        drawDisplayPanel (g, timeSigBounds);
        if (! timeSigLabel.isBeingEdited())
            timeSigLabel.setText (audioEngine.getTimeSigAtPosition (pos), juce::dontSendNotification);
        g.setColour (Theme::textMuted.withAlpha (0.45f));
        g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
        g.drawText ("TIME SIG",
                    timeSigBounds.getX(), timeSigBounds.getBottom() - 11, timeSigBounds.getWidth(), 11,
                    juce::Justification::centred);
    }

    std::unique_ptr<juce::Drawable> icons[6];  // indexed by Glyph enum: play, stop, record, rewind, forward, loop

    enum class Glyph { play, stop, record, rewind, forward, loop };

    void drawBtn (juce::Graphics& g, juce::Rectangle<float> b, Glyph k, bool active = false)
    {
        auto bc = b.reduced (2.0f);
        juce::Colour col = (k == Glyph::record) ? Theme::recordRed : Theme::active;

        g.setColour (active ? col.withAlpha (0.18f) : juce::Colour (0xff0c1018));
        g.fillRoundedRectangle (bc, 4.0f);
        g.setColour (active ? col.withAlpha (0.55f) : Theme::border.withAlpha (0.45f));
        g.drawRoundedRectangle (bc, 4.0f, 1.0f);

        if (auto* icon = icons[(int) k].get())
        {
            Icons::draw (g, *icon, bc.reduced (4.0f), active ? col : Theme::textMuted.withAlpha (0.85f));
            return;
        }

        float cx = bc.getCentreX(), cy = bc.getCentreY();
        g.setColour (active ? col : Theme::textMuted.withAlpha (0.75f));

        switch (k)
        {
            case Glyph::play: {
                juce::Path p;
                p.addTriangle (cx - 5.0f, cy - 7.0f, cx - 5.0f, cy + 7.0f, cx + 7.0f, cy);
                g.fillPath (p);
            } break;
            case Glyph::stop:
                g.fillRoundedRectangle (cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, 1.5f);
                break;
            case Glyph::record:
                g.setColour (active ? Theme::recordRed : Theme::recordRed.withAlpha (0.65f));
                g.fillEllipse (cx - 6.5f, cy - 6.5f, 13.0f, 13.0f);
                if (active) {
                    g.setColour (Theme::recordRed.withAlpha (0.18f));
                    g.fillEllipse (cx - 9.5f, cy - 9.5f, 19.0f, 19.0f);
                }
                break;
            case Glyph::rewind: {
                juce::Path p;
                p.addTriangle (cx - 2.0f, cy, cx + 7.0f, cy - 6.0f, cx + 7.0f, cy + 6.0f);
                p.addTriangle (cx - 9.0f, cy, cx - 2.0f, cy - 6.0f, cx - 2.0f, cy + 6.0f);
                g.fillPath (p);
            } break;
            case Glyph::forward: {
                juce::Path p;
                p.addTriangle (cx + 2.0f, cy, cx - 7.0f, cy - 6.0f, cx - 7.0f, cy + 6.0f);
                p.addTriangle (cx + 9.0f, cy, cx + 2.0f, cy - 6.0f, cx + 2.0f, cy + 6.0f);
                g.fillPath (p);
            } break;
            case Glyph::loop: {
                g.drawEllipse (cx - 7.5f, cy - 5.5f, 15.0f, 11.0f, 1.5f);
                juce::Path arrow;
                arrow.addTriangle (cx + 5.5f, cy - 7.5f, cx + 9.5f, cy - 1.5f, cx + 1.5f, cy - 1.5f);
                g.fillPath (arrow);
            } break;
        }
    }

    static void drawDisplayPanel (juce::Graphics& g, juce::Rectangle<int> b)
    {
        g.setColour (juce::Colour (0xff050709));
        g.fillRoundedRectangle (b.toFloat(), 3.0f);
        g.setColour (Theme::border.withAlpha (0.7f));
        g.drawRoundedRectangle (b.toFloat(), 3.0f, 1.0f);
    }

    static void drawSectionDivider (juce::Graphics& g, int x, int y, int h)
    {
        g.setColour (Theme::border.withAlpha (0.4f));
        g.drawLine ((float)x, (float)(y + 4), (float)x, (float)(y + h - 4), 1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (playBounds.contains(e.getPosition())) audioEngine.play();
        if (stopBounds.contains(e.getPosition())) audioEngine.stop();
        if (recBounds.contains(e.getPosition()))  audioEngine.record();
        
        if (rewindBounds.contains(e.getPosition())) 
        {
            if (audioEngine.getTransportPosition() > 0.1)
                audioEngine.setTransportPosition(0.0);
            else
                audioEngine.setTransportPosition(juce::jmax(0.0, audioEngine.getTransportPosition() - 4.0));
        }
        
        if (forwardBounds.contains(e.getPosition()))
            audioEngine.setTransportPosition(audioEngine.getTransportPosition() + 4.0);
            
        if (loopBounds.contains(e.getPosition())) {
            auto& t = audioEngine.getEdit().getTransport();
            t.looping = ! t.looping;
        }

        repaint();
    }

private:
    AudioEngineManager& audioEngine;
    ProjectData& projectData;
    juce::Rectangle<int> playBounds, stopBounds, recBounds, rewindBounds, forwardBounds, loopBounds, tempoBounds, timeSigBounds;
    juce::Label tempoLabel, timeSigLabel;
};

//==============================================================================
// Small popup for metronome volume settings.
class MetronomeSettingsPopup : public juce::Component
{
public:
    MetronomeSettingsPopup (AudioEngineManager& ae) : audioEngine (ae)
    {
        addAndMakeVisible (slider);
        slider.setRange (AudioEngineManager::kMinVolumeDb, AudioEngineManager::kMaxVolumeDb, 0.1);
        slider.setValue (audioEngine.getMetronomeVolumeDb());
        slider.setTextValueSuffix (" dB");
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 20);
        slider.onValueChange = [this] {
            audioEngine.setMetronomeVolumeDb ((float) slider.getValue());
        };

        addAndMakeVisible (label);
        label.setText ("CLICK LEVEL", juce::dontSendNotification);
        label.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
        label.setColour (juce::Label::textColourId, Theme::textMuted);

        addAndMakeVisible (accentToggle);
        accentToggle.setButtonText ("Accent downbeat");
        accentToggle.setToggleState (audioEngine.isMetronomeAccentEnabled(), juce::dontSendNotification);
        accentToggle.setColour (juce::ToggleButton::textColourId, Theme::textMuted);
        accentToggle.onStateChange = [this] {
            audioEngine.setMetronomeAccentEnabled (accentToggle.getToggleState());
        };

        setSize (210, 98);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bgPanel);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10);
        label.setBounds (b.removeFromTop (20));
        b.removeFromTop (4);
        slider.setBounds (b.removeFromTop (28));
        b.removeFromTop (6);
        accentToggle.setBounds (b.removeFromTop (22));
    }

private:
    AudioEngineManager& audioEngine;
    juce::Slider       slider;
    juce::Label        label;
    juce::ToggleButton accentToggle;
};
