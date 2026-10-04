#pragma once

// Mixer console.

#include "ViewShared.h"

//==============================================================================
// Reaper-style mixer: each strip has a clickable inserts list, a functional pan
// knob, M/S buttons, a fader with dB readout and meter. Detachable to a floating
// window via the header pop-out button.
class Mixer : public juce::Component,
              public juce::DragAndDropTarget,
              public juce::ValueTree::Listener
{
    // Per-strip hit areas recorded while painting, reused by mouse handling
    // and by the playback repaint. Declared first because paintStrip() takes one.
    struct StripHit
    {
        tracktion::Track* track = nullptr;
        bool isMaster = false;
        juce::Rectangle<int> stripBounds;
        juce::Rectangle<int> muteBtn, soloBtn, panArea, faderArea, peakReadoutArea;
        juce::Rectangle<int> monoBtn, fxBtn, infoBtn;
        juce::Array<InsertRowHitAreas> insertRowHits;

        // Fader and pan values currently on screen, so the playback tick can
        // tell when automation has moved them and the whole strip needs a repaint.
        float paintedVolumeDb = 0.0f;
        float paintedPan      = 0.0f;
    };

public:
    static constexpr int kStripW        = 118;
    static constexpr int kInsertColW    = 24;
    static constexpr int kFolderSubmixExtraW = 8;
    static constexpr int kHeaderH       = 28;
    static constexpr int kColorBandH    = 4;
    static constexpr int kNameH         = 20;
    static constexpr int kPanAreaH      = 36;   // rotary knob row
    static constexpr int kPanKnobSize   = 28;   // knob diameter
    static constexpr int kSideBtnColW   = 34;   // right button column width
    static constexpr int kSideBtnH      = 20;   // height per side button
    static constexpr int kSideBtnGap    = 4;    // gap between buttons
    static constexpr int kBottomH       = 16;   // peak-hold label
    static constexpr int kStripGap      = 6;
    static constexpr int kMasterGap     = 18;
    // How far a strip's drawing reaches past its bounds (submix outline).
    static constexpr int kStripPaintMargin = 2;

    std::function<void()> onDetachRequested;
    std::function<void(tracktion::Track*, const juce::PluginDescription&)> onPluginDroppedOnStrip;
    bool detached = false;

    std::unique_ptr<juce::Drawable> faderKnobDrawable;

    Mixer(AudioEngineManager& ae, ProjectData& pd) : audioEngine(ae), projectData(pd)
    {
        projectData.getProjectTree().addListener (this);
        // Meter animation while playing: MainComponent timer calls repaintStripMetersArea().

        if (auto svgXml = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::aerion_fader_svg, BinaryData::aerion_fader_svgSize)))
            faderKnobDrawable = juce::Drawable::createFromSVG (*svgXml);
    }

    ~Mixer() override { projectData.getProjectTree().removeListener (this); }

    /** What playback changes in the console: each strip's meters, fader cap and
        peak readout, or the whole strip if automation has moved its fader or
        pan since it was last drawn. Falls back to the whole strip body when
        there is no up-to-date layout to go on. */
    juce::RectangleList<int> getPlaybackRepaintRegion()
    {
        const auto body = getLocalBounds().withTrimmedTop (kHeaderH + 8);

        if (stripHits.isEmpty())
            return body;

        juce::RectangleList<int> region;
        auto tracks  = audioEngine.getMixerTracks();
        auto* master = audioEngine.getMasterTrack();

        for (auto& hit : stripHits)
        {
            // A track deleted since the last paint: that repaint is already
            // queued, so don't touch the stale pointer.
            if (hit.track != master && ! tracks.contains (hit.track))
                return body;

            const bool automationMoved =
                   ! juce::exactlyEqual (audioEngine.getTrackVolumeDb (hit.track), hit.paintedVolumeDb)
                || ! juce::exactlyEqual (audioEngine.getTrackPan (hit.track), hit.paintedPan);

            if (automationMoved)
                region.add (hit.stripBounds.expanded (kStripPaintMargin));
            else
                region.add (faderLiveAreas (hit.faderArea));
        }

        return region;
    }

    /** Called by MainComponent's timer while playing or recording. */
    void repaintStripMetersArea()
    {
        for (auto& r : getPlaybackRepaintRegion())
            repaint (r);
    }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { repaint(); }

    void paint(juce::Graphics& g) override
    {
        AERION_PROFILE_SCOPE ("Mixer::paint");

        Theme::fillBackgroundGradient (g, getLocalBounds());

        // Header strip. The detach button's bounds are needed for hit-testing
        // even when the header is outside the repainted area.
        const auto header = getLocalBounds().removeFromTop (kHeaderH);
        detachBtn = header.withLeft (header.getRight() - 72).reduced (4, 4);

        if (g.clipRegionIntersects (header.withHeight (kHeaderH + 1)))
        {
            Theme::fillVerticalGradient (g, header, Theme::bgPanel.brighter (0.08f), Theme::bgPanel.darker (0.06f));
            g.setColour(Theme::border);
            g.drawLine(0.0f, (float)kHeaderH, (float)getWidth(), (float)kHeaderH);
            g.setColour(Theme::active);
            g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
            g.drawText("CONSOLE", 16, 0, 100, kHeaderH, juce::Justification::centredLeft);

            // Detach / dock button.
            g.setColour(Theme::surface);
            g.fillRoundedRectangle(detachBtn.toFloat(), 3.0f);
            g.setColour(Theme::active);
            g.drawRoundedRectangle(detachBtn.toFloat(), 3.0f, 1.0f);
            g.setColour(Theme::active);
            g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
            g.drawText(detached ? juce::String("DOCK") : juce::String("POP OUT"), detachBtn, juce::Justification::centred);
        }

        auto tracks = audioEngine.getMixerTracks();
        if (tracks.isEmpty())
        {
            g.setColour(Theme::textMuted);
            g.setFont (Theme::uiSize (12.0f));
            g.drawText("Add a track to see the console.",
                       0, kHeaderH, getWidth(), getHeight() - kHeaderH, juce::Justification::centred);
            stripHits.clearQuick();
            return;
        }

        // Last paint's hit areas: strips whose layout has not changed can reuse
        // them and skip drawing when they are outside the repainted area.
        juce::Array<StripHit> previousHits;
        previousHits.swapWith (stripHits);

        int x = 12;
        int y = kHeaderH + 8;
        int h = getHeight() - y - 12;

        for (int i = 0; i <= tracks.size(); ++i)
        {
            bool isMaster = (i == tracks.size());
            tracktion::Track* track = isMaster ? audioEngine.getMasterTrack() : tracks[i];
            juce::Colour tColor = isMaster ? Theme::meterRed : Theme::colourForTrack(i);
            int stripW = kStripW;
            if (! isMaster)
                stripW += kInsertColW;
            if (auto* f = dynamic_cast<tracktion::FolderTrack*> (track))
                if (audioEngine.isFolderSubmix (f))
                    stripW += kFolderSubmixExtraW;

            const juce::Rectangle<int> stripBounds (x, y, stripW, h);

            const StripHit* previous = nullptr;
            for (auto& p : previousHits)
                if (p.track == track && p.stripBounds == stripBounds)
                    previous = &p;

            if (previous != nullptr && ! g.clipRegionIntersects (stripBounds.expanded (kStripPaintMargin)))
            {
                stripHits.add (*previous);
            }
            else
            {
                // During playback the clip is two small rectangles per strip.
                // Narrowing it to this strip keeps each fill from walking the
                // whole list, which the software renderer pays for per shape.
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (stripBounds.expanded (kStripPaintMargin));
                paintStrip (g, stripBounds, track, tColor, isMaster, previous);
            }

            x += stripW + kStripGap;
            if (isMaster) break;
            if (i == tracks.size() - 1) x += kMasterGap;
        }
    }

    /** `previous` is this strip's hit areas from the last paint if its layout
        is unchanged, or nullptr. With it, sections outside the repainted area
        are skipped and their hit areas carried over; during playback that
        leaves only the meters to draw. Without it everything is drawn. */
    void paintStrip(juce::Graphics& g, juce::Rectangle<int> cb,
                    tracktion::Track* track, juce::Colour tColor, bool isMaster,
                    const StripHit* previous)
    {
        StripHit hit;
        hit.track       = track;
        hit.isMaster    = isMaster;
        hit.stripBounds = cb;

        auto needsPaint = [&g, previous] (juce::Rectangle<int> area)
        {
            return previous == nullptr || g.clipRegionIntersects (area);
        };

        auto* folder = dynamic_cast<tracktion::FolderTrack*>(track);

        // Background panel
        g.setColour (folder != nullptr ? Theme::bgPanel.darker (0.1f) : Theme::bgPanel);
        g.fillRoundedRectangle (cb.toFloat(), 4.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (cb.toFloat(), 4.0f, 1.0f);
        if (folder != nullptr && audioEngine.isFolderSubmix (folder))
        {
            g.setColour (tColor.withAlpha (0.95f));
            g.drawRoundedRectangle (cb.toFloat().expanded (0.75f), 5.0f, 1.5f);
        }

        auto inner = cb.reduced (4);

        auto colourBand = inner.removeFromTop (kColorBandH);
        inner.removeFromTop (2);
        auto nameArea = inner.removeFromTop (kNameH);
        inner.removeFromTop (2);

        if (needsPaint (colourBand.getUnion (nameArea)))
        {
            // Color band
            g.setColour (tColor);
            g.fillRoundedRectangle (colourBand.toFloat(), 2.0f);

            // Track name
            g.setColour (Theme::textMain);
            g.setFont (Theme::uiSize (11.0f).withStyle (juce::Font::bold));
            juce::String name = isMaster ? juce::String ("MASTER") : track->getName();
            if (folder != nullptr)
            {
                auto iconR = nameArea.removeFromLeft (16).reduced (2);
                g.setColour (Theme::textMuted);
                juce::Path p;
                p.addRoundedRectangle (iconR.getX(), iconR.getY() + 2, iconR.getWidth(), iconR.getHeight() - 4, 1.0f);
                p.addRectangle (iconR.getX(), iconR.getY(), 6, 4);
                g.fillPath (p);
                g.setColour (Theme::textMain);
            }
            g.drawText (name, nameArea, juce::Justification::centred);
        }

        auto stripBody = inner;
        auto rightCol = stripBody.removeFromRight (kSideBtnColW);
        stripBody.removeFromRight (2);

        juce::Rectangle<int> insertCol;
        if (! isMaster)
        {
            insertCol = stripBody.removeFromRight (kInsertColW);
            stripBody.removeFromRight (2);
        }

        const float pan      = audioEngine.getTrackPan (track);
        const float volumeDb = audioEngine.getTrackVolumeDb (track);

        // Pan knob row
        auto panArea = stripBody.removeFromTop (kPanAreaH);
        hit.panArea = panArea;
        const bool panPainted = needsPaint (panArea);

        if (panPainted)
        {
            drawPanKnob (g, panArea, pan);

            // dB gain readout to the right of the knob
            int lx = panArea.getX() + kPanKnobSize + 5;
            g.setColour (Theme::textMuted.withAlpha (0.8f));
            g.setFont (Theme::uiSize (9.5f));
            g.drawText (juce::String::formatted ("%.2fdB", volumeDb),
                        juce::Rectangle<int> (lx, panArea.getBottom() - 12,
                                              panArea.getRight() - lx, 12),
                        juce::Justification::centredLeft, false);
        }
        stripBody.removeFromTop (2);

        // Right-side button column
        if (needsPaint (rightCol))
        {
            juce::Rectangle<int> btnHits[5];
            drawSideButtonColumn (g, rightCol, track, audioEngine, btnHits);
            hit.muteBtn  = btnHits[0];
            hit.soloBtn  = btnHits[1];
            hit.monoBtn  = btnHits[2];
            hit.fxBtn    = btnHits[3];
            hit.infoBtn  = btnHits[4];
        }
        else
        {
            hit.muteBtn = previous->muteBtn;
            hit.soloBtn = previous->soloBtn;
            hit.monoBtn = previous->monoBtn;
            hit.fxBtn   = previous->fxBtn;
            hit.infoBtn = previous->infoBtn;
        }

        // Fader + dual meters (fills remaining inner zone)
        auto faderZone = stripBody.withTrimmedBottom (kBottomH);
        hit.faderArea = faderZone;
        // The fader cap overhangs the top of the zone by up to 14 px near +6 dB,
        // into the pan row, so a repaint there must redraw the cap too.
        const bool faderPainted = needsPaint (faderZone.withTop (faderZone.getY() - 16));

        if (faderPainted)
        {
            const bool k14 = isMaster && (bool) projectData.getProjectTree().getProperty (IDs::masterKMeter, false);
            ::paintFader (g, faderZone, audioEngine, track, tColor, isMaster,
                          faderKnobDrawable.get(), &hit.peakReadoutArea, k14);
        }
        else
        {
            hit.peakReadoutArea = previous->peakReadoutArea;
        }

        // Record what is on screen now. A value only counts as shown once every
        // place that displays it has been drawn; otherwise keep the old value
        // so the next playback tick notices and repaints the whole strip.
        hit.paintedPan      = panPainted ? pan : previous->paintedPan;
        hit.paintedVolumeDb = (panPainted && faderPainted) ? volumeDb : previous->paintedVolumeDb;

        if (! isMaster && insertCol.getWidth() > 0 && ! needsPaint (insertCol))
        {
            hit.insertRowHits = previous->insertRowHits;
        }
        else if (! isMaster && insertCol.getWidth() > 0)
        {
            juce::Array<tracktion::ExternalPlugin*> externals;
            for (auto* pl : track->pluginList)
                if (auto* ep = dynamic_cast<tracktion::ExternalPlugin*> (pl))
                    externals.add (ep);

            int slotY = insertCol.getY();
            const int slotH = 16;
            const int slotGap = 2;

            for (auto* ep : externals)
            {
                if (slotY + slotH > insertCol.getBottom())
                    break;

                juce::Rectangle<int> slot (insertCol.getX(), slotY, insertCol.getWidth(), slotH);
                hit.insertRowHits.add (paintCompactInsertSlot (g, slot, *ep, audioEngine));
                slotY += slotH + slotGap;
            }

            if (insertDragTrack == track && insertDragState.dropPreviewY >= 0 && ! hit.insertRowHits.isEmpty())
                paintInsertDropLine (g, insertDragState.dropPreviewY,
                                     hit.insertRowHits.getFirst().row.getX(),
                                     hit.insertRowHits.getFirst().row.getWidth());
        }

        // Plugin-drag hover highlight
        if (! isMaster && pluginDragHoverTrack == track)
        {
            g.setColour (Theme::accent.withAlpha (0.18f));
            g.fillRoundedRectangle (cb.toFloat(), 4.0f);
            g.setColour (Theme::accent.withAlpha (0.90f));
            g.drawRoundedRectangle (cb.toFloat(), 4.0f, 2.0f);
        }

        stripHits.add (hit);
    }

        static void drawPanKnob (juce::Graphics& g, juce::Rectangle<int> b, float pan)
        {
            int   kd = kPanKnobSize;
            int   kx = b.getX() + 2;
            int   ky = b.getY() + (b.getHeight() - kd) / 2;
            float cx = kx + kd / 2.0f;
            float cy = ky + kd / 2.0f;
            float r  = kd / 2.0f - 2.0f;

            // JUCE arc: 0 = 12 o'clock, clockwise positive
            const float kStart = juce::MathConstants<float>::pi * 1.1667f; // ~7 o'clock
            const float kEnd   = juce::MathConstants<float>::pi * 2.8333f; // ~5 o'clock
            float t     = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) / 2.0f;
            float angle = kStart + t * (kEnd - kStart);

            // Groove arc
            juce::Path groove;
            groove.addArc (cx - r, cy - r, r * 2.0f, r * 2.0f, kStart, kEnd, true);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.strokePath (groove, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

            // Value arc from 12 o'clock to current angle
            float mid  = kStart + 0.5f * (kEnd - kStart);
            float arcA = juce::jmin (mid, angle);
            float arcB = juce::jmax (mid, angle);
            if (arcB - arcA > 0.01f)
            {
                juce::Path fill;
                fill.addArc (cx - r, cy - r, r * 2.0f, r * 2.0f, arcA, arcB, true);
                g.setColour (Theme::active);
                g.strokePath (fill, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }

            // Knob body
            float br = r - 2.5f;
            g.setColour (Theme::surface.brighter (0.15f));
            g.fillEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f);
            g.setColour (Theme::border);
            g.drawEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f, 1.0f);

            // Pointer line
            float px = cx + (br - 2.0f) * std::sin (angle);
            float py = cy - (br - 2.0f) * std::cos (angle);
            g.setColour (Theme::textMain.withAlpha (0.9f));
            g.drawLine (cx, cy, px, py, 1.5f);

            // Label to the right of the knob
            float p = juce::jlimit (-1.0f, 1.0f, pan);
            juce::String label = juce::approximatelyEqual (p, 0.0f)
                ? juce::String ("center")
                : juce::String::formatted (p < 0 ? "L%d" : "R%d",
                                           (int) std::round (std::abs (p) * 100.0f));
            int lx = kx + kd + 3;
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (9.5f).withStyle (juce::Font::bold));
            g.drawText (label, juce::Rectangle<int> (lx, ky, b.getRight() - lx, kd),
                        juce::Justification::centredLeft, false);
        }

        // btns[0]=M, [1]=S, [2]=MONO, [3]=FX, [4]=i
        static void drawSideButtonColumn (juce::Graphics& g, juce::Rectangle<int> col,
                                          tracktion::Track* track,
                                          AudioEngineManager& audioEngine,
                                          juce::Rectangle<int> (&btns)[5])
        {
            const char* labels[5] = { "M", "S", "MONO", "FX", "i" };
            bool states[5] = {
                track->isMuted (false),
                track->isSolo  (false),
                audioEngine.getTrackMono (track),
                false, false
            };
            juce::Colour colours[5] = {
                Theme::meterYellow, Theme::accent, Theme::active, Theme::active, Theme::textMuted
            };
            auto cursor = col;
            for (int i = 0; i < 5; ++i)
            {
                btns[i] = cursor.removeFromTop (kSideBtnH);
                cursor.removeFromTop (kSideBtnGap);
                paintLetterButton (g, btns[i], labels[i], states[i], colours[i]);
            }
        }


        void showTrackContextMenu (tracktion::Track* t, juce::Point<int> screenPos)
        {
            juce::PopupMenu m;

            bool isPhase = audioEngine.getTrackPhase (t);
            bool isMono  = audioEngine.getTrackMono (t);

            m.addItem (1, "Phase Invert", true, isPhase);
            m.addItem (2, "Mono Sum", true, isMono);
            m.addSeparator();

            if (auto* f = dynamic_cast<tracktion::FolderTrack*> (t))
            {
                if (audioEngine.isFolderSubmix (f))
                    m.addItem (20, "Convert to Folder (organizational)");
                else
                    m.addItem (21, "Convert to Submix");
                m.addSeparator();
            }

            if (t == audioEngine.getMasterTrack())
            {
                const bool km = (bool) projectData.getProjectTree().getProperty (IDs::masterKMeter, false);
                m.addItem (22, "K-14 Reference Scale", true, km);
                m.addSeparator();
            }

            // Snapshots Submenu
            juce::PopupMenu snaps;
            auto names = audioEngine.getMixSnapshotNames();
            if (names.isEmpty())
            {
                snaps.addItem (0, "No snapshots saved", false);
            }
            else
            {
                for (int i = 0; i < names.size(); ++i)
                    snaps.addItem (100 + i, names[i]);
            }
            snaps.addSeparator();
            snaps.addItem (200, "Save New Snapshot...");

            m.addSubMenu ("Snapshots", snaps);

            m.addSeparator();
            m.addItem (3, "Reset Peak");

            m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                [this, t] (int result)
                {
                    if (result == 1)      audioEngine.setTrackPhase (t, ! audioEngine.getTrackPhase (t));
                    else if (result == 2) audioEngine.setTrackMono (t, ! audioEngine.getTrackMono (t));
                    else if (result == 3) audioEngine.clearTrackMaxPeak (t);
                    else if (result == 20 || result == 21)
                    {
                        if (auto* f = dynamic_cast<tracktion::FolderTrack*> (t))
                            audioEngine.setFolderSubmix (f, result == 21);
                    }
                    else if (result == 22 && t == audioEngine.getMasterTrack())
                    {
                        auto tree = projectData.getProjectTree();
                        const bool cur = (bool) tree.getProperty (IDs::masterKMeter, false);
                        tree.setProperty (IDs::masterKMeter, ! cur, nullptr);
                    }
                    else if (result == 200)
                    {
                        // Show a quick dialog or just auto-name it
                        juce::String name = "Mix " + juce::String (audioEngine.getMixSnapshotNames().size() + 1);
                        audioEngine.saveMixSnapshot (name);
                    }
                    else if (result >= 100)
                    {
                        auto names = audioEngine.getMixSnapshotNames();
                        int idx = result - 100;
                        if (idx < names.size())
                            audioEngine.recallMixSnapshot (names[idx]);
                    }
                    repaint();
                });
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (detachBtn.contains (e.getPosition())) {
                if (onDetachRequested) onDetachRequested();
                return;
            }

            if (e.mods.isPopupMenu())
            {
                for (auto& hit : stripHits)
                    if (hit.stripBounds.contains (e.getPosition()))
                    {
                        showTrackContextMenu (hit.track, e.getScreenPosition());
                        return;
                    }
            }

            for (auto& hit : stripHits)
            {
                if (hit.muteBtn.contains (e.getPosition()))  { audioEngine.toggleTrackMute (hit.track); repaint(); return; }
                if (hit.soloBtn.contains (e.getPosition()))  { audioEngine.toggleTrackSolo (hit.track); repaint(); return; }

                if (hit.monoBtn.contains (e.getPosition()))
                {
                    audioEngine.setTrackMono (hit.track, ! audioEngine.getTrackMono (hit.track));
                    repaint(); return;
                }

                if (hit.fxBtn.contains (e.getPosition()))
                {
                    auto* trk    = hit.track;
                    auto  screen = localAreaToGlobal (hit.fxBtn);
                    PluginPicker::show (audioEngine, screen, [this, trk] (const juce::PluginDescription& d) {
                        if (auto p = audioEngine.addPluginToTrack (trk, d))
                            p->showWindowExplicitly();
                        repaint();
                    });
                    return;
                }

                if (hit.infoBtn.contains (e.getPosition()))
                {
                    audioEngine.clearTrackMaxPeak (hit.track);
                    repaint(); return;
                }

                if (hit.peakReadoutArea.contains (e.getPosition()))
                {
                    audioEngine.clearTrackMaxPeak (hit.track);
                    repaint(); return;
                }

                if (hit.panArea.contains (e.getPosition()))
                {
                    activePanTrack  = hit.track;
                    activePanArea   = hit.panArea;
                    dragStartY      = e.y;
                    panAtDragStart  = audioEngine.getTrackPan (hit.track);
                    return;
                }

                if (handleInsertRowMouseDown (e, hit.insertRowHits, audioEngine, hit.track, insertDragState,
                                              [this] { repaint(); }))
                {
                    if (insertDragState.draggingExternalIndex >= 0)
                        insertDragTrack = hit.track;
                    return;
                }

                if (hit.faderArea.contains (e.getPosition()))
                {
                    if (e.mods.isPopupMenu())
                    {
                        showTrackContextMenu (hit.track, e.getScreenPosition());
                        return;
                    }
                    activeFaderTrack = hit.track;
                    return;
                }
            }
        }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (insertDragTrack != nullptr && insertDragState.draggingExternalIndex >= 0)
        {
            for (auto& hit : stripHits)
            {
                if (hit.track == insertDragTrack)
                {
                    handleInsertRowMouseDrag (e, hit.insertRowHits, insertDragState, [this] { repaint(); });
                    return;
                }
            }
        }

        if (activePanTrack)
        {
            float delta = (dragStartY - e.y) / 100.0f; // up = right, down = left
            audioEngine.setTrackPan(activePanTrack, juce::jlimit(-1.0f, 1.0f, panAtDragStart + delta));
            repaint();
            return;
        }

        if (activeFaderTrack)
        {
            for (auto& hit : stripHits)
                if (hit.track == activeFaderTrack)
                { ::setFaderFromY(audioEngine, activeFaderTrack, hit.faderArea, e.y); break; }
            repaint();
        }
    }

    /** Where `track`'s fader took mouse input at the last paint (for tests). */
    juce::Rectangle<int> getFaderArea (const tracktion::Track* track) const
    {
        for (auto& hit : stripHits)
            if (hit.track == track)
                return hit.faderArea;
        return {};
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (insertDragTrack != nullptr)
        {
            for (auto& hit : stripHits)
            {
                if (hit.track == insertDragTrack)
                {
                    handleInsertRowMouseUp (audioEngine, hit.track, hit.insertRowHits, insertDragState,
                                            [this] { repaint(); });
                    break;
                }
            }
            insertDragTrack = nullptr;
        }

        juce::ignoreUnused (e);
        activePanTrack   = nullptr;
        activeFaderTrack = nullptr;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        for (auto& hit : stripHits)
        {
            if (hit.faderArea.contains (e.getPosition()))
            {
                audioEngine.setTrackVolumeDb (hit.track, 0.0f);
                repaint();
                return;
            }

            if (hit.panArea.contains (e.getPosition()))
            {
                audioEngine.setTrackPan (hit.track, 0.0f);
                repaint();
                return;
            }
        }
    }

