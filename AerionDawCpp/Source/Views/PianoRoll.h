#pragma once

// Piano Roll editor and its floating window.

#include "ViewShared.h"
#include <array>
#include <unordered_set>

//==============================================================================
// Piano Roll editor  -  opens when the user double-clicks an existing MIDI clip.
class PianoRollEditor : public juce::Component,
                        public juce::ScrollBar::Listener,
                        public juce::ComboBox::Listener,
                        public juce::ValueTree::Listener
{
public:
    enum class PRDragMode { none, notes, velocity, ccLane, laneSplitter, marquee };

    struct DragNoteState
    {
        tracktion::MidiNote* note = nullptr;
        double start = 0.0;
        double length = 0.0;
        int noteNumber = 0;
    };

    struct CopiedNote
    {
        int note = 0;
        double start = 0.0;
        double length = 0.0;
        int velocity = 100;
        int colour = 0;
    };

    std::function<void()> onDetachRequested;

    /** Transport › Follow Playback, shared with the Timeline. */
    void setFollowPlayback (bool shouldFollow)
    {
        followPlayback = shouldFollow;
        followPlayheadInView = true;
    }

    /** Called every display frame. Moves the playhead line and, with Follow
        Playback on, pages the view the same way Timeline::followPlayhead does,
        including leaving the user alone after they scroll the playhead away. */
    void updatePlayhead (double seconds, bool playing)
    {
        const double beat = midiClip.getContentBeatAtTime (tracktion::TimePosition::fromSeconds (seconds)).inBeats();

        const bool jumped = ! followWasPlaying
                         || seconds < followLastSeconds - 0.001
                         || seconds > followLastSeconds + 0.5;
        const bool scrolledByUser = viewBeat != followViewBeat;
        followWasPlaying  = playing;
        followLastSeconds = seconds;
        followViewBeat    = viewBeat;

        const double content = contentBeats();

        if (followPlayback && playing && beat >= 0.0 && beat <= content)
        {
            const auto ga = gridArea();
            const float x = beatToX (beat);

            if (x >= (float) ga.getX() && x < (float) ga.getRight())
            {
                followPlayheadInView = true;
            }
            else if (! jumped && (scrolledByUser || ! followPlayheadInView))
            {
                followPlayheadInView = false;
            }
            else
            {
                const double maxStart = juce::jmax (0.0, content - visibleBeats());
                viewBeat = juce::jlimit (0.0, maxStart, beat - 0.05 * visibleBeats());
                updateScrollRanges();
                followViewBeat = viewBeat;
                followPlayheadInView = true;
                repaint();
            }
        }

        playheadBeat = beat;
        const int x = juce::roundToInt (beatToX (beat));
        if (x != playheadX)
        {
            repaint (playheadStrip (playheadX));
            playheadX = x;
            repaint (playheadStrip (playheadX));
        }
    }

    /** What updatePlayhead repaints for a playhead at x (AerionBench measures it). */
    juce::Rectangle<int> getPlayheadStrip (int x) const    { return playheadStrip (x); }

    PianoRollEditor (tracktion::MidiClip& clip, tracktion::Edit& edit,
                     ProjectData& pd, AudioEngineManager& ae)
        : midiClip (clip), edit (edit), projectData (pd), audioEngine (ae)
    {
        projectData.getProjectTree().addListener (this);
        midiClip.state.addListener (this);
        snapEnabled = projectData.getProjectTree().getProperty (IDs::snapEnabled);
        snapInterval = projectData.getProjectTree().getProperty (IDs::snapInterval);

        addAndMakeVisible (hScroll);
        addAndMakeVisible (vScroll);
        hScroll.setAutoHide (false);
        vScroll.setAutoHide (false);
        hScroll.addListener (this);
        vScroll.addListener (this);
        scrollY = (127 - 24) * kRowH;   // open near C1 by default

        ccTypeCombo.addItem ("Pitch Bend", 1);
        ccTypeCombo.addItem ("CC1 Mod Wheel", 2);
        ccTypeCombo.addItem ("CC7 Volume", 3);
        ccTypeCombo.addItem ("CC10 Pan", 4);
        ccTypeCombo.addItem ("CC11 Expression", 5);
        ccTypeCombo.addItem ("CC64 Sustain", 6);
        ccTypeCombo.addItem ("Other CC...", 7);
        ccTypeCombo.addListener (this);
        addAndMakeVisible (ccTypeCombo);
        syncCCSelectorFromClipState();

        // Note duration dropdown — 1/1 through 1/128, plus triplet variants
        noteDurationCombo.addItem ("1/1", 1);
        noteDurationCombo.addItem ("1/1T", 2);
        noteDurationCombo.addItem ("1/2", 3);
        noteDurationCombo.addItem ("1/2T", 4);
        noteDurationCombo.addItem ("1/4", 5);      // default
        noteDurationCombo.addItem ("1/4T", 6);
        noteDurationCombo.addItem ("1/8", 7);
        noteDurationCombo.addItem ("1/8T", 8);
        noteDurationCombo.addItem ("1/16", 9);
        noteDurationCombo.addItem ("1/16T", 10);
        noteDurationCombo.addItem ("1/32", 11);
        noteDurationCombo.addItem ("1/32T", 12);
        noteDurationCombo.addItem ("1/64", 13);
        noteDurationCombo.addItem ("1/64T", 14);
        noteDurationCombo.addItem ("1/128", 15);
        noteDurationCombo.addItem ("1/128T", 16);
        noteDurationCombo.setSelectedId (5, juce::dontSendNotification);  // default 1/4
        noteDurationCombo.addListener (this);
        addAndMakeVisible (noteDurationCombo);

        // Zoom slider
        zoomSlider.setRange (20.0, 400.0, 1.0);
        zoomSlider.setValue (pxPerBeat);
        zoomSlider.setColour (juce::Slider::trackColourId, Theme::border.withAlpha (0.5f));
        zoomSlider.setColour (juce::Slider::thumbColourId, Theme::active);
        zoomSlider.onValueChange = [this] { pxPerBeat = zoomSlider.getValue(); updateScrollRanges(); repaint(); };
        addAndMakeVisible (zoomSlider);

        detachBtn.setColour (juce::TextButton::buttonColourId,  Theme::surface);
        detachBtn.setColour (juce::TextButton::textColourOffId, Theme::textMuted);
        detachBtn.onClick = [this] { if (onDetachRequested) onDetachRequested(); };
        addAndMakeVisible (detachBtn);

        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    ~PianoRollEditor() override
    {
        projectData.getProjectTree().removeListener (this);
        midiClip.state.removeListener (this);
    }

    void comboBoxChanged (juce::ComboBox* box) override
    {
        if (box == &noteDurationCombo)
        {
            static const double kDurations[] = {
                4.0,        8.0/3.0,     // 1/1, 1/1T
                2.0,        4.0/3.0,     // 1/2, 1/2T
                1.0,        2.0/3.0,     // 1/4, 1/4T
                0.5,        1.0/3.0,     // 1/8, 1/8T
                0.25,       1.0/6.0,     // 1/16, 1/16T
                0.125,      1.0/12.0,    // 1/32, 1/32T
                0.0625,     1.0/24.0,    // 1/64, 1/64T
                0.03125,    1.0/48.0     // 1/128, 1/128T
            };
            int idx = box->getSelectedId() - 1;
            if (idx >= 0 && idx < 16)
            {
                defaultNoteLength = kDurations[idx];
                // Sync the snap grid to match note duration, but only for non-triplet values
                // (triplets are odd divisions that don't create clean grid lines)
                bool isTriplet = (idx % 2 == 1);  // odd indices are triplets
                if (!isTriplet)
                {
                    snapInterval = defaultNoteLength;
                    projectData.getProjectTree().setProperty (IDs::snapInterval, defaultNoteLength, nullptr);
                    repaint();
                }
            }
            return;
        }

        if (box != &ccTypeCombo) return;

        if (! canEditMidiClip())
        {
            syncCCSelectorFromClipState();
            return;
        }

        const int id = ccTypeCombo.getSelectedId();
        if (id == 7)
        {
            auto* alert = new juce::AlertWindow ("Custom CC number", "Enter controller number (0-127):", juce::AlertWindow::QuestionIcon);
            alert->addTextEditor ("cc", juce::String (ccCustomNumber), "CC");
            alert->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<PianoRollEditor> safe (this);
            alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, alert] (int r) mutable
            {
                std::unique_ptr<juce::AlertWindow> owned (alert);
                if (safe == nullptr) return;
                safe->handleCustomCCAlert (r, owned->getTextEditorContents ("cc"));
            }), true);
            return;
        }

        int stored = 1;
        switch (id)
        {
            case 1: stored = -1; break;
            case 2: stored = 1;  break;
            case 3: stored = 7;  break;
            case 4: stored = 10; break;
            case 5: stored = 11; break;
            case 6: stored = 64; break;
            default: return;
        }
        midiClip.state.setProperty (IDs::pianoRollCC, stored, &edit.getUndoManager());
    }

    /** Called from modal dialog (must be public for non-member callback). */
    void handleCustomCCAlert (int result, juce::String ccText)
    {
        if (! canEditMidiClip())
        {
            syncCCSelectorFromClipState();
            repaint();
            return;
        }

        if (result == 1)
        {
            int v = juce::jlimit (0, 127, ccText.getIntValue());
            ccCustomNumber = v;
            midiClip.state.setProperty (IDs::pianoRollCC, v, &edit.getUndoManager());
        }
        else
            syncCCSelectorFromClipState();
        repaint();
    }

    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override
    {
        if (&v == &midiClip.state)
        {
            if (i == IDs::pianoRollCC)
                syncCCSelectorFromClipState();
            repaint();
            return;
        }

        if (&v == &projectData.getProjectTree())
        {
            if (i == IDs::snapEnabled)
                snapEnabled = v.getProperty (i);
            else if (i == IDs::snapInterval)
                snapInterval = v.getProperty (i);
        }

        repaint();
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}

    void paint (juce::Graphics& g) override
    {
        AERION_PROFILE_SCOPE ("PianoRollEditor::paint");

        prepareSelectionForPaint();

        g.fillAll (Theme::bgBase);
        auto ga = gridArea();
        auto ca = ccLaneArea();
        auto va = velocityArea();

        drawGrid (g, ga);
        drawCCLane (g, ca);
        drawVelocityLane (g, va);
        drawNotes (g, ga);
        drawPianoKeys (g);

        // Clip-length end marker (grid + CC + velocity stack)
        g.setColour (Theme::active.withAlpha (0.4f));
        float endX = beatToX (clipLengthBeats());
        if (endX > (float) kKeyW && endX < (float) ga.getRight())
            g.fillRect (endX, (float) kGridTop, 2.0f, (float) (va.getBottom() - kGridTop));

        playheadX = juce::roundToInt (beatToX (playheadBeat));
        if (playheadX >= kKeyW && playheadX < ga.getRight())
        {
            g.setColour (Theme::textMain.withAlpha (0.85f));
            g.fillRect ((float) playheadX, (float) kToolbarH, 1.5f, (float) (va.getBottom() - kToolbarH));
        }
    }

    void resized() override
    {
        hScroll.setBounds (getLocalBounds().removeFromBottom (kBottomScrollH).withTrimmedLeft (kKeyW));
        vScroll.setBounds (getLocalBounds().removeFromRight (14).withTrimmedTop (kGridTop).withTrimmedBottom (bottomStackH()));
        noteDurationCombo.setBounds (kKeyW + 4, 4, 120, kToolbarH - 8);
        zoomSlider.setBounds (kKeyW + 128, 4, 150, kToolbarH - 8);
        detachBtn.setBounds (getWidth() - 70, 4, 64, kToolbarH - 8);
        detachBtn.setVisible (onDetachRequested != nullptr);
        ccTypeCombo.setBounds (kKeyW + 2, ccLaneArea().getY() + 2, 118, 18);
        updateScrollRanges();
    }

    void scrollBarMoved (juce::ScrollBar* bar, double v) override
    {
        if (bar == &hScroll) viewBeat = v;
        else                 scrollY  = (int) v;
        repaint();
    }

    juce::MouseCursor getMouseCursor() override
    {
        const auto pos = getMouseXYRelative();
        auto ga = gridArea();

        if (laneSplitterArea().contains (pos))
            return juce::MouseCursor::UpDownResizeCursor;

        if (ccLaneArea().contains (pos) || velocityArea().contains (pos))
            return juce::MouseCursor::PointingHandCursor;

        if (ga.contains (pos))
        {
            if (auto* note = getNoteAt (pos))
            {
                auto r = noteRect (note, ga);
                if (pos.x > r.getRight() - 6.0f)
                    return juce::MouseCursor::LeftRightResizeCursor;

                return juce::MouseCursor::DraggingHandCursor;
            }

            return juce::MouseCursor::NormalCursor;
        }

        return juce::MouseCursor::NormalCursor;
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        auto& km = audioEngine.getKeymap();

        if (km.matches ("edit.undo", key)) { edit.getUndoManager().undo(); repaint(); return true; }
        if (km.matches ("edit.redo", key)) { edit.getUndoManager().redo(); repaint(); return true; }

        if (km.matches ("pianoRoll.selectAll", key)) { selectAllNotes();    return true; }
        if (km.matches ("pianoRoll.copy",      key)) { copySelection();     return true; }
        if (km.matches ("pianoRoll.cut",       key)) { if (canEditMidiClip()) cutSelection();       return true; }
        if (km.matches ("pianoRoll.paste",     key)) { if (canEditMidiClip()) pasteClipboard();     return true; }
        if (km.matches ("pianoRoll.duplicate", key)) { if (canEditMidiClip()) duplicateSelection(); return true; }

        if (km.matches ("transport.playStop", key))
        {
            if (audioEngine.isPlaying())
                audioEngine.stop();
            else
                audioEngine.play();
            repaint();
            return true;
        }

        if (km.matches ("transport.goToStart", key))
        {
            bool wasPlaying = audioEngine.isPlaying();
            audioEngine.setTransportPosition (0.0);
            if (wasPlaying)
                audioEngine.play();
            repaint();
            return true;
        }

        if (km.matches ("pianoRoll.clearSel", key)) { clearSelection(); return true; }

        if (km.matches ("pianoRoll.delete", key))
        {
            if (canEditMidiClip())
                deleteSelectedNotes ("Delete MIDI notes");
            return true;
        }

        const bool nudgeL = km.matches ("pianoRoll.nudgeLeft",  key);
        const bool nudgeR = km.matches ("pianoRoll.nudgeRight", key);
        if (nudgeL || nudgeR)
        {
            if (! canEditMidiClip())
                return true;

            double delta = nudgeL ? -snapInterval : snapInterval;
            if (! snapEnabled)
                delta *= 0.1;

            nudgeSelectedNotes (delta);
            return true;
        }

        const bool transUp = km.matches ("pianoRoll.transposeUp",   key);
        const bool transDn = km.matches ("pianoRoll.transposeDown", key);
        if (transUp || transDn)
        {
            if (canEditMidiClip())
                transposeSelectedNotes (transUp ? 1 : -1);
            return true;
        }

        if (km.matches ("pianoRoll.quantize", key))
        {
            if (canEditMidiClip())
                quantizeSelectedNotes();
            return true;
        }

        return false;
    }

    void quantize()
    {
        if (canEditMidiClip())
            quantizeSelectedNotes();
    }

    void notifyMidiContentChanged()
    {
        audioEngine.notifyEditContentChanged();
    }

    bool canEditMidiClip()
    {
        return ! isClipTrackFrozenOrFreezing (audioEngine, &midiClip);
    }

    void deleteSelectedNotes (const juce::String& transactionName)
    {
        if (! canEditMidiClip())
            return;

        auto notes = getSelectedNotes();
        if (notes.isEmpty())
            return;

        beginUndo (transactionName);
        for (auto* n : notes)
            midiClip.getSequence().removeNote (*n, &edit.getUndoManager());

        selectedNotes.clearQuick();
        updateScrollRanges();
        notifyMidiContentChanged();
        repaint();
    }

    void nudgeSelectedNotes (double delta)
    {
        if (! canEditMidiClip())
            return;

        auto notes = getSelectedNotes();
        if (notes.isEmpty())
            return;

        beginUndo ("Nudge MIDI notes");
        for (auto* n : notes)
        {
            const double start = n->getStartBeat().inBeats();
            const double len = n->getLengthBeats().inBeats();
            n->setStartAndLength (tracktion::BeatPosition::fromBeats (juce::jmax (0.0, start + delta)),
                                  tracktion::BeatDuration::fromBeats (len),
                                  &edit.getUndoManager());
        }
        updateScrollRanges();
        notifyMidiContentChanged();
        repaint();
    }

    void transposeSelectedNotes (int delta)
    {
        if (! canEditMidiClip())
            return;

        auto notes = getSelectedNotes();
        if (notes.isEmpty())
            return;

        beginUndo ("Transpose MIDI notes");
        for (auto* n : notes)
            n->setNoteNumber (juce::jlimit (0, 127, n->getNoteNumber() + delta), &edit.getUndoManager());

        if (auto* first = notes.getFirst())
            setAuditionNote (first->getNoteNumber());
        notifyMidiContentChanged();
        repaint();
    }

    void quantizeSelectedNotes()
    {
        if (! canEditMidiClip())
            return;

        auto notes = getSelectedNotes();
        if (notes.isEmpty())
            return;

        beginUndo ("Quantize MIDI notes");
        for (auto* n : notes)
        {
            const double s = n->getStartBeat().inBeats();
            const double l = n->getLengthBeats().inBeats();
            const double ns = std::floor (s / snapInterval + 0.5) * snapInterval;
            const double ne = std::floor ((s + l) / snapInterval + 0.5) * snapInterval;
            n->setStartAndLength (tracktion::BeatPosition::fromBeats (ns),
                                  tracktion::BeatDuration::fromBeats (juce::jmax (snapInterval, ne - ns)),
                                  &edit.getUndoManager());
        }
        updateScrollRanges();
        notifyMidiContentChanged();
        repaint();
    }

    void copySelection()
    {
        auto notes = getSelectedNotes();
        noteClipboard.clearQuick();

        if (notes.isEmpty())
            return;

        double earliest = std::numeric_limits<double>::max();
        for (auto* n : notes)
            earliest = juce::jmin (earliest, n->getStartBeat().inBeats());

        for (auto* n : notes)
        {
            noteClipboard.add ({ n->getNoteNumber(),
                                 n->getStartBeat().inBeats() - earliest,
                                 n->getLengthBeats().inBeats(),
                                 (int) n->getVelocity(),
                                 0 });
        }
    }

    void cutSelection()
    {
        if (! canEditMidiClip())
            return;

        copySelection();
        deleteSelectedNotes ("Cut MIDI notes");
    }

    tracktion::MidiNote* insertCopiedNote (const CopiedNote& copied, double baseBeat)
    {
        if (! canEditMidiClip())
            return nullptr;

        auto before = midiClip.getSequence().getNotes();
        const double start = juce::jmax (0.0, baseBeat + copied.start);

        midiClip.getSequence().addNote (juce::jlimit (0, 127, copied.note),
            tracktion::BeatPosition::fromBeats (start),
            tracktion::BeatDuration::fromBeats (juce::jmax (0.03125, copied.length)),
            juce::jlimit (1, 127, copied.velocity), copied.colour, &edit.getUndoManager());

        for (auto* n : midiClip.getSequence().getNotes())
            if (! before.contains (n)
                && n->getNoteNumber() == copied.note
                && std::abs (n->getStartBeat().inBeats() - start) < 0.0001)
                return n;

        return findRecentlyAddedNote (copied.note, start, copied.length);
    }

    void pasteClipboard()
    {
        if (! canEditMidiClip())
            return;

        if (noteClipboard.isEmpty())
            return;

        beginUndo ("Paste MIDI notes");
        selectedNotes.clearQuick();

        const double pasteBeat = snapBeat (viewBeat);
        for (auto& copied : noteClipboard)
            if (auto* inserted = insertCopiedNote (copied, pasteBeat))
                selectedNotes.addIfNotAlreadyThere (inserted);

        updateScrollRanges();
        notifyMidiContentChanged();
        repaint();
    }

    void duplicateSelection()
    {
        if (! canEditMidiClip())
            return;

        auto notes = getSelectedNotes();
        if (notes.isEmpty())
            return;

        juce::Array<CopiedNote> duplicateNotes;
        double earliest = std::numeric_limits<double>::max();
        double latest = 0.0;

        for (auto* n : notes)
        {
            const double start = n->getStartBeat().inBeats();
            const double end = start + n->getLengthBeats().inBeats();
            earliest = juce::jmin (earliest, start);
            latest = juce::jmax (latest, end);
        }

        for (auto* n : notes)
            duplicateNotes.add ({ n->getNoteNumber(),
                                  n->getStartBeat().inBeats() - earliest,
                                  n->getLengthBeats().inBeats(),
                                  (int) n->getVelocity(),
                                  0 });

        beginUndo ("Duplicate MIDI notes");
        selectedNotes.clearQuick();
        for (auto& copied : duplicateNotes)
            if (auto* inserted = insertCopiedNote (copied, latest))
                selectedNotes.addIfNotAlreadyThere (inserted);

        noteClipboard = duplicateNotes;
        updateScrollRanges();
        notifyMidiContentChanged();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        // Transport button clicks (toolbar)
        if (e.y < kToolbarH && e.x >= kKeyW)
        {
            if (prStopBounds.contains (e.getPosition()))      { audioEngine.stop();   repaint(); return; }
            if (prPlayBounds.contains (e.getPosition()))      { if (audioEngine.isPlaying()) audioEngine.stop();
                                                                 else audioEngine.play();     repaint(); return; }
            if (prRecBounds.contains (e.getPosition()))       { audioEngine.record(); repaint(); return; }
            return;
        }

        auto ga = gridArea();
        auto ca = ccLaneArea();
        auto va = velocityArea();
        auto sp = laneSplitterArea();

        if (e.x < kKeyW)
        {
            setAuditionNote (yToNote (e.y));
            return;
        }

        if (! canEditMidiClip())
        {
            dragMode = PRDragMode::none;
            return;
        }

        if (sp.contains (e.getPosition()))
        {
            dragMode = PRDragMode::laneSplitter;
            splitterDragStartY = e.y;
            splitterStartCcH = kCCLaneH;
            splitterStartVelH = kVelocityLaneH;
            return;
        }

        if (ca.contains (e.getPosition()))
        {
            dragMode = PRDragMode::ccLane;
            if (e.mods.isRightButtonDown())
                tryRemoveCCEventAt (e);
            else
                applyCCValueAt (e.x, e.y, true);
            return;
        }

        if (va.contains (e.getPosition()))
        {
            dragMode = PRDragMode::velocity;
            updateVelocityAt (e.x, e.y);
            return;
        }

        if (e.y < kGridTop) return;

        if (e.mods.isRightButtonDown())
        {
            if (auto* n = getNoteAt (e.getPosition()))
            {
                beginUndo ("Delete MIDI note");
                midiClip.getSequence().removeNote (*n, &edit.getUndoManager());
                selectedNotes.removeFirstMatchingValue (n);
                updateScrollRanges();
                notifyMidiContentChanged();
                repaint();
            }
            return;
        }

        if (auto* n = getNoteAt (e.getPosition()))
        {
            if (e.mods.isShiftDown())
            {
                toggleSelected (n);
                setAuditionNote (n->getNoteNumber());
                return;
            }

            if (! selectedNotes.contains (n))
                selectOnly (n);

            draggingNote = n;
            dragBeat0 = xToBeat (e.x);
            dragNote0 = yToNote (e.y);
            origStart = n->getStartBeat().inBeats();
            origNote  = n->getNoteNumber();
            origLen   = n->getLengthBeats().inBeats();
            resizing = (e.x > noteRect (n, ga).getRight() - 6);
            dragMode = PRDragMode::notes;
            dragNoteStates.clearQuick();

            juce::Array<tracktion::MidiNote*> notesToDrag;
            if (resizing)
                notesToDrag.add (n);
            else
                notesToDrag = getSelectedNotes();
            for (auto* selected : notesToDrag)
                dragNoteStates.add ({ selected,
                                      selected->getStartBeat().inBeats(),
                                      selected->getLengthBeats().inBeats(),
                                      selected->getNoteNumber() });

            beginUndo (resizing ? "Resize MIDI note" : "Move MIDI notes");
            setAuditionNote (n->getNoteNumber());
            return;
        }

        dragMode = PRDragMode::marquee;
        marqueeAnchor = e.getPosition();
        marqueeBounds = {};
        marqueeAdditive = e.mods.isShiftDown();
        marqueeBaseSelection = marqueeAdditive ? getSelectedNotes() : juce::Array<tracktion::MidiNote*>();
        if (! marqueeAdditive)
            selectedNotes.clearQuick();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragMode == PRDragMode::laneSplitter)
        {
            int dy = e.y - splitterDragStartY;
            int newCc = juce::jlimit (40, 200, splitterStartCcH + dy);
            int newVel = juce::jlimit (40, 200, splitterStartVelH - dy);
            if (newCc + newVel > 280)
            {
                const int over = newCc + newVel - 280;
                if (dy >= 0)
                    newVel = juce::jmax (40, newVel - over);
                else
                    newCc = juce::jmax (40, newCc - over);
            }
            kCCLaneH = newCc;
            kVelocityLaneH = newVel;
            resized();
            repaint();
            return;
        }

        if (! canEditMidiClip())
        {
            dragMode = PRDragMode::none;
            draggingNote = nullptr;
            return;
        }

        if (dragMode == PRDragMode::ccLane)
        {
            if (! e.mods.isRightButtonDown())
                applyCCValueAt (e.x, e.y, false);
            return;
        }

        if (dragMode == PRDragMode::velocity)
        {
            updateVelocityAt (e.x, e.y);
            return;
        }

        if (dragMode == PRDragMode::marquee)
        {
            marqueeBounds = juce::Rectangle<int>::leftTopRightBottom (
                juce::jmin (marqueeAnchor.x, e.x), juce::jmin (marqueeAnchor.y, e.y),
                juce::jmax (marqueeAnchor.x, e.x), juce::jmax (marqueeAnchor.y, e.y));

            updateMarqueeSelection();
            repaint();
            return;
        }

        if (draggingNote == nullptr) return;
        float beat = xToBeat (e.x);
        int   note = yToNote (e.y);

        if (resizing)
        {
            double minLen = juce::jmin (defaultNoteLength, snapInterval) * 0.5;
            double newLen = juce::jmax (minLen, (double)(beat - origStart));
            draggingNote->setStartAndLength (
                tracktion::BeatPosition::fromBeats (origStart),
                tracktion::BeatDuration::fromBeats (snapBeat (newLen)),
                &edit.getUndoManager());
        }
        else
        {
            const double dBeat = snapBeat (beat - dragBeat0);
            const int dNote = note - dragNote0;

            for (auto& state : dragNoteStates)
            {
                if (state.note == nullptr)
                    continue;

                state.note->setStartAndLength (
                    tracktion::BeatPosition::fromBeats (juce::jmax (0.0, state.start + dBeat)),
                    tracktion::BeatDuration::fromBeats (state.length),
                    &edit.getUndoManager());
                state.note->setNoteNumber (juce::jlimit (0, 127, state.noteNumber + dNote), &edit.getUndoManager());
            }
        }
        setAuditionNote (note);
        updateScrollRanges();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        const bool didMoveOrResizeNotes = (dragMode == PRDragMode::notes && draggingNote != nullptr);

        if (dragMode == PRDragMode::marquee)
        {
            if (canEditMidiClip()
                && marqueeBounds.getWidth() < 4 && marqueeBounds.getHeight() < 4 && ! marqueeAdditive)
                addNoteAt (marqueeAnchor);

            marqueeBounds = {};
            marqueeBaseSelection.clearQuick();
            marqueeAdditive = false;
        }

        draggingNote = nullptr;
        resizing = false;
        dragMode = PRDragMode::none;
        dragNoteStates.clearQuick();
        lastCCPaintBeat = -1.0e9;
        if (didMoveOrResizeNotes)
            notifyMidiContentChanged();
        repaint();
    }

    void updateVelocityAt (int x, int y)
    {
        if (! canEditMidiClip())
            return;

        auto va = velocityArea();
        float vel  = juce::jlimit (0.0f, 1.0f, (float) (va.getBottom() - y) / (float) juce::jmax (1, va.getHeight()));
        int velInt = (int) (vel * 127.0f);

        for (auto* n : midiClip.getSequence().getNotes())
        {
            float nx = beatToX (n->getStartBeat().inBeats());
            if (std::abs (nx - (float)x) < 8.0f)
            {
                n->setVelocity (velInt, &edit.getUndoManager());
                repaint();
                break;
            }
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        // Zoom with Ctrl held (Cmd on macOS). Otherwise sideways for a
        // trackpad's sideways swipe or Shift with a wheel, and up and down.
        if (e.mods.isCommandDown())
        {
            zoomBy (1.0 + w.deltaY * 0.15);
            return;
        }

        const float sideways = w.deltaX + (e.mods.isShiftDown() ? w.deltaY : 0.0f);
        const float vertical = e.mods.isShiftDown() ? 0.0f : w.deltaY;

        viewBeat = juce::jmax (0.0, viewBeat - sideways * 2.0);
        scrollY = juce::jlimit (0, juce::jmax (0, 128 * kRowH - gridArea().getHeight()),
                                scrollY - (int) (vertical * 40.0f));
        updateScrollRanges();
        repaint();
    }

    /** A trackpad pinch zooms in time. */
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override
    {
        zoomBy (scaleFactor);
    }

    void zoomBy (double factor)
    {
        pxPerBeat = juce::jlimit (20.0, 400.0, pxPerBeat * factor);
        zoomSlider.setValue (pxPerBeat, juce::dontSendNotification);
        updateScrollRanges();
        repaint();
    }

    bool   snapEnabled = true;
    double snapInterval = 1.0;

private:
    static constexpr int kKeyW = 44;
    static constexpr int kRowH = 12;
    static constexpr int kToolbarH = 28;  // note-duration dropdown strip
    static constexpr int kHdrH = 24;      // beat ruler height
    static constexpr int kGridTop = kToolbarH + kHdrH;  // = 52, where note grid begins
    static constexpr int kBottomScrollH = 14;
    static constexpr int kSplitterH = 4;

    tracktion::MidiClip& midiClip;
    tracktion::Edit&     edit;
    ProjectData&         projectData;
    AudioEngineManager&  audioEngine;

    juce::TextButton detachBtn { "DETACH" };
    juce::Rectangle<int> prPlayBounds, prStopBounds, prRecBounds;

    juce::ScrollBar hScroll { false }, vScroll { true };
    juce::ComboBox  ccTypeCombo;
    juce::ComboBox  noteDurationCombo;
    juce::Slider    zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    int ccCustomNumber = 17;
    int kCCLaneH = 80;
    int kVelocityLaneH = 80;

    double defaultNoteLength = 1.0;  // beats; 1.0 = quarter note (1/4)

    double viewBeat = 0.0;
    int    scrollY  = 0;
    double pxPerBeat = 80.0;

    double playheadBeat = 0.0;
    int    playheadX    = kKeyW;
    bool   followPlayback       = false;
    bool   followPlayheadInView = true;
    bool   followWasPlaying     = false;
    double followLastSeconds    = 0.0;
    double followViewBeat       = 0.0;

    juce::Rectangle<int> playheadStrip (int x) const
    {
        return { x - 1, kToolbarH, 4, juce::jmax (0, velocityArea().getBottom() - kToolbarH) };
    }

    PRDragMode dragMode = PRDragMode::none;
    tracktion::MidiNote* draggingNote  = nullptr;
    juce::Array<tracktion::MidiNote*> selectedNotes;
    juce::Array<tracktion::MidiNote*> marqueeBaseSelection;

    // Worked out once per paint (prepareSelectionForPaint), so drawing looks
    // up the selection instead of searching it per key row and per note.
    std::unordered_set<tracktion::MidiNote*> paintSelection;
    std::array<bool, 128> highlightedPitches {};
    juce::Rectangle<int> marqueeBounds;
    juce::Point<int> marqueeAnchor;
    bool marqueeAdditive = false;
    int    auditionNote = -1;
    bool resizing = false;
    float  dragBeat0 = 0;
    int    dragNote0 = 0;
    double origStart = 0, origLen = 0;
    int    origNote  = 0;

    juce::Array<DragNoteState> dragNoteStates;

    juce::Array<CopiedNote> noteClipboard;

    int    splitterDragStartY = 0;
    int    splitterStartCcH = 80;
    int    splitterStartVelH = 80;
    double lastCCPaintBeat = -1.0e9;

    int bottomStackH() const noexcept { return kCCLaneH + kSplitterH + kVelocityLaneH + kBottomScrollH; }

    juce::Rectangle<int> gridArea() const
    {
        const int stack = bottomStackH();
        const int h = juce::jmax (40, getHeight() - kGridTop - stack);
        return { kKeyW, kGridTop, getWidth() - kKeyW - 14, h };
    }

    juce::Rectangle<int> ccLaneArea() const
    {
        auto g = gridArea();
        return { g.getX(), g.getBottom(), g.getWidth(), kCCLaneH };
    }

    juce::Rectangle<int> laneSplitterArea() const
    {
        auto c = ccLaneArea();
        return { c.getX(), c.getBottom(), c.getWidth(), kSplitterH };
    }

    juce::Rectangle<int> velocityArea() const
    {
        auto s = laneSplitterArea();
        return { s.getX(), s.getBottom(), s.getWidth(), kVelocityLaneH };
    }

    void syncCCSelectorFromClipState()
    {
        const int v = (int) midiClip.state.getProperty (IDs::pianoRollCC, 1);
        if (v == -1)
            ccTypeCombo.setSelectedId (1, juce::dontSendNotification);
        else if (v == 1)  ccTypeCombo.setSelectedId (2, juce::dontSendNotification);
        else if (v == 7)  ccTypeCombo.setSelectedId (3, juce::dontSendNotification);
        else if (v == 10) ccTypeCombo.setSelectedId (4, juce::dontSendNotification);
        else if (v == 11) ccTypeCombo.setSelectedId (5, juce::dontSendNotification);
        else if (v == 64) ccTypeCombo.setSelectedId (6, juce::dontSendNotification);
        else
        {
            ccCustomNumber = v;
            ccTypeCombo.setSelectedId (7, juce::dontSendNotification);
        }
    }

    int activeControllerType() const noexcept
    {
        const int stored = (int) midiClip.state.getProperty (IDs::pianoRollCC, 1);
        if (stored == -1)
            return tracktion::MidiControllerEvent::pitchWheelType;
        return stored;
    }

    bool isPitchMode() const noexcept { return (int) midiClip.state.getProperty (IDs::pianoRollCC, 1) == -1; }

    float ccValueToY (juce::Rectangle<int> ca, int rawValue) const
    {
        if (isPitchMode())
        {
            const float n = juce::jlimit (0.0f, 1.0f, (float) (rawValue / 16383.0));
            return (float) ca.getBottom() - 2.0f - n * (float) (ca.getHeight() - 4);
        }
        const float n = juce::jlimit (0.0f, 1.0f, (float) rawValue / 127.0f);
        return (float) ca.getBottom() - 2.0f - n * (float) (ca.getHeight() - 4);
    }

    int yToCCValue (juce::Rectangle<int> ca, int y) const
    {
        const float t = juce::jlimit (0.0f, 1.0f,
                                    ((float) ca.getBottom() - (float) y) / (float) juce::jmax (1, ca.getHeight() - 4));
        if (isPitchMode())
            return (int) std::lround (t * 16383.0);
        return (int) std::lround (t * 127.0);
    }

    void applyCCValueAt (int x, int y, bool force)
    {
        if (! canEditMidiClip())
            return;

        auto ca = ccLaneArea();
        const double b = snapBeat ((double) xToBeat (x));
        if (! force && std::abs (b - lastCCPaintBeat) < 1.0e-7)
            return;

        const int v = yToCCValue (ca, y);
        auto& seq = midiClip.getSequence();
        seq.setControllerValueAt (activeControllerType(),
                                  tracktion::BeatPosition::fromBeats (b),
                                  v,
                                  &edit.getUndoManager());
        lastCCPaintBeat = b;
        updateScrollRanges();
        repaint();
    }

    void tryRemoveCCEventAt (const juce::MouseEvent& e)
    {
        if (! canEditMidiClip())
            return;

        auto ca = ccLaneArea();
        const int ctype = activeControllerType();
        tracktion::MidiControllerEvent* hit = nullptr;
        float bestD = 1.0e9f;
        auto& seq = midiClip.getSequence();

        for (auto* ev : seq.getControllerEvents())
        {
            if (ev->getType() != ctype) continue;
            const float px = beatToX (ev->getBeatPosition().inBeats());
            const float py = ccValueToY (ca, ev->getControllerValue());
            const float d = std::hypot (px - (float) e.x, py - (float) e.y);
            if (d < 10.0f && d < bestD)
            {
                bestD = d;
                hit = ev;
            }
        }

        if (hit != nullptr)
        {
            seq.removeControllerEvent (*hit, &edit.getUndoManager());
            repaint();
        }
    }

    void drawCCLane (juce::Graphics& g, juce::Rectangle<int> ca)
    {
        g.setColour (Theme::bgBase.darker (0.08f));
        g.fillRect (ca);
        g.setColour (Theme::border.withAlpha (0.35f));
        g.drawRoundedRectangle (ca.toFloat().reduced (0.5f), 2.0f, 1.0f);

        const int ctype = activeControllerType();
        juce::Array<tracktion::MidiControllerEvent*> evs;
        for (auto* ev : midiClip.getSequence().getControllerEvents())
            if (ev->getType() == ctype)
                evs.add (ev);

        std::sort (evs.begin(), evs.end(),
                   [] (tracktion::MidiControllerEvent* a, tracktion::MidiControllerEvent* b) {
                       return a->getBeatPosition().inBeats() < b->getBeatPosition().inBeats();
                   });

        const int refVal = isPitchMode() ? 8192 : 64;
        g.setColour (Theme::border.withAlpha (0.25f));
        const float refY = ccValueToY (ca, refVal);
        g.drawHorizontalLine ((int) refY, (float) ca.getX(), (float) ca.getRight());

        if (! evs.isEmpty())
        {
            juce::Path path;
            bool first = true;
            float prevY = 0;

            for (auto* ev : evs)
            {
                const float x = beatToX (ev->getBeatPosition().inBeats());
                const float y = ccValueToY (ca, ev->getControllerValue());
                if (x < (float) ca.getX() - 4.0f || x > (float) ca.getRight() + 4.0f)
                    continue;

                if (first)
                {
                    path.startNewSubPath (x, y);
                    first = false;
                    prevY = y;
                }
                else if (isPitchMode())
                {
                    path.lineTo (x, y);
                    prevY = y;
                }
                else
                {
                    path.lineTo (x, prevY);
                    path.lineTo (x, y);
                    prevY = y;
                }
            }

            if (! path.isEmpty())
            {
                g.setColour (Theme::active.withAlpha (0.72f));
                g.strokePath (path, juce::PathStrokeType (1.6f));
            }

            g.setColour (Theme::active.withAlpha (0.95f));
            for (auto* ev : evs)
            {
                const float x = beatToX (ev->getBeatPosition().inBeats());
                const float y = ccValueToY (ca, ev->getControllerValue());
                if (x < (float) ca.getX() - 4.0f || x > (float) ca.getRight() + 4.0f)
                    continue;
                g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
            }
        }

        auto sp = laneSplitterArea();
        g.setColour (Theme::border.withAlpha (0.45f));
        g.fillRect (sp);
        g.setColour (Theme::textMuted.withAlpha (0.35f));
        g.drawHorizontalLine (sp.getCentreY(), (float) sp.getX() + 6.0f, (float) sp.getRight() - 6.0f);
    }

    double visibleBeats() const { return gridArea().getWidth() / pxPerBeat; }

    double clipLengthBeats() const
    {
        auto& ts = edit.tempoSequence;
        auto  t  = midiClip.getPosition().getLength();
        return ts.toBeats (midiClip.getPosition().getStart() + t).inBeats()
             - ts.toBeats (midiClip.getPosition().getStart()).inBeats();
    }

    double contentBeats() const
    {
        double mx = clipLengthBeats();
        for (auto* n : midiClip.getSequence().getNotes())
            mx = juce::jmax (mx, n->getEndBeat().inBeats());

        const int ctype = activeControllerType();
        for (auto* ev : midiClip.getSequence().getControllerEvents())
            if (ev->getType() == ctype)
                mx = juce::jmax (mx, ev->getBeatPosition().inBeats());

        return mx + 4.0;
    }

    float beatToX (double beat) const { return (float)kKeyW + (float)((beat - viewBeat) * pxPerBeat); }
    float xToBeat (int x)       const { return (float)((x - kKeyW) / pxPerBeat + viewBeat); }
    int   noteToY (int note)    const { return kGridTop + (127 - note) * kRowH - scrollY; }
    int   yToNote (int y)       const { return juce::jlimit (0, 127, 127 - (y - kGridTop + scrollY) / kRowH); }
    double snapBeat (double b)  const
    {
        if (! snapEnabled) return b;
        return std::round (b / snapInterval) * snapInterval;
    }

    double snapBeatForInsert (double b) const
    {
        if (! snapEnabled || snapInterval <= 0.0)
            return b;

        constexpr double epsilon = 1.0e-6;
        return std::floor ((b + epsilon) / snapInterval) * snapInterval;
    }

    juce::Rectangle<float> noteRect (tracktion::MidiNote* n, juce::Rectangle<int>) const
    {
        float x = beatToX (n->getStartBeat().inBeats());
        float y = (float) noteToY (n->getNoteNumber());
        float w = juce::jmax (3.0f, (float)(n->getLengthBeats().inBeats() * pxPerBeat) - 1.0f);
        return { x, y + 1.0f, w, (float) kRowH - 2.0f };
    }

    tracktion::MidiNote* getNoteAt (juce::Point<int> p) const
    {
        auto ga = gridArea();
        for (auto* n : midiClip.getSequence().getNotes())
            if (noteRect (n, ga).contains (p.toFloat()))
                return n;

        return nullptr;
    }

    /** The selection without notes that are no longer in the clip (undo,
        delete) or listed twice. */
    juce::Array<tracktion::MidiNote*> getSelectedNotes()
    {
        const auto& notes = midiClip.getSequence().getNotes();
        const std::unordered_set<tracktion::MidiNote*> inClip (notes.begin(), notes.end());
        std::unordered_set<tracktion::MidiNote*> seen;

        juce::Array<tracktion::MidiNote*> valid;
        for (auto* selected : selectedNotes)
            if (selected != nullptr && inClip.count (selected) > 0 && seen.insert (selected).second)
                valid.add (selected);

        selectedNotes = valid;
        return valid;
    }

    void prepareSelectionForPaint()
    {
        paintSelection.clear();
        highlightedPitches.fill (false);

        for (auto* n : getSelectedNotes())
        {
            paintSelection.insert (n);
            highlightedPitches[(size_t) juce::jlimit (0, 127, n->getNoteNumber())] = true;
        }

        if (juce::isPositiveAndBelow (auditionNote, 128))
            highlightedPitches[(size_t) auditionNote] = true;
    }

    bool isSelected (tracktion::MidiNote* note)
    {
        if (note == nullptr)
            return false;

        return getSelectedNotes().contains (note);
    }

    void clearSelection()
    {
        if (selectedNotes.isEmpty())
            return;

        selectedNotes.clearQuick();
        repaint();
    }

    void selectOnly (tracktion::MidiNote* note)
    {
        selectedNotes.clearQuick();
        if (note != nullptr)
            selectedNotes.add (note);
        repaint();
    }

    void addToSelection (tracktion::MidiNote* note)
    {
        if (note != nullptr)
            selectedNotes.addIfNotAlreadyThere (note);
        repaint();
    }

    void toggleSelected (tracktion::MidiNote* note)
    {
        if (note == nullptr)
            return;

        if (selectedNotes.contains (note))
            selectedNotes.removeFirstMatchingValue (note);
        else
            selectedNotes.add (note);

        repaint();
    }

    void selectAllNotes()
    {
        selectedNotes.clearQuick();
        for (auto* n : midiClip.getSequence().getNotes())
            selectedNotes.addIfNotAlreadyThere (n);
        repaint();
    }

    void beginUndo (const juce::String& name)
    {
        edit.getUndoManager().beginNewTransaction (name);
    }

    void setAuditionNote (int note)
    {
        auditionNote = juce::jlimit (0, 127, note);
        repaint();

        juce::Component::SafePointer<PianoRollEditor> safe (this);
        juce::Timer::callAfterDelay (140, [safe, note]
        {
            if (safe != nullptr && safe->auditionNote == note)
            {
                safe->auditionNote = -1;
                safe->repaint();
            }
        });
    }

    tracktion::MidiNote* findRecentlyAddedNote (int note, double start, double length)
    {
        tracktion::MidiNote* best = nullptr;
        double bestDelta = 1.0e9;

        for (auto* n : midiClip.getSequence().getNotes())
        {
            if (n == nullptr || n->getNoteNumber() != note)
                continue;

            const double delta = std::abs (n->getStartBeat().inBeats() - start)
                               + std::abs (n->getLengthBeats().inBeats() - length);
            if (delta < bestDelta)
            {
                best = n;
                bestDelta = delta;
            }
        }

        return best;
    }

    tracktion::MidiNote* addNoteAt (juce::Point<int> pos)
    {
        if (! canEditMidiClip())
            return nullptr;

        if (! gridArea().contains (pos))
            return nullptr;

        const int note = yToNote (pos.y);
        const double snapped = snapBeatForInsert (xToBeat (pos.x));

        beginUndo ("Add MIDI note");
        midiClip.getSequence().addNote (note,
            tracktion::BeatPosition::fromBeats (snapped),
            tracktion::BeatDuration::fromBeats (defaultNoteLength),
            100, 0, &edit.getUndoManager());

        auto* added = findRecentlyAddedNote (note, snapped, defaultNoteLength);
        selectOnly (added);
        setAuditionNote (note);
        updateScrollRanges();
        notifyMidiContentChanged();
        return added;
    }

    void updateMarqueeSelection()
    {
        const auto selectionRect = marqueeBounds.getIntersection (gridArea()).toFloat();
        selectedNotes = marqueeBaseSelection;

        if (selectionRect.isEmpty())
            return;

        auto ga = gridArea();
        for (auto* n : midiClip.getSequence().getNotes())
            if (n != nullptr && noteRect (n, ga).intersects (selectionRect))
                selectedNotes.addIfNotAlreadyThere (n);

        getSelectedNotes();
    }

    void updateScrollRanges()
    {
        double total = contentBeats();
        hScroll.setRangeLimits (0.0, total);
        hScroll.setCurrentRange (viewBeat, visibleBeats(), juce::dontSendNotification);
        int totalH = 128 * kRowH;
        int visH   = gridArea().getHeight();
        vScroll.setRangeLimits (0, totalH);
        vScroll.setCurrentRange (scrollY, visH, juce::dontSendNotification);
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<int> ga)
    {
        // Toolbar background
        g.setColour (Theme::bgPanel.darker (0.05f));
        g.fillRect (0, 0, getWidth(), kToolbarH);
        g.setColour (Theme::border.withAlpha (0.3f));
        g.drawHorizontalLine (kToolbarH, 0.0f, (float) getWidth());

        // Transport pills (drawn after toolbar background)
        const int tBtnX0 = kKeyW + 290;
        const int tBtnY  = (kToolbarH - 20) / 2;
        const int tBtnW  = 38, tBtnH = 20, tGap = 4;
        prStopBounds = { tBtnX0,              tBtnY, tBtnW, tBtnH };
        prPlayBounds = { tBtnX0 + tBtnW+tGap, tBtnY, tBtnW, tBtnH };
        prRecBounds  = { tBtnX0 + (tBtnW+tGap)*2, tBtnY, tBtnW, tBtnH };

        bool playing   = audioEngine.isPlaying();
        bool recording = audioEngine.isRecording();

        ::drawPill (g, prStopBounds, "STOP", false,     Theme::active);
        ::drawPill (g, prPlayBounds, "PLAY", playing,   Theme::active);
        ::drawPill (g, prRecBounds,  "REC",  recording, Theme::recordRed);

        // Row backgrounds
        for (int note = 0; note <= 127; ++note)
        {
            int y = noteToY (note);
            if (y + kRowH < ga.getY() || y > ga.getBottom()) continue;
            bool black = isBlackKey (note);
            g.setColour (black ? Theme::bgBase.darker (0.25f) : Theme::bgPanel.withAlpha (0.35f));
            g.fillRect (ga.getX(), y, ga.getWidth(), kRowH - 1);

            if (highlightedPitches[(size_t) note])
            {
                g.setColour (Theme::active.withAlpha (0.10f));
                g.fillRect (ga.getX(), y, ga.getWidth(), kRowH - 1);
            }

            if (note % 12 == 0)
            {
                g.setColour (Theme::border.withAlpha (0.6f));
                g.drawLine ((float) ga.getX(), (float) y, (float) ga.getRight(), (float) y, 1.0f);
            }
        }

        // Ruler + beat lines
        g.setColour (Theme::bgPanel.darker (0.3f));
        g.fillRect (kKeyW, kToolbarH, getWidth() - kKeyW, kHdrH);

        const double firstBeat = std::floor (viewBeat);
        const double lastBeat  = viewBeat + visibleBeats() + 1.0;

        if (snapEnabled && snapInterval > 0.0)
        {
            // Draw the active snap grid separately from beat/bar lines. If a value
            // such as 1/128 is too dense at the current zoom, draw the closest
            // visible multiple and let zoom reveal the exact smaller divisions.
            double visibleSnapStep = snapInterval;
            while (visibleSnapStep * pxPerBeat < 4.0)
                visibleSnapStep *= 2.0;

            const double firstSnap = std::floor (viewBeat / visibleSnapStep) * visibleSnapStep;
            for (double b = firstSnap; b <= lastBeat; b += visibleSnapStep)
            {
                const float x = beatToX (b);
                if (x < ga.getX() || x > ga.getRight())
                    continue;

                const bool isBeat = std::abs (b - std::round (b)) < 0.0001;
                if (isBeat)
                    continue; // Beat lines are drawn stronger below.

                g.setColour (Theme::border.brighter (0.35f)
                                 .withAlpha (visibleSnapStep == snapInterval ? 0.34f : 0.24f));
                g.drawLine (x, (float) kGridTop, x, (float) ga.getBottom(), 1.0f);
            }
        }

        for (double b = std::floor (firstBeat); b <= lastBeat; b += 1.0)
        {
            float x = beatToX (b);
            if (x < ga.getX() || x > ga.getRight()) continue;

            int beatsPerBar = edit.tempoSequence.getTimeSigAt(tracktion::BeatPosition::fromBeats(b)).numerator;
            bool isBar = (std::fmod (b, (double) beatsPerBar) < 0.001);
            bool isBeat = (std::fmod (b, 1.0) < 0.001);
            g.setColour (isBar ? Theme::border.brighter (0.2f)
                               : isBeat ? Theme::border : Theme::border.withAlpha (0.2f));
            g.drawLine (x, (float) kGridTop, x, (float) ga.getBottom(), 1.0f);
            if (isBeat)
            {
                g.setColour (isBar ? Theme::textMain : Theme::textMuted);
                g.setFont (isBar ? Theme::uiSize (10.0f).boldened() : Theme::uiSize (9.0f));
                int barNum = (int) std::floor (b / beatsPerBar) + 1;
                int beatInBar = ((int) std::round (b)) % beatsPerBar + 1;
                juce::String label = juce::String (barNum) + "." + juce::String (beatInBar);
                g.drawText (label, (int) x + 3, kToolbarH + 5, 30, 14, juce::Justification::left);
            }
        }
        g.setColour (Theme::border);
        g.drawLine ((float) kKeyW, (float) kGridTop, (float) kKeyW, (float) ga.getBottom());
    }

    void drawVelocityLane (juce::Graphics& g, juce::Rectangle<int> va)
    {
        g.setColour (Theme::bgBase.darker (0.1f));
        g.fillRect (va);
        g.setColour (Theme::border);
        g.drawHorizontalLine (va.getY(), (float) va.getX(), (float) va.getRight());
        
        g.setColour (Theme::active.withAlpha (0.6f));
        for (auto* n : midiClip.getSequence().getNotes())
        {
            float x = beatToX (n->getStartBeat().inBeats());
            if (x < (float) va.getX() || x > (float) va.getRight()) continue;
            
            float h = (n->getVelocity() / 127.0f) * (float) va.getHeight();
            g.fillRect (x - 2.0f, (float) va.getBottom() - h, 4.0f, h);
            g.fillEllipse (x - 3.0f, (float) va.getBottom() - h - 3.0f, 6.0f, 6.0f);
        }
    }

    void drawNotes (juce::Graphics& g, juce::Rectangle<int> ga)
    {
        for (auto* n : midiClip.getSequence().getNotes())
        {
            auto r = noteRect (n, ga);
            if (r.getRight() < (float) ga.getX() || r.getX() > (float) ga.getRight()) continue;
            if (r.getBottom() < (float) ga.getY() || r.getY() > (float) ga.getBottom()) continue;
            const bool selected = paintSelection.count (n) > 0;
            auto col = selected ? Theme::active.brighter (0.45f)
                                : (n == draggingNote) ? Theme::active.brighter (0.3f) : Theme::active;
            g.setColour (col);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (selected ? juce::Colours::white.withAlpha (0.85f) : col.darker (0.4f));
            g.drawRoundedRectangle (r, 2.0f, selected ? 1.6f : 1.0f);
            if (r.getWidth() > 18.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.setFont (Theme::uiSize (8.5f));
                g.drawText (noteName (n->getNoteNumber()), r.getX() + 3, r.getY(), (int) r.getWidth(), kRowH, juce::Justification::centredLeft);
            }
        }

        if (dragMode == PRDragMode::marquee && ! marqueeBounds.isEmpty())
        {
            auto r = marqueeBounds.getIntersection (ga).toFloat();
            g.setColour (Theme::active.withAlpha (0.12f));
            g.fillRect (r);
            g.setColour (Theme::active.withAlpha (0.75f));
            g.drawRect (r, 1.0f);
        }
    }

    void drawPianoKeys (juce::Graphics& g)
    {
        g.setColour (Theme::surface);
        g.fillRect (0, kGridTop, kKeyW, getHeight() - kGridTop);

        for (int note = 0; note <= 127; ++note)
        {
            int y = noteToY (note);
            if (y + kRowH < 0 || y > getHeight()) continue;
            bool black = isBlackKey (note);
            const bool selectedPitch = highlightedPitches[(size_t) note];

            if (!black)
            {
                g.setColour (selectedPitch ? Theme::active : juce::Colours::white.withAlpha (0.88f));
                g.fillRect (1, y + 1, kKeyW - 3, kRowH - 2);
                g.setColour (Theme::border.withAlpha (0.4f));
                g.drawRect (1, y + 1, kKeyW - 3, kRowH - 2);
                if (note % 12 == 0)
                {
                    g.setColour (selectedPitch ? juce::Colours::black : Theme::bgBase.withAlpha (0.75f));
                    g.setFont (Theme::uiSize (9.0f));
                    g.drawText ("C" + juce::String (note / 12 - 1), 3, y + 2, kKeyW - 8, kRowH - 4, juce::Justification::left);
                }
            }
            else
            {
                g.setColour (selectedPitch ? Theme::active : juce::Colour (0xff1a1a1a));
                g.fillRect (1, y + 1, kKeyW * 2 / 3, kRowH - 1);
            }
        }
        g.setColour (Theme::border);
        g.drawLine ((float) kKeyW, (float) kGridTop, (float) kKeyW, (float) getHeight(), 1.5f);
    }

    static bool isBlackKey (int note) noexcept
    {
        int n = note % 12;
        return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
    }

    static juce::String noteName (int note)
    {
        const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollEditor)
};

class PianoRollWindow : public juce::DocumentWindow,
                        private juce::ValueTree::Listener
{
public:
    PianoRollWindow (tracktion::MidiClip& clip, tracktion::Edit& edit,
                     ProjectData& pd, AudioEngineManager& ae)
        : DocumentWindow (clip.getName() + "   -   Piano Roll",
                          Theme::bgBase, DocumentWindow::allButtons, true),
          clipState (clip.state), audioEngine (ae)
    {
        clipState.addListener (this);
        editor.reset (new PianoRollEditor (clip, edit, pd, ae));
        followPlayback = isFollowPlaybackOn (ae);
        editor->setFollowPlayback (followPlayback);
        setUsingNativeTitleBar (false);
        setResizable (true, false);
        setContentNonOwned (editor.get(), true);
        centreWithSize (960, 560);
        setVisible (true);
        editor->grabKeyboardFocus();
    }

    ~PianoRollWindow() override
    {
        clipState.removeListener (this);
    }

    void closeButtonPressed() override { delete this; }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override {}
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}

    void valueTreeParentChanged (juce::ValueTree& tree) override
    {
        if (closing || tree != clipState || clipState.getParent().isValid())
            return;

        closing = true;

        // Release the editor while Tracktion's MidiClip is still alive. Once the
        // state-removal callback returns, the editor's MidiClip reference may dangle.
        clearContentComponent();
        editor.reset();
        setVisible (false);

        juce::Component::SafePointer<PianoRollWindow> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                delete safe.getComponent();
        });
    }

    void onDisplayFrame()
    {
        if (editor == nullptr || ! isShowing())
            return;

        const bool follow = isFollowPlaybackOn (audioEngine);
        if (follow != followPlayback)
        {
            followPlayback = follow;
            editor->setFollowPlayback (follow);
        }

        editor->updatePlayhead (audioEngine.getTransportPosition(), audioEngine.isPlaying());
    }

    juce::ValueTree clipState;
    AudioEngineManager& audioEngine;
    std::unique_ptr<PianoRollEditor> editor;
    bool closing = false;
    bool followPlayback = true;
    juce::VBlankAttachment displayClock { this, [this] (double) { onDisplayFrame(); } };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollWindow)
};