private:
    AudioEngineManager& audioEngine;
    ProjectData& projectData;

    juce::Rectangle<int>     detachBtn;
    juce::Array<StripHit>    stripHits;

    tracktion::Track*       activePanTrack   = nullptr;
    juce::Rectangle<int>    activePanArea;
    tracktion::Track*       activeFaderTrack = nullptr;
    float panAtDragStart = 0.0f;
    int   dragStartY     = 0;

    tracktion::Track* insertDragTrack = nullptr;
    InsertRowDragState insertDragState;

    // Plugin drag hover state
    tracktion::Track* pluginDragHoverTrack = nullptr;

    // -- DragAndDropTarget (plugin drag from Browser) -------------------------
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        return d.description.toString().startsWith ("PLUGIN:");
    }
    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        pluginDragHoverTrack = getStripTrackAt (d.localPosition);
        repaint();
    }
    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        pluginDragHoverTrack = getStripTrackAt (d.localPosition);
        repaint();
    }
    void itemDragExit (const juce::DragAndDropTarget::SourceDetails&) override
    {
        pluginDragHoverTrack = nullptr;
        repaint();
    }
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        auto* track = getStripTrackAt (d.localPosition);
        pluginDragHoverTrack = nullptr;

        if (track != nullptr && onPluginDroppedOnStrip)
        {
            juce::String idStr = d.description.toString()
                                     .fromFirstOccurrenceOf ("PLUGIN:", false, false);
            auto& known = audioEngine.getEngine().getPluginManager().knownPluginList;
            for (auto& t : known.getTypes())
            {
                if (t.createIdentifierString() == idStr)
                {
                    onPluginDroppedOnStrip (track, t);
                    break;
                }
            }
        }
        repaint();
    }

    tracktion::Track* getStripTrackAt (juce::Point<int> pos)
    {
        auto tracks = audioEngine.getMixerTracks();
        int x = 12;
        int y = kHeaderH + 8;
        int h = getHeight() - y - 12;
        for (int i = 0; i <= tracks.size(); ++i)
        {
            const bool isMaster = (i == tracks.size());
            auto* track = isMaster ? audioEngine.getMasterTrack() : tracks[i];
            int stripW = kStripW;
            if (! isMaster)
                stripW += kInsertColW;
            if (auto* f = dynamic_cast<tracktion::FolderTrack*> (track))
                if (audioEngine.isFolderSubmix (f))
                    stripW += kFolderSubmixExtraW;
            juce::Rectangle<int> strip (x, y, stripW, h);
            if (strip.contains (pos))
                return track;
            x += stripW + kStripGap;
            if (! isMaster && i == tracks.size() - 1) x += kMasterGap;
            if (isMaster) break;
        }
        return nullptr;
    }
};
