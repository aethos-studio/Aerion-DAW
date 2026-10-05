#pragma once

// Arranger Timeline and its playhead overlay.

#include "ViewShared.h"
#include "PluginManagerWindow.h"
#include "PianoRoll.h"

//==============================================================================
class Timeline : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 public juce::DragAndDropTarget,
                 public juce::ScrollBar::Listener,
                 public juce::ValueTree::Listener,
                 public AudioEngineManager::Listener,
                 private juce::AsyncUpdater
{
public:
    enum class DragMode : int;

    static constexpr int kHeaderWidth = 250;
    // How far a clip's drawing reaches past its body: drop shadow, 2 px
    // selection outline, anti-aliasing.
    static constexpr float kClipPaintMargin = 3.0f;
    static constexpr int kRulerSigH        = 18;  // time signature flags
    static constexpr int kRulerBeatH       = 24;  // bar.beat labels + ticks
    static constexpr int kRulerBotH        = 24;  // tempo lane + clock-time labels
    static constexpr int kRulerLaneLabelW  = 72; // left gutter for SIG / TEMPO lane labels
    static constexpr int kRulerTopH        = kRulerSigH + kRulerBeatH;
    static constexpr int kRulerH           = kRulerTopH + kRulerBotH;
    static constexpr int kRulerContentX    = kHeaderWidth + kRulerLaneLabelW;
    static constexpr int kHeaderBarH  = 32;
    static constexpr int kTrackH      = 80;    // default lane height; the full header fits
    static constexpr int kMinTrackH   = 56;    // name and M / S / R / A still fit, FX is hidden
    static constexpr int kMaxTrackH   = 400;
    static constexpr int kAutoLaneH   = 80;    // automation lane below a track
    static constexpr int kResizeGrabH = 4;     // grab zone around a lane's bottom edge, each side

    struct TrackHeightPreset { const char* name; int height; };
    static constexpr TrackHeightPreset kTrackHeightPresets[] = {
        { "Small", kMinTrackH }, { "Normal", kTrackH }, { "Large", 140 }, { "Huge", 240 } };
    static constexpr int kNumTrackHeightPresets = (int) std::size (kTrackHeightPresets);
    static constexpr int kFooterH     = 28;
    static constexpr int kVScrollW    = 12;

    std::function<void(tracktion::Track*)> onTrackSelected;
    std::function<void(juce::Array<tracktion::Track*>)> onSelectionChanged;
    std::function<void()> onAddTrack;
    std::function<void()> onAddMidiTrack;
    std::function<void()> onAddFolder;
    std::function<void(const juce::File&)> onImportFile; // legacy single-file path (menu import)
    // Position-aware drop: files + target track (nullptr = create new) + time position
    std::function<void(const juce::Array<juce::File>&,
                       tracktion::AudioTrack*,
                       double)> onImportFiles;
    // Plugin dropped on a track header from the Browser Plugins tab
    std::function<void(tracktion::Track*, const juce::PluginDescription&)> onPluginDroppedOnTrack;
    // MIDI clip double-clicked in timeline (MainComponent uses this for embedded Piano Roll)
    std::function<void(tracktion::MidiClip&)> onMidiClipDoubleClicked;

    Timeline(AudioEngineManager& ae, ProjectData& pd) : audioEngine(ae), projectData(pd)
    {
        // Rendered into a cached layer: paint() runs only for areas this component
        // invalidates. The playhead lives on TimelinePlayheadOverlay above it, so
        // moving it redraws from the cache. paint() fills every pixel, hence opaque.
        setOpaque (true);
        auto* layer = new CachedLayer (*this);
        layer->setUnderlay (Theme::backgroundMid());
        setCachedComponentImage (layer);

        projectData.getProjectTree().addListener (this);
        audioEngine.addListener (this);
        snapEnabled = projectData.getProjectTree().getProperty (IDs::snapEnabled);
        snapInterval = projectData.getProjectTree().getProperty (IDs::snapInterval);

        addAndMakeVisible (horizontalScrollBar);
        horizontalScrollBar.setRangeLimits (0.0, 3600.0); // 1 hour max
        horizontalScrollBar.addListener (this);

        addAndMakeVisible (verticalScrollBar);
        verticalScrollBar.addListener (this);

        addAndMakeVisible (zoomSlider);
        zoomSlider.setRange (1.0, 2000.0, 1.0);
        zoomSlider.setValue (pxPerSec);
        zoomSlider.onValueChange = [this] {
            const double start = getStartTime();
            pxPerSec = zoomSlider.getValue();
            scrollPx = pixelsForStartTime (start);
            repaint();
            updateScrollBar();
        };

        addChildComponent (trackNameEditor);
        trackNameEditor.setColour (juce::TextEditor::backgroundColourId, Theme::surface);
        trackNameEditor.setColour (juce::TextEditor::outlineColourId, Theme::active);
        trackNameEditor.setColour (juce::TextEditor::textColourId, Theme::textMain);
        trackNameEditor.onReturnKey = [this] { commitTrackRename(); };
        trackNameEditor.onFocusLost = [this] { commitTrackRename(); };
        trackNameEditor.onEscapeKey = [this] { trackNameEditor.setVisible (false); };

        addChildComponent (rulerValueEditor);
        rulerValueEditor.setColour (juce::TextEditor::backgroundColourId, Theme::surface);
        rulerValueEditor.setColour (juce::TextEditor::outlineColourId, Theme::active);
        rulerValueEditor.setColour (juce::TextEditor::textColourId, Theme::textMain);
        rulerValueEditor.setJustification (juce::Justification::centred);
        rulerValueEditor.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
        rulerValueEditor.onReturnKey = [this] { commitRulerValueEdit(); };
        rulerValueEditor.onFocusLost = [this] { commitRulerValueEdit(); };
        rulerValueEditor.onEscapeKey = [this] { cancelRulerValueEdit(); };

        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    ~Timeline() override
    {
        cancelPendingUpdate();
        if (selectedClipState.isValid())
            selectedClipState.removeListener (this);
        projectData.getProjectTree().removeListener (this);
        audioEngine.removeListener (this);
    }

    void setSelectedClip (tracktion::Clip* clip)
    {
        if (selectedClipState.isValid())
            selectedClipState.removeListener (this);

        selectedClip = clip;
        selectedClipState = (clip != nullptr ? clip->state : juce::ValueTree());
        selectedClipRebindPending = false;

        if (selectedClipState.isValid())
            selectedClipState.addListener (this);
    }

    void clearSelectedClip()
    {
        setSelectedClip (nullptr);
        dragMode = DragMode::none;
    }

    tracktion::Clip* findClipWithState (const juce::ValueTree& state) const
    {
        if (! state.isValid())
            return nullptr;

        for (auto* t : tracktion::getAllTracks (audioEngine.getEdit()))
            if (auto* a = dynamic_cast<tracktion::AudioTrack*> (t))
                for (auto* c : a->getClips())
                    if (c != nullptr && c->state == state)
                        return c;

        return nullptr;
    }

    /** Scroll the timeline so bar 1 aligns with the start of the content area. */
    void scrollViewToBarOne()
    {
        scrollTo (0.0, scrollY);
        updateScrollBar();
    }

    juce::MouseCursor getMouseCursor() override
    {
        if (hoveredTempoNodeIndex >= 0)
            return juce::MouseCursor::UpDownLeftRightResizeCursor;
        if (resizeTrack != nullptr || hoverResizeTrack != nullptr)
            return juce::MouseCursor::UpDownResizeCursor;
        if (activeTool == EditTool::razor)
            return juce::MouseCursor::CrosshairCursor;
        if (hoverDragMode == DragMode::trimLeft || hoverDragMode == DragMode::trimRight)
            return juce::MouseCursor::LeftRightResizeCursor;
        if (hoverDragMode == DragMode::fadeLeft || hoverDragMode == DragMode::fadeRight)
            return juce::MouseCursor::LeftRightResizeCursor;
        if (hoverDragMode == DragMode::move)
            return juce::MouseCursor::DraggingHandCursor;

        return juce::MouseCursor::NormalCursor;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        // Tempo-node hover feedback (resize cursor + node highlight).
        {
            const int th = getTempoNodeAt (e.getPosition());
            if (th != hoveredTempoNodeIndex)
            {
                hoveredTempoNodeIndex = th;
                setMouseCursor (getMouseCursor());
                repaint (tempoLaneArea());
            }
        }

        // A lane's bottom edge in the header column resizes the track.
        if (auto* edgeTrack = getLaneEdgeTrackAt (e.getPosition()); edgeTrack != hoverResizeTrack)
        {
            hoverResizeTrack = edgeTrack;
            setMouseCursor (getMouseCursor());
        }

        if (activeTool == EditTool::razor)
        {
            int oldX = lastMouseX;
            lastMouseX = e.x;
            // Only invalidate the 3px strips where the hairline was and where it's going
            repaint (juce::jmax (kHeaderWidth, oldX - 1), 0, 3, getHeight());
            repaint (juce::jmax (kHeaderWidth, e.x  - 1), 0, 3, getHeight());
        }
        else if (activeTool == EditTool::select)
        {
            lastMouseX = e.x;

            auto newHover = DragMode::none;
            if (e.x >= kHeaderWidth && e.y >= kRulerH && e.y < laneBottom() && e.x < getWidth() - kVScrollW)
            {
                if (auto* clip = getClipAt (e.getPosition()))
                    if (! isClipTrackFrozenOrFreezing (audioEngine, clip))
                        newHover = getSmartToolDragModeFor (*clip, e.getPosition());
            }

            if (newHover != hoverDragMode)
            {
                hoverDragMode = newHover;
                setMouseCursor (getMouseCursor());
            }
        }
        else
        {
            lastMouseX = e.x;
            if (hoverDragMode != DragMode::none)
            {
                hoverDragMode = DragMode::none;
                setMouseCursor (getMouseCursor());
            }
        }
    }

    void commitTrackRename()
    {
        if (trackBeingRenamed != nullptr)
        {
            trackBeingRenamed->setName (trackNameEditor.getText());
            if (onSelectionChanged) onSelectionChanged (getSelectedTracks());
            trackBeingRenamed = nullptr;
        }
        trackNameEditor.setVisible (false);
        repaint();
    }

    void commitRulerValueEdit()
    {
        if (! rulerValueEditor.isVisible())
            return;

        if (editingTempoIndex >= 0)
        {
            const double bpm = rulerValueEditor.getText().getDoubleValue();
            if (bpm >= 20.0 && bpm <= 300.0)
                audioEngine.setTempoBpmAtIndex (editingTempoIndex, bpm);
        }
        else if (editingTimeSigIndex >= 0)
        {
            const juce::String sig = rulerValueEditor.getText().trim();
            const int numerator = sig.upToFirstOccurrenceOf ("/", false, false).getIntValue();
            const int denominator = sig.fromFirstOccurrenceOf ("/", false, false).getIntValue();
            if (numerator > 0 && denominator > 0)
            {
                const double beat = audioEngine.getTimeSigStartBeat (editingTimeSigIndex);
                audioEngine.setTimeSigAtBeat (beat, numerator, denominator);
            }
        }

        cancelRulerValueEdit();
        repaint();
    }

    void cancelRulerValueEdit()
    {
        rulerValueEditor.setVisible (false);
        editingTempoIndex = -1;
        editingTimeSigIndex = -1;
    }

    void beginTempoBpmEdit (int index, juce::Rectangle<int> bounds)
    {
        if (index < 0 || index >= audioEngine.getNumTempos())
            return;

        trackNameEditor.setVisible (false);
        editingTempoIndex = index;
        editingTimeSigIndex = -1;
        rulerValueEditor.setInputRestrictions (0, "0123456789.");
        rulerValueEditor.setText (juce::String (audioEngine.getTempoBpm (index), 0), false);
        rulerValueEditor.setBounds (bounds.expanded (2, 1));
        rulerValueEditor.setVisible (true);
        rulerValueEditor.grabKeyboardFocus();
        rulerValueEditor.selectAll();
    }

    void beginTimeSigEdit (int index, juce::Rectangle<int> bounds)
    {
        if (index < 0 || index >= audioEngine.getNumTimeSigs())
            return;

        trackNameEditor.setVisible (false);
        editingTimeSigIndex = index;
        editingTempoIndex = -1;
        rulerValueEditor.setInputRestrictions (0, "0123456789/");
        rulerValueEditor.setText (audioEngine.getTimeSigAtIndex (index), false);
        rulerValueEditor.setBounds (bounds.expanded (2, 1));
        rulerValueEditor.setVisible (true);
        rulerValueEditor.grabKeyboardFocus();
        rulerValueEditor.selectAll();
    }

    enum class DragMode : int { none, move, trimLeft, trimRight, fadeLeft, fadeRight, loopStart, loopEnd, marker, tempoNode, timeSigNode };
    DragMode dragMode = DragMode::none;
    DragMode hoverDragMode = DragMode::none;

    // Track resize by dragging a lane's bottom edge in the header column.
    // hoverResizeTrack is only compared, never dereferenced.
    tracktion::Track* hoverResizeTrack = nullptr;
    tracktion::Track* resizeTrack = nullptr;
    juce::Array<tracktion::Track*> resizeGroup;
    int resizeStartY = 0, resizeStartH = kTrackH;
    tracktion::MarkerClip* draggingMarker = nullptr;
    double dragOffset = 0;
    double dragStartVal = 0;
    int selectedTempoIndex = -1;
    int selectedTimeSigIndex = -1;
    int editingTempoIndex = -1;
    int editingTimeSigIndex = -1;
    double tempoDragStartBpm = 120.0;
    int tempoDragStartY = 0;
    int hoveredTempoNodeIndex = -1;

    DragMode getSmartToolDragModeFor (tracktion::Clip& clip, juce::Point<int> pos) const
    {
        const float edgeThreshold = 8.0f;
        const float hSize = 6.0f;

        const float clipX = timeToX ((float) clip.getPosition().getStart().inSeconds());
        const float clipW = (float) clip.getPosition().getLength().inSeconds() * pxPerSec;

        int rowTopInComp = 0;
        for (auto& row : const_cast<Timeline*> (this)->getVisibleRows())
            if (row.track == clip.getTrack())
                { rowTopInComp = kRulerH + row.y - scrollY; break; }

        const float clipTopY = (float) rowTopInComp + 2.0f;
        const bool onFadeInDot  = (pos.x >= clipX                && pos.x <= clipX + hSize
                                   && pos.y >= clipTopY          && pos.y <= clipTopY + hSize);
        const bool onFadeOutDot = (pos.x >= clipX + clipW - hSize && pos.x <= clipX + clipW
                                   && pos.y >= clipTopY          && pos.y <= clipTopY + hSize);

        if (dynamic_cast<tracktion::WaveAudioClip*> (&clip) != nullptr)
        {
            if (onFadeInDot)  return DragMode::fadeLeft;
            if (onFadeOutDot) return DragMode::fadeRight;
        }

        if (pos.x >= clipX && pos.x <= clipX + edgeThreshold)
            return DragMode::trimLeft;
        if (pos.x >= clipX + clipW - edgeThreshold && pos.x <= clipX + clipW)
            return DragMode::trimRight;
        return DragMode::move;
    }

    // When two wave events overlap, auto-create a crossfade by setting
    // the left event's fade-out and right event's fade-in to the overlap duration.
    static bool shouldOverwriteFadeIn (tracktion::WaveAudioClip& clip)
    {
        // Never clobber a user fade. Only overwrite if this fade was auto-set.
        return (bool) clip.state.getProperty ("aerionAutoFadeIn", false);
    }

    static bool shouldOverwriteFadeOut (tracktion::WaveAudioClip& clip)
    {
        return (bool) clip.state.getProperty ("aerionAutoFadeOut", false);
    }

    void applyAutoCrossfadesForTrack (tracktion::Track& track)
    {
        auto* audio = dynamic_cast<tracktion::AudioTrack*> (&track);
        if (audio == nullptr)
            return;

        if (audioEngine.isTrackFrozen (audio) || audioEngine.isTrackFreezing (audio))
            return;

        juce::Array<tracktion::WaveAudioClip*> waves;
        for (auto* c : audio->getClips())
            if (auto* w = dynamic_cast<tracktion::WaveAudioClip*> (c))
                waves.add (w);

        if (waves.size() < 2)
            return;

        std::sort (waves.begin(), waves.end(),
                   [] (auto* a, auto* b)
                   {
                       return a->getPosition().getStart().inSeconds() < b->getPosition().getStart().inSeconds();
                   });

        const int maxMs = (int) projectData.getProjectTree().getProperty (IDs::autoCrossfadeMaxMs, 120);
        const double maxFadeSecs = juce::jlimit (0.0, 5.0, (double) maxMs / 1000.0);

        for (int i = 0; i < waves.size() - 1; ++i)
        {
            auto* left  = waves[i];
            auto* right = waves[i + 1];
            if (left == nullptr || right == nullptr)
                continue;

            const double leftStart  = left->getPosition().getStart().inSeconds();
            const double leftEnd    = left->getPosition().getEnd().inSeconds();
            const double rightStart = right->getPosition().getStart().inSeconds();
            const double rightEnd   = right->getPosition().getEnd().inSeconds();

            const double overlap = leftEnd - rightStart;
            if (overlap <= 0.0)
                continue;

            // Clamp overlap to both clip lengths so fades never exceed content.
            const double leftLen  = juce::jmax (0.01, leftEnd - leftStart);
            const double rightLen = juce::jmax (0.01, rightEnd - rightStart);
            const double fadeSecs = juce::jmin (overlap, juce::jmin (leftLen, rightLen));
            const double clamped  = juce::jmin (fadeSecs, maxFadeSecs);
            if (clamped <= 0.0)
                continue;

            // Fade-out on left
            const double leftExisting = left->getFadeOut().inSeconds();
            if (leftExisting <= 0.0 || shouldOverwriteFadeOut (*left))
            {
                left->setFadeOut (tracktion::TimeDuration::fromSeconds (clamped));
                left->state.setProperty ("aerionAutoFadeOut", true, nullptr);
            }

            // Fade-in on right
            const double rightExisting = right->getFadeIn().inSeconds();
            if (rightExisting <= 0.0 || shouldOverwriteFadeIn (*right))
            {
                right->setFadeIn (tracktion::TimeDuration::fromSeconds (clamped));
                right->state.setProperty ("aerionAutoFadeIn", true, nullptr);
            }
        }
    }

    // -- FileDragAndDropTarget ------------------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }

    void fileDragEnter (const juce::StringArray& files, int x, int y) override
    {
        fileDragFiles  = files;
        fileDragActive = true;
        updateFileDragState (x, y);
        repaint();
    }

    void fileDragMove (const juce::StringArray& files, int x, int y) override
    {
        fileDragFiles = files;
        updateFileDragState (x, y);
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        fileDragActive       = false;
        fileDragTargetRowIdx = -1;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        fileDragActive = false;
        updateFileDragState (x, y);

        juce::Array<juce::File> fileArray;
        for (auto& s : files)
            fileArray.add (juce::File (s));

        tracktion::AudioTrack* targetTrack = nullptr;
        if (fileDragTargetRowIdx >= 0)
        {
            auto rows = getVisibleRows();
            if (fileDragTargetRowIdx < rows.size())
                targetTrack = dynamic_cast<tracktion::AudioTrack*> (rows[fileDragTargetRowIdx].track);
        }

        if (onImportFiles)
            onImportFiles (fileArray, targetTrack, fileDragSnappedTime);
        else if (onImportFile)               // fallback for legacy callers
            for (auto& f : fileArray)
                onImportFile (f);

        fileDragTargetRowIdx = -1;
        repaint();
    }

    // -- DragAndDropTarget (plugin drag from Browser) -------------------------
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        return d.description.toString().startsWith ("PLUGIN:");
    }

    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        pluginDragActive = true;
        updatePluginDragTarget (d.localPosition.x, d.localPosition.y);
        repaint();
    }

    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        updatePluginDragTarget (d.localPosition.x, d.localPosition.y);
        repaint();
    }

    void itemDragExit (const juce::DragAndDropTarget::SourceDetails&) override
    {
        pluginDragActive    = false;
        pluginDragTargetRow = -1;
        repaint();
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        pluginDragActive = false;
        updatePluginDragTarget (d.localPosition.x, d.localPosition.y);

        if (pluginDragTargetRow >= 0 && onPluginDroppedOnTrack)
        {
            auto rows = getVisibleRows();
            if (pluginDragTargetRow < rows.size())
            {
                auto* track = rows[pluginDragTargetRow].track;
                juce::String idStr = d.description.toString()
                                         .fromFirstOccurrenceOf ("PLUGIN:", false, false);
                auto& known = audioEngine.getEngine().getPluginManager().knownPluginList;
                for (auto& t : known.getTypes())
                {
                    if (t.createIdentifierString() == idStr)
                    {
                        onPluginDroppedOnTrack (track, t);
                        break;
                    }
                }
            }
        }

        pluginDragTargetRow = -1;
        repaint();
    }

    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override
    {
        if (bar == &horizontalScrollBar)     scrollTo (pixelsForStartTime (newRangeStart), scrollY);
        else if (bar == &verticalScrollBar)  scrollTo (scrollPx, (int) newRangeStart);
    }

    void repaintRecordingRows()
    {
        bool repaintedAny = false;
        auto rows = getVisibleRows();
        for (auto& row : rows)
        {
            if (row.track != nullptr && audioEngine.isTrackArmed (row.track))
            {
                repaint (0, kRulerH + row.y - scrollY, getWidth(), row.height);
                repaintedAny = true;
            }
        }

        if (! repaintedAny)
            requestTimelineRefresh (false);
    }

    int contentHeight() const
    {
        int totalH = 0;
        auto top = audioEngine.getTopLevelTracks();
        
        std::function<void(tracktion::Track*)> addHeight = [&](tracktion::Track* t) {
            totalH += getTrackHeight (t);
            if (auto* f = dynamic_cast<tracktion::FolderTrack*>(t))
                if (! collapsedFolders.contains (f->itemID.toString()))
                    for (auto* child : f->getAllAudioSubTracks(false))
                        addHeight (child);
        };

        for (auto* t : top)
            addHeight (t);

        return totalH;
    }

    int laneTop()    const { return kRulerH; }
    int laneBottom() const { return getHeight() - kFooterH; }

    juce::Rectangle<int> tempoLaneArea() const
    {
        return { kHeaderWidth, kRulerTopH, juce::jmax (0, getWidth() - kHeaderWidth - kVScrollW), kRulerBotH };
    }

    juce::Rectangle<int> beatRulerArea() const
    {
        return { kHeaderWidth, kRulerSigH, juce::jmax (0, getWidth() - kHeaderWidth - kVScrollW), kRulerBeatH };
    }

    juce::Rectangle<int> timeSigLaneArea() const
    {
        return { kHeaderWidth, 0, juce::jmax (0, getWidth() - kHeaderWidth - kVScrollW), kRulerSigH };
    }

    int getTimeSigNodeAt (juce::Point<int> p) const
    {
        if (! timeSigLaneArea().contains (p))
            return -1;

        if (audioEngine.getNumTimeSigs() > 0
            && audioEngine.getTimeSigStartBeat (0) <= 0.0001)
        {
            auto rootArea = timeSigLaneArea().withWidth (kRulerLaneLabelW).withTrimmedLeft (20);
            if (rootArea.contains (p))
                return 0;
        }

        for (int i = audioEngine.getNumTimeSigs(); --i >= 0;)
        {
            if (i == 0 && audioEngine.getTimeSigStartBeat (0) <= 0.0001)
                continue;

            const double beat = audioEngine.getTimeSigStartBeat (i);
            const float x = timeToX (audioEngine.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds());
            if (std::abs ((float) p.x - x) <= 8.0f)
                return i;
        }

        return -1;
    }

    double getBarStartBeatForX (float x) const
    {
        auto& ts = audioEngine.getEdit().tempoSequence;
        const double time = juce::jmax (0.0, xToTime (x));
        auto barsAndBeats = ts.toBarsAndBeats (tracktion::TimePosition::fromSeconds (time));
        barsAndBeats.beats = tracktion::BeatDuration();
        return ts.toBeats (ts.toTime (barsAndBeats)).inBeats();
    }

    void showTimeSigMenuAt (double beat, int existingIndex = -1)
    {
        const int kBase = 100;
        const char* presets[] = { "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8" };
        constexpr int kPresetCount = 8;
        juce::PopupMenu menu;
        const auto current = existingIndex >= 0 ? audioEngine.getTimeSigAtIndex (existingIndex) : juce::String();

        for (int i = 0; i < kPresetCount; ++i)
            menu.addItem (kBase + i, presets[i], true, current == presets[i]);

        if (existingIndex > 0)
        {
            menu.addSeparator();
            menu.addItem (1, "Remove time signature change");
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this, beat, existingIndex](int result)
            {
                if (result == 1 && existingIndex > 0)
                {
                    audioEngine.removeTimeSigAtIndex (existingIndex);
                    selectedTimeSigIndex = -1;
                    repaint();
                    return;
                }

                if (result >= kBase)
                {
                    const char* presets[] = { "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8" };
                    constexpr int kPresetCount = 8;
                    const int idx = result - kBase;
                    if (idx >= 0 && idx < kPresetCount)
                    {
                        const juce::String sig (presets[idx]);
                        const int numerator = sig.upToFirstOccurrenceOf ("/", false, false).getIntValue();
                        const int denominator = sig.fromFirstOccurrenceOf ("/", false, false).getIntValue();
                        audioEngine.setTimeSigAtBeat (beat, numerator, denominator);
                        selectedTimeSigIndex = audioEngine.getEdit().tempoSequence.indexOfTimeSig (
                            &audioEngine.getEdit().tempoSequence.getTimeSigAt (tracktion::BeatPosition::fromBeats (beat)));
                        repaint();
                    }
                }
            });
    }

    float tempoBpmToY (double bpm) const
    {
        auto area = tempoLaneArea().reduced (0, 3);
        const double norm = juce::jmap (juce::jlimit (20.0, 300.0, bpm), 20.0, 300.0, 1.0, 0.0);
        return area.getY() + (float) norm * area.getHeight();
    }

    int getTempoNodeAt (juce::Point<int> p) const
    {
        if (! tempoLaneArea().contains (p))
            return -1;

        for (int i = audioEngine.getNumTempos(); --i >= 0;)
        {
            const double beat = audioEngine.getTempoStartBeat (i);
            if (i == 0 && beat <= 0.0001)
            {
                if (tempoLaneArea().withWidth (kRulerLaneLabelW).contains (p))
                    return 0;
                continue;
            }

            const float x = timeToX (audioEngine.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds());
            const float y = tempoBpmToY (audioEngine.getTempoBpm (i));
            if (std::abs ((float) p.x - x) <= 7.0f && std::abs ((float) p.y - y) <= 7.0f)
                return i;
        }

        return -1;
    }

    juce::Rectangle<int> getTempoBpmLabelBounds (int index) const
    {
        return getTempoBpmEditBounds (index);
    }

    juce::Rectangle<int> getTempoBpmEditBounds (int index) const
    {
        if (index < 0 || index >= audioEngine.getNumTempos())
            return {};

        const auto area = tempoLaneArea();
        if (index == 0 && audioEngine.getTempoStartBeat (0) <= 0.0001)
        {
            const auto labelArea = area.withWidth (kRulerLaneLabelW);
            return labelArea.withTrimmedTop (labelArea.getHeight() / 2).reduced (2, 0);
        }

        const double beat = audioEngine.getTempoStartBeat (index);
        const double time = audioEngine.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds();
        const float x = timeToX (time);
        const juce::String bpmText = juce::String (audioEngine.getTempoBpm (index), 0);
        const int pillW = juce::jmax (22, (int) Theme::uiSize (8.0f).getStringWidth (bpmText) + 8);
        return { (int) (x - (float) pillW * 0.5f), area.getY() + 1, pillW, 11 };
    }

    int getTempoBpmLabelAt (juce::Point<int> p) const
    {
        for (int i = 0; i < audioEngine.getNumTempos(); ++i)
            if (getTempoBpmLabelBounds (i).contains (p))
                return i;

        return -1;
    }

    juce::Rectangle<int> getTimeSigLabelBounds (int index) const
    {
        if (index < 0 || index >= audioEngine.getNumTimeSigs())
            return {};

        const auto area = timeSigLaneArea();
        const auto labelArea = area.withWidth (kRulerLaneLabelW);
        const double beat = audioEngine.getTimeSigStartBeat (index);
        const bool isRoot = (index == 0 && beat <= 0.0001);

        if (isRoot)
            return labelArea.withTrimmedLeft (20).reduced (2, 2);

        const double time = audioEngine.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds();
        const float anchorX = timeToX (time);
        if (anchorX < (float) labelArea.getRight() || anchorX > getWidth() - kVScrollW)
            return {};

        return { (int) anchorX + 4, area.getY() + 2, 32, 12 };
    }

    int getTimeSigLabelAt (juce::Point<int> p) const
    {
        for (int i = 0; i < audioEngine.getNumTimeSigs(); ++i)
            if (getTimeSigLabelBounds (i).contains (p))
                return i;

        return -1;
    }

    void drawTempoLane (juce::Graphics& g)
    {
        auto area = tempoLaneArea();
        if (area.isEmpty())
            return;

        auto labelArea   = area.withWidth (kRulerLaneLabelW);
        auto contentArea = area.withTrimmedLeft (kRulerLaneLabelW);

        g.setColour (Theme::surface.withAlpha (0.45f));
        g.fillRect (area);
        g.setColour (Theme::surface.withAlpha (0.92f));
        g.fillRect (labelArea);
        g.setColour (Theme::border.withAlpha (0.35f));
        g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());
        g.setColour (Theme::border.withAlpha (0.22f));
        g.drawVerticalLine (contentArea.getX(), (float) area.getY(), (float) area.getBottom());

        const auto titleArea = labelArea.withHeight (labelArea.getHeight() / 2);
        const auto valueArea = labelArea.withTrimmedTop (labelArea.getHeight() / 2);

        g.setColour (Theme::textMuted.withAlpha (0.65f));
        g.setFont (Theme::uiSize (7.5f).withStyle (juce::Font::bold));
        g.drawText ("TEMPO", titleArea.reduced (4, 0), juce::Justification::centredLeft);

        if (audioEngine.getNumTempos() > 0 && audioEngine.getTempoStartBeat (0) <= 0.0001)
        {
            g.setColour (Theme::textMain.withAlpha (0.88f));
            g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
            g.drawText (juce::String (audioEngine.getTempoBpm (0), 0),
                        valueArea.reduced (4, 0), juce::Justification::centredLeft);
        }

        juce::Path curve;
        bool started = false;
        for (int i = 0; i < audioEngine.getNumTempos(); ++i)
        {
            const double beat = audioEngine.getTempoStartBeat (i);
            if (i == 0 && beat <= 0.0001)
                continue;

            const double time = audioEngine.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds();
            const float x = timeToX (time);
            if (x < (float) contentArea.getX() || x > getWidth() + 20.0f)
                continue;

            const float y = tempoBpmToY (audioEngine.getTempoBpm (i));
            if (! started) { curve.startNewSubPath (x, y); started = true; }
            else           { curve.lineTo (x, y); }
        }

        if (! started && audioEngine.getNumTempos() > 0)
        {
            const float x = (float) contentArea.getX();
            const float y = tempoBpmToY (audioEngine.getTempoBpm (0));
            curve.startNewSubPath (x, y);
            started = true;
        }

        if (! curve.isEmpty())
        {
            g.setColour (Theme::active.withAlpha (0.55f));
            g.strokePath (curve, juce::PathStrokeType (1.25f));
        }

        for (int i = 0; i < audioEngine.getNumTempos(); ++i)
        {
            const double beat = audioEngine.getTempoStartBeat (i);
            if (i == 0 && beat <= 0.0001)
                continue;

            const double time = audioEngine.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds();
            const float x = timeToX (time);
            if (x < (float) contentArea.getX() || x > getWidth() - kVScrollW)
                continue;

            const double bpm = audioEngine.getTempoBpm (i);
            const float y = tempoBpmToY (bpm);
            const bool selected = (i == selectedTempoIndex);
            const bool hovered  = (i == hoveredTempoNodeIndex);

            g.setColour (Theme::border.withAlpha (0.35f));
            g.drawLine (x, (float) area.getY(), x, (float) kRulerTopH, 1.0f);

            const auto c = selected ? Theme::accent
                         : hovered  ? Theme::accent.brighter (0.15f)
                                    : Theme::active;
            const float r = (selected || hovered) ? 5.5f : 4.5f;
            g.setColour (c.withAlpha (0.95f));
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
            g.setColour ((hovered && ! selected) ? Theme::textMain.withAlpha (0.7f)
                                                 : juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.0f);

            const juce::String bpmText = juce::String (bpm, 0);
            const int pillW = juce::jmax (22, (int) Theme::uiSize (8.0f).getStringWidth (bpmText) + 8);
            juce::Rectangle<float> pill (x - (float) pillW * 0.5f, area.getY() + 1.0f, (float) pillW, 11.0f);
            g.setColour (Theme::surface.withAlpha (0.92f));
            g.fillRoundedRectangle (pill, 2.0f);
            g.setColour (c.withAlpha (selected ? 0.95f : 0.65f));
            g.drawRoundedRectangle (pill, 2.0f, 1.0f);
            g.setColour (selected ? Theme::textMain : Theme::textMuted);
            g.setFont (Theme::uiSize (8.0f).withStyle (selected ? juce::Font::bold : juce::Font::plain));
            g.drawText (bpmText, pill.toNearestInt(), juce::Justification::centred);
        }
    }

    void drawTimeSigLane (juce::Graphics& g)
    {
        auto area = timeSigLaneArea();
        if (area.isEmpty())
            return;

        auto labelArea   = area.withWidth (kRulerLaneLabelW);
        auto contentArea = area.withTrimmedLeft (kRulerLaneLabelW);

        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRect (area);
        g.setColour (Theme::surface.withAlpha (0.92f));
        g.fillRect (labelArea);
        g.setColour (Theme::border.withAlpha (0.22f));
        g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());
        g.drawVerticalLine (contentArea.getX(), (float) area.getY(), (float) area.getBottom());

        g.setColour (Theme::textMuted.withAlpha (0.65f));
        g.setFont (Theme::uiSize (7.5f).withStyle (juce::Font::bold));
        g.drawText ("SIG", labelArea.withWidth (20).reduced (2, 0), juce::Justification::centredLeft);

        for (int i = 0; i < audioEngine.getNumTimeSigs(); ++i)
        {
            const double beat = audioEngine.getTimeSigStartBeat (i);
            const double time = audioEngine.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds();
            const auto sig = audioEngine.getTimeSigAtIndex (i);
            const bool selected = i == selectedTimeSigIndex;
            const bool isRoot = (i == 0 && beat <= 0.0001);

            juce::Rectangle<float> flag;
            float anchorX = 0.0f;

            if (isRoot)
            {
                flag = labelArea.withTrimmedLeft (20).reduced (2.0f, 2.0f).toFloat();
                anchorX = flag.getCentreX();
            }
            else
            {
                anchorX = timeToX (time);
                if (anchorX < (float) contentArea.getX() || anchorX > getWidth() - kVScrollW)
                    continue;

                flag = { anchorX + 4.0f, area.getY() + 2.0f, 32.0f, 12.0f };
            }

            g.setColour ((selected ? Theme::accent : Theme::meterYellow).withAlpha (selected ? 0.96f : 0.76f));
            g.fillRoundedRectangle (flag, 2.0f);
            g.setColour (selected ? Theme::accent.brighter (0.25f) : juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (flag, 2.0f, selected ? 1.5f : 1.0f);

            if (! isRoot)
            {
                g.setColour (Theme::border.withAlpha (0.35f));
                g.drawLine (anchorX, (float) area.getBottom() - 1.0f, anchorX, (float) kRulerTopH, 1.0f);
            }

            g.setColour (juce::Colours::black.withAlpha (0.82f));
            g.setFont (Theme::uiSize (8.0f).withStyle (juce::Font::bold));
            g.drawText (sig, flag.toNearestInt(), juce::Justification::centred);
        }
    }

    double pixelsForStartTime (double seconds) const
    {
        return std::floor (juce::jmax (0.0, seconds) * pxPerSec + 0.5);
    }

    int clampScrollY (int y) const
    {
        const int visibleH = juce::jmax (1, laneBottom() - laneTop());
        return juce::jlimit (0, juce::jmax (0, contentHeight() - visibleH), y);
    }

    /** Every scroll goes through here. Moves the cached pixels of the lane area
        when it can, so only the strip scrolled into view is drawn again;
        otherwise repaints everything. Returns true when pixels were moved. */
    bool scrollTo (double newScrollPx, int newScrollY)
    {
        // On a scaled display only steps of whole physical pixels can reuse
        // pixels, so positions stay on that grid (4 px at 125 %). The bottom
        // of the content stays reachable.
        if (const int step = scrollStep(); step > 1)
        {
            newScrollPx = std::round (newScrollPx / step) * step;
            if (newScrollY < clampScrollY (std::numeric_limits<int>::max()))
                newScrollY = juce::jmax (0, newScrollY / step * step);
        }

        const double dx = scrollPx - newScrollPx;
        const int    dy = scrollY - newScrollY;

        if (dx == 0.0 && dy == 0)
            return false;

        scrollPx = newScrollPx;
        scrollY  = newScrollY;

        if (scrollCachedPixels ((int) dx, dy))
            return true;

        repaint();
        return false;
    }

    double getScrollPx() const   { return scrollPx; }
    int    getScrollY() const    { return scrollY; }

    /** Scroll positions are multiples of this many logical pixels: 1 at
        100 %, more on a scaled display (CachedLayer::getWholePixelStep). */
    int scrollStep() const
    {
        if (auto* layer = dynamic_cast<CachedLayer*> (getCachedComponentImage()))
            return layer->getWholePixelStep();
        return 1;
    }

    /** Shows a track's automation lane, as its A button does. */
    void showAutomationLane (tracktion::Track& track)
    {
        automationVisibleTracks.addIfNotAlreadyThere (track.itemID.toString());
        updateScrollBar();
        repaint();
    }

    /** Things drawn at a fixed place on screen (drag previews, the razor line,
        a value tooltip, editors) would be carried along by moving pixels. */
    bool canScrollByCopying() const
    {
        return ! dragging && ! fileDragActive && ! pluginDragActive
            && dragMode == DragMode::none
            && activeTool != EditTool::razor && activeTool != EditTool::comp
            && ! currentTooltip.isValid
            && ! trackNameEditor.isVisible() && ! rulerValueEditor.isVisible()
            && ! audioEngine.isRecording();
    }

    /** Scroll by moving the cached lane pixels (see CachedLayer::scroll).
        Everything in the lane area is drawn at positions that move by exactly
        the scroll distance: whole-pixel scroll steps, timeToX on a 1/256 px
        grid, clip edges and waveform tiles at whole pixels, and a flat lane
        background. A horizontal scroll redraws the ruler, whose labels are
        laid out per view. */
    bool scrollCachedPixels (int dx, int dy)
    {
        auto* layer = dynamic_cast<CachedLayer*> (getCachedComponentImage());

        if (layer == nullptr || (dx != 0 && dy != 0) || ! canScrollByCopying())
            return false;

        const juce::Rectangle<int> lanes (dx != 0 ? kHeaderWidth : 0, laneTop(),
                                          getWidth() - kVScrollW - (dx != 0 ? kHeaderWidth : 0),
                                          laneBottom() - laneTop());

        if (! layer->scroll (lanes, dx, dy))
            return false;

        if (dx != 0)
        {
            layer->invalidateOnly ({ kHeaderWidth, 0, getWidth() - kHeaderWidth, kRulerH });
            // Automation lanes mark the current value at the right edge: redraw
            // the edge, and the place the moved pixels carried the old mark to.
            const juce::Rectangle<int> valueMarks (lanes.getRight() - 4, lanes.getY(), 4, lanes.getHeight());
            layer->invalidateOnly (valueMarks);
            layer->invalidateOnly (valueMarks.translated (dx, 0));
        }

        layer->repaintKeepingContents();
        return true;
    }

    void updateScrollBar()
    {
        double totalLen = audioEngine.getEdit().getLength().inSeconds() + 60.0;
        double viewLen  = (getWidth() - kHeaderWidth - kVScrollW) / pxPerSec;
        horizontalScrollBar.setRangeLimits (0.0, totalLen);
        horizontalScrollBar.setCurrentRange (getStartTime(), viewLen);

        int visibleH = juce::jmax (1, laneBottom() - laneTop());
        int totalH   = juce::jmax (visibleH, contentHeight());
        verticalScrollBar.setRangeLimits (0.0, (double) totalH);
        verticalScrollBar.setCurrentRange ((double) scrollY, (double) visibleH);

        // Clamp scrollY when content shrinks.
        scrollTo (scrollPx, clampScrollY (scrollY));
    }

    void resized() override
    {
        const int barY = getHeight() - kFooterH + 6;
        horizontalScrollBar.setBounds (kHeaderWidth, barY, getWidth() - kHeaderWidth - kVScrollW, 14);
        zoomSlider.setBounds (10, barY, kHeaderWidth - 20, 14);
        verticalScrollBar.setBounds (getWidth() - kVScrollW, laneTop(), kVScrollW, laneBottom() - laneTop());
        updateScrollBar();
    }

    // Positions are rounded to 1/256 px (the software renderer's subpixel
    // grid) with floor (v + 0.5), so scrolling by whole pixels moves every
    // position by exactly that many pixels and cached pixels can be reused
    // (scrollCachedPixels).
    float timeToX (double t) const
    {
        const double x = t * pxPerSec - scrollPx;
        return (float) kHeaderWidth + (float) (std::floor (x * 256.0 + 0.5) / 256.0);
    }

    double xToTime (float x) const { return ((double) (x - kHeaderWidth) + scrollPx) / pxPerSec; }

    /** Time at the left edge of the lane area. */
    double getStartTime() const { return scrollPx / pxPerSec; }

    double pxPerBeatAt (tracktion::TempoSequence& ts, double beat) const
    {
        const double t0 = ts.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds();
        const double t1 = ts.toTime (tracktion::BeatPosition::fromBeats (beat + 1.0)).inSeconds();
        return (t1 - t0) * pxPerSec;
    }

    static tracktion::tempo::BarsAndBeats barsAndBeatsAtBeat (tracktion::TempoSequence& ts, double beat)
    {
        return ts.toBarsAndBeats (ts.toTime (tracktion::BeatPosition::fromBeats (beat)));
    }

    float barBeatToX (tracktion::TempoSequence& ts, int barIndex, int beatInBar) const
    {
        tracktion::tempo::BarsAndBeats bb;
        bb.bars = barIndex;
        bb.beats = tracktion::BeatDuration::fromBeats ((double) beatInBar);
        return timeToX (ts.toTime (ts.toBeats (bb)).inSeconds());
    }

    int beatsPerBarAt (tracktion::TempoSequence& ts, int barIndex) const
    {
        tracktion::tempo::BarsAndBeats bb;
        bb.bars = barIndex;
        bb.beats = tracktion::BeatDuration {};
        const double time = ts.toTime (ts.toBeats (bb)).inSeconds();
        return juce::jmax (1, (int) ts.getTimeSigAt (tracktion::TimePosition::fromSeconds (time)).numerator);
    }

    double absoluteBeatAt (tracktion::TempoSequence& ts, int barIndex, int beatInBar) const
    {
        tracktion::tempo::BarsAndBeats bb;
        bb.bars = barIndex;
        bb.beats = tracktion::BeatDuration::fromBeats ((double) beatInBar);
        return ts.toBeats (bb).inBeats();
    }

    juce::Array<tracktion::Track*> getSelectedTracks() const
    {
        juce::Array<tracktion::Track*> result;
        auto top = audioEngine.getTopLevelTracks();

        std::function<tracktion::Track*(tracktion::Track*, const juce::String&)> findById =
            [&findById] (tracktion::Track* track, const juce::String& id) -> tracktion::Track*
            {
                if (track == nullptr)
                    return nullptr;

                if (track->itemID.toString() == id)
                    return track;

                if (auto* folder = dynamic_cast<tracktion::FolderTrack*> (track))
                    for (auto* child : folder->getAllAudioSubTracks (false))
                        if (auto* found = findById (child, id))
                            return found;

                return nullptr;
            };

        for (auto& id : selectedIds)
            for (auto* t : top)
                if (auto* found = findById (t, id)) { result.add(found); break; }
        return result;
    }

    int getSelectedIndex() const
    {
        if (selectedIds.isEmpty()) return -1;
        auto rows = const_cast<Timeline*> (this)->getVisibleRows();
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].track != nullptr && rows[i].track->itemID.toString() == selectedIds[0])
                return i;
        return -1;
    }

    void paint(juce::Graphics& g) override
    {
        AERION_PROFILE_SCOPE ("Timeline::paint");

        trackButtonCache.clear();  // Clear button bounds cache before repainting
        currentTooltip.isValid = false;

        AERION_PROFILE_SECTION (chromeZone, "Timeline.chrome");
        {
            // The lane area scrolls by moving its pixels (scrollCachedPixels), so
            // its background must not depend on where it is on screen: flat, not
            // the window gradient.
            juce::Graphics::ScopedSaveState s (g);
            g.excludeClipRegion ({ 0, laneTop(), getWidth(), laneBottom() - laneTop() });
            Theme::fillBackgroundGradient (g, getLocalBounds());
        }
        g.setColour (Theme::backgroundMid());
        g.fillRect (0, laneTop(), getWidth(), laneBottom() - laneTop());

        // Header column action bar (+ Track / + MIDI / + Folder)
        int btnW = (kHeaderWidth - 24) / 3;
        addTrackBtn     = juce::Rectangle<int>(8,               4, btnW, kHeaderBarH - 8);
        addMidiTrackBtn = juce::Rectangle<int>(8 + btnW + 4,    4, btnW, kHeaderBarH - 8);
        addFolderBtn    = juce::Rectangle<int>(8 + (btnW + 4)*2, 4, btnW, kHeaderBarH - 8);

        // Header column (slightly raised)
        Theme::fillVerticalGradient (g, { 0, 0, kHeaderWidth, kHeaderBarH },
                                     Theme::bgPanel.brighter (0.08f), Theme::bgPanel.darker (0.06f));
        // Borders next to the lane area are crisp 1 px fills on their own side:
        // an anti-aliased line on the edge would put half a pixel into the lanes,
        // which scroll by moving pixels (scrollCachedPixels).
        g.setColour(Theme::border);
        g.fillRect (kHeaderWidth - 1, 0, 1, kHeaderBarH);

        drawHeaderButton(g, addTrackBtn,     "+ Audio", Theme::accent);
        drawHeaderButton(g, addMidiTrackBtn, "+ MIDI",  Theme::trackColours[3]);
        drawHeaderButton(g, addFolderBtn,    "+ Folder", Theme::active);
        AERION_PROFILE_SECTION_END (chromeZone);

        AERION_PROFILE_SECTION (rulerZone, "Timeline.ruler");

        // Ruler (right of header bar)
        Theme::fillVerticalGradient (g, { kHeaderWidth, 0, getWidth() - kHeaderWidth, kRulerH },
                                     Theme::bgPanel.brighter (0.09f), Theme::bgPanel.darker (0.06f));
        g.setColour(Theme::border);
        g.fillRect (0, kRulerH - 1, getWidth(), 1);
        g.drawLine((float)kHeaderWidth, 0.0f, (float)getWidth(), 0.0f);

        auto& transport = audioEngine.getEdit().getTransport();
        auto loopRange = transport.getLoopRange();
        float loopX0 = timeToX (loopRange.getStart().inSeconds());
        float loopX1 = timeToX (loopRange.getEnd().inSeconds());

        // Draw Loop Range
        if (loopX1 > kHeaderWidth)
        {
            float drawX0 = juce::jmax ((float) kHeaderWidth, loopX0);
            float drawX1 = juce::jmin ((float) (getWidth() - kVScrollW), loopX1);
            if (drawX1 > drawX0)
            {
                g.setColour (Theme::active.withAlpha (0.15f));
                g.fillRect (drawX0, 0.0f, drawX1 - drawX0, (float) kRulerH);
                g.setColour (Theme::active.withAlpha (0.4f));
                g.drawHorizontalLine (0, drawX0, drawX1);
                g.drawHorizontalLine (kRulerH - 1, drawX0, drawX1);
                
                // Handles
                g.setColour (Theme::active);
                if (loopX0 >= kHeaderWidth) {
                    juce::Path p;
                    p.addTriangle (loopX0, 0, loopX0 + 6, 0, loopX0, 8);
                    g.fillPath (p);
                    g.drawVerticalLine ((int) loopX0, 0.0f, (float) kRulerH);
                }
                if (loopX1 >= kHeaderWidth && loopX1 < getWidth() - kVScrollW) {
                    juce::Path p;
                    p.addTriangle (loopX1, 0, loopX1 - 6, 0, loopX1, 8);
                    g.fillPath (p);
                    g.drawVerticalLine ((int) loopX1, 0.0f, (float) kRulerH);
                }
            }
        }

        // Draw Markers
        if (auto* mt = audioEngine.getEdit().getMarkerTrack())
        {
            for (auto* clip : mt->getClips())
            {
                if (auto* marker = dynamic_cast<tracktion::MarkerClip*> (clip))
                {
                    float mx = timeToX (marker->getPosition().getStart().inSeconds());
                    if (mx >= kHeaderWidth && mx < getWidth() - kVScrollW)
                    {
                        g.setColour (Theme::accent.withAlpha (0.85f));
                        g.fillRoundedRectangle (mx, (float) kRulerSigH + 2.0f, 10.0f, 10.0f, 2.0f); // Flag
                        g.fillRect (mx + 0.5f, (float) kRulerSigH + 2.0f, 1.0f, 18.0f);              // Pole
                        g.setColour (Theme::textMain);
                        g.setFont (Theme::uiSize (9.0f));
                        g.drawText (marker->getName(), (int) mx + 12, kRulerSigH + 2, 60, 12, juce::Justification::left);
                    }
                }
            }
        }

        g.setFont (Theme::uiSize (10.0f));

        auto& ts = audioEngine.getEdit().tempoSequence;
        const auto startBB = ts.toBarsAndBeats (tracktion::TimePosition::fromSeconds (getStartTime()));
        const auto endBB   = ts.toBarsAndBeats (tracktion::TimePosition::fromSeconds (xToTime ((float) getWidth())));
        const int startBar = juce::jmax (0, (int) startBB.bars);
        const int endBar   = (int) endBB.bars + 1;

        double minPxPerBeat = pxPerBeatAt (ts, absoluteBeatAt (ts, startBar, 0));
        double minPxPerBar  = minPxPerBeat * beatsPerBarAt (ts, startBar);
        for (int ti = 0; ti < ts.getNumTempos(); ++ti)
        {
            if (auto* tempo = ts.getTempo (ti))
            {
                const double beat = tempo->getStartBeat().inBeats();
                minPxPerBeat = juce::jmin (minPxPerBeat, pxPerBeatAt (ts, beat));
            }
        }
        for (int bar = startBar; bar <= endBar; ++bar)
        {
            const int bpb = beatsPerBarAt (ts, bar);
            const double barPx = pxPerBeatAt (ts, absoluteBeatAt (ts, bar, 0)) * (double) bpb;
            minPxPerBar = juce::jmin (minPxPerBar, barPx);
        }

        int barLabelStep = 1;
        while (minPxPerBar * (double) barLabelStep < 70.0)
            barLabelStep *= 2;

        const bool showBeatLabels    = (minPxPerBeat >= 28.0);
        const bool showSubBeatLabels = (minPxPerBeat * 0.25 >= 24.0);
        const bool showBeatGrid      = (minPxPerBeat >= 20.0);

        // Musical bar/beat ruler — respects time-signature and tempo changes.
        for (int bar = startBar; bar <= endBar; ++bar)
        {
            const int beatsPerBar = beatsPerBarAt (ts, bar);
            const bool labelThisBar = ((bar % barLabelStep) == 0);

            for (int beatInBar = 0; beatInBar < beatsPerBar; ++beatInBar)
            {
                const float x = barBeatToX (ts, bar, beatInBar);
                if (x + 1.0f < (float) kRulerContentX) continue;
                if (x > (float) getWidth()) break;

                const bool isBarLine = (beatInBar == 0);
                const double absBeat = absoluteBeatAt (ts, bar, beatInBar);
                const double localPxPerBeat = pxPerBeatAt (ts, absBeat);

                if (x + 1.0f >= (float) kRulerContentX)
                {
                    const float tickH = isBarLine ? (float) kRulerBeatH : 8.0f;
                    g.setColour (Theme::border.withAlpha (isBarLine ? 1.0f : 0.45f));
                    g.drawLine (x, (float) kRulerTopH - tickH, x, (float) kRulerTopH);
                }

                if (x + 3.0f >= (float) kRulerContentX)
                {
                    if (isBarLine && labelThisBar)
                    {
                        g.setColour (Theme::textMain.withAlpha (0.9f));
                        g.setFont (Theme::uiSize (10.0f).boldened());
                        g.drawText (juce::String::formatted ("%d.1", bar + 1),
                                    (int) x + 3, kRulerSigH + 3, 36, kRulerBeatH - 4, juce::Justification::left);
                    }
                    else if (! isBarLine && showBeatLabels && localPxPerBeat >= 24.0)
                    {
                        g.setColour (Theme::textMuted.withAlpha (0.72f));
                        g.setFont (Theme::uiSize (9.0f));
                        g.drawText (juce::String::formatted ("%d.%d", bar + 1, beatInBar + 1),
                                    (int) x + 3, kRulerSigH + 3, 28, kRulerBeatH - 4, juce::Justification::left);
                    }
                }

                if (isBarLine && bar > 0 && labelThisBar && x + 3.0f >= (float) kRulerContentX + 48.0f)
                {
                    const double posSeconds = ts.toTime (tracktion::BeatPosition::fromBeats (absBeat)).inSeconds();
                    const int totalMs = juce::roundToInt (posSeconds * 1000.0);
                    const juce::String timeStr = juce::String::formatted ("%d:%02d.%03d",
                                                                          (totalMs / 1000) / 60,
                                                                          (totalMs / 1000) % 60,
                                                                          totalMs % 1000);
                    g.setColour (Theme::textMuted.withAlpha (0.6f));
                    g.setFont (Theme::uiSize (8.5f));
                    g.drawText (timeStr, (int) x + 3, kRulerTopH + 3, 70, kRulerBotH - 5,
                                juce::Justification::left);
                }
            }
        }

        // Sub-beat ticks: quarter-note subdivisions inside each beat when zoomed in.
        if (showSubBeatLabels)
        {
            for (int bar = startBar; bar <= endBar; ++bar)
            {
                const int beatsPerBar = beatsPerBarAt (ts, bar);
                for (int beatInBar = 0; beatInBar < beatsPerBar; ++beatInBar)
                {
                    const double absBeat = absoluteBeatAt (ts, bar, beatInBar);
                    if (pxPerBeatAt (ts, absBeat) * 0.25 < 12.0)
                        continue;

                    for (int sub = 1; sub < 4; ++sub)
                    {
                        const double subPos = absBeat + (double) sub * 0.25;
                        const float x = timeToX (ts.toTime (tracktion::BeatPosition::fromBeats (subPos)).inSeconds());
                        if (x + 1.0f < (float) kRulerContentX || x > (float) getWidth())
                            continue;

                        g.setColour (Theme::border.withAlpha (0.22f));
                        g.drawLine (x, (float) kRulerTopH - 4.0f, x, (float) kRulerTopH);
                    }
                }
            }
        }

        // Separator line between top and bottom bands
        g.setColour (Theme::border.withAlpha (0.4f));
        g.drawHorizontalLine (kRulerTopH, (float)kHeaderWidth, (float)getWidth());

        drawTimeSigLane (g);
        drawTempoLane (g);
        AERION_PROFILE_SECTION_END (rulerZone);

        auto top = audioEngine.getTopLevelTracks();

        // Clip the lane area so rows don't paint over the footer (zoom slider / hscroll bar).
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (0, laneTop(), getWidth() - kVScrollW, laneBottom() - laneTop());

            // Draw vertical bar/beat grid lines through the track lane body
            {
                AERION_PROFILE_SCOPE ("Timeline.grid");

                g.setColour (Theme::border.withAlpha (0.15f));
                for (int bar = startBar; bar <= endBar + 1; ++bar)
                {
                    const float gx = barBeatToX (ts, bar, 0);
                    if (gx < (float) kHeaderWidth || gx > (float) getWidth())
                        continue;
                    g.drawVerticalLine ((int) gx, (float) laneTop(), (float) laneBottom());
                }

                if (showBeatGrid)
                {
                    g.setColour (Theme::border.withAlpha (0.07f));
                    for (int bar = startBar; bar <= endBar; ++bar)
                    {
                        const int beatsPerBar = beatsPerBarAt (ts, bar);
                        for (int beatInBar = 1; beatInBar < beatsPerBar; ++beatInBar)
                        {
                            const float gx = barBeatToX (ts, bar, beatInBar);
                            if (gx < (float) kHeaderWidth || gx > (float) getWidth())
                                continue;
                            g.drawVerticalLine ((int) gx, (float) laneTop(), (float) laneBottom());
                        }
                    }
                }
            }

            {
                AERION_PROFILE_SCOPE ("Timeline.rows");

                int y = laneTop() - scrollY;
                for (int i = 0; i < top.size(); ++i)
                    y = drawTrackRow (g, top[i], i, /*indent*/ 0, y);
            }

            if (top.isEmpty()) {
                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (13.0f));
                g.drawText ("No tracks yet. Click  + Track  to begin.",
                            0, kRulerH, getWidth(), 60, juce::Justification::centred);
            }

            if (dragging)
            {
                if (dropDetachToTopLevel)
                {
                    g.setColour (Theme::accent.withAlpha (0.22f));
                    g.fillRect (0, kRulerH, 14, laneBottom() - kRulerH);
                    g.setColour (Theme::active.withAlpha (0.85f));
                    g.drawVerticalLine (14, (float) kRulerH, (float) laneBottom());
                }
                else if (dropFolderTarget != nullptr)
                {
                    for (auto& row : getVisibleRows())
                    {
                        if (row.track == dropFolderTarget)
                        {
                            int ry = kRulerH + row.y - scrollY;
                            g.setColour (Theme::active.withAlpha (0.18f));
                            g.fillRect (0, ry, getWidth() - kVScrollW, row.height);
                            g.setColour (Theme::active);
                            g.drawRect (0, ry, getWidth() - kVScrollW, row.height, 2);
                            break;
                        }
                    }
                }
                else if (dropPreviewY >= 0)
                {
                    g.setColour (Theme::active);
                    g.fillRect (0.0f, (float) dropPreviewY - 1.0f, (float) (getWidth() - kVScrollW), 2.0f);
                }
            }

            // -- File-drag ghost preview -----------------------------------
            if (fileDragActive)
            {
                auto rows = getVisibleRows();
                float gx  = timeToX (fileDragSnappedTime);
                float gw  = juce::jmax (20.0f, (float) (fileDragPreviewLength * pxPerSec));

                if (fileDragTargetRowIdx >= 0 && fileDragTargetRowIdx < rows.size())
                {
                    auto& row = rows.getReference (fileDragTargetRowIdx);
                    int ry = kRulerH + row.y - scrollY;

                    // Row highlight
                    g.setColour (Theme::active.withAlpha (0.12f));
                    g.fillRect (kHeaderWidth, ry, getWidth() - kHeaderWidth - kVScrollW, row.height);
                    g.setColour (Theme::active.withAlpha (0.65f));
                    g.drawRect (kHeaderWidth, ry, getWidth() - kHeaderWidth - kVScrollW, row.height, 1);

                    // Ghost clip
                    juce::Rectangle<float> gc (gx, (float) ry + 4.0f, gw, (float) row.height - 8.0f);
                    g.setColour (Theme::active.withAlpha (0.30f));
                    g.fillRoundedRectangle (gc, 4.0f);
                    g.setColour (Theme::active.withAlpha (0.85f));
                    g.drawRoundedRectangle (gc, 4.0f, 1.5f);
                    g.setColour (Theme::textMain.withAlpha (0.85f));
                    g.setFont (Theme::uiSize (10.0f));
                    g.drawText (juce::String (fileDragSnappedTime, 2) + "s",
                                (int) gx + 5, (int) gc.getY() + 3, 60, 12,
                                juce::Justification::left);
                }
                else
                {
                    // New-track zone: draw accent line below last track + ghost shape
                    int lineY = rows.isEmpty()
                                    ? laneTop() + 4
                                    : kRulerH + rows.getLast().y + rows.getLast().height - scrollY + 2;
                    g.setColour (Theme::active.withAlpha (0.85f));
                    g.fillRect (kHeaderWidth, lineY, getWidth() - kHeaderWidth - kVScrollW, 2);

                    juce::Rectangle<float> gc (gx, (float) lineY + 3.0f, gw, (float) kTrackH - 8.0f);
                    g.setColour (Theme::active.withAlpha (0.25f));
                    g.fillRoundedRectangle (gc, 4.0f);
                    g.setColour (Theme::active.withAlpha (0.75f));
                    g.drawRoundedRectangle (gc, 4.0f, 1.5f);
                }
            }

            // -- Plugin-drag highlight on track header ---------------------
            if (pluginDragActive && pluginDragTargetRow >= 0)
            {
                auto rows = getVisibleRows();
                if (pluginDragTargetRow < rows.size())
                {
                    auto& row = rows.getReference (pluginDragTargetRow);
                    int ry = kRulerH + row.y - scrollY;
                    g.setColour (Theme::accent.withAlpha (0.22f));
                    g.fillRect (0, ry, kHeaderWidth, row.height);
                    g.setColour (Theme::accent.withAlpha (0.90f));
                    g.drawRect (0, ry, kHeaderWidth, row.height, 2);
                    g.setColour (Theme::textMain);
                    g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
                    g.drawText ("DROP PLUGIN", 0, ry + row.height / 2 - 7,
                                kHeaderWidth, 14, juce::Justification::centred);
                }
            }
        }

        // Footer band background.
        g.setColour (Theme::bgPanel);
        g.fillRect (0, getHeight() - kFooterH, getWidth(), kFooterH);
        g.setColour (Theme::border);
        g.fillRect (0, getHeight() - kFooterH, getWidth(), 1);

        // The playhead is drawn by TimelinePlayheadOverlay, not here, so that
        // moving it never invalidates this component's cached layer.

        // Razor hairline preview
        if (activeTool == EditTool::razor && lastMouseX >= kHeaderWidth && lastMouseX < getWidth() - kVScrollW)
        {
            const bool   snapEnabled_  = (bool)   projectData.getProjectTree().getProperty (IDs::snapEnabled,  true);
            const double snapInterval_ = (double) projectData.getProjectTree().getProperty (IDs::snapInterval, 1.0);
            float drawX = (float) lastMouseX;
            if (snapEnabled_)
            {
                auto& ts = audioEngine.getEdit().tempoSequence;
                auto t = tracktion::TimePosition::fromSeconds (xToTime (drawX));
                auto beats = ts.toBeats (t);
                double snappedBeats = std::round (beats.inBeats() / snapInterval_) * snapInterval_;
                drawX = timeToX (ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds());
            }

            g.setColour (Theme::accent.withAlpha (0.6f));
            g.drawLine (drawX, (float) kRulerH, drawX, (float) (getHeight() - kFooterH), 1.0f);
        }

        // Draw the tooltip after all other drawing is done
        if (currentTooltip.isValid)
        {
            juce::Font font = Theme::uiSize (12.0f);
            g.setFont (font);
            g.setColour (Theme::surface);
            g.fillRoundedRectangle (currentTooltip.bounds.toFloat(), 3.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (currentTooltip.bounds.toFloat(), 3.0f, 1.0f);
            g.setColour (Theme::textMain);
            g.drawText (currentTooltip.text, currentTooltip.bounds, juce::Justification::centred);
        }
    }

    static void drawHeaderButton (juce::Graphics& g, juce::Rectangle<int> b,
                                  const juce::String& label, juce::Colour col)
    {
        g.setColour(col.withAlpha(0.18f));
        g.fillRoundedRectangle(b.toFloat(), 4.0f);
        g.setColour(col);
        g.drawRoundedRectangle(b.toFloat(), 4.0f, 1.0f);
        g.setFont (Theme::uiSize (11.0f).withStyle (juce::Font::bold));
        g.drawText(label, b, juce::Justification::centred);
    }

    struct RowInfo {
        tracktion::Track* track;
        tracktion::FolderTrack* parent;  // nullptr = top-level
        int y;
        int height;
        int indent;
    };

    juce::Array<RowInfo> getVisibleRows()
    {
        juce::Array<RowInfo> rows;
        int currentRowY = 0;
        auto top = audioEngine.getTopLevelTracks();

        std::function<void(tracktion::Track*, tracktion::FolderTrack*, int)> addTrack =
            [&](tracktion::Track* t, tracktion::FolderTrack* parentFolder, int indent) {
            int th = getTrackHeight (t);
            rows.add ({ t, parentFolder, currentRowY, th, indent });
            currentRowY += th;

            if (auto* f = dynamic_cast<tracktion::FolderTrack*>(t))
                if (! collapsedFolders.contains (f->itemID.toString()))
                    for (auto* child : f->getAllAudioSubTracks (false))
                        addTrack (child, f, indent + 16);
        };

        for (auto* t : top)
            addTrack (t, nullptr, 0);

        return rows;
    }

    /** The area a clip's drawing covers on screen, matching drawTrackRow's
        layout, clipped to the lane area. Empty if the clip's track is not in
        the visible row list (e.g. inside a collapsed folder). */
    juce::Rectangle<int> getClipPaintBounds (tracktion::Clip& clip)
    {
        auto* track = clip.getTrack();
        for (auto& row : getVisibleRows())
        {
            if (row.track != track)
                continue;

            const auto pos = clip.getPosition();
            const juce::Rectangle<float> body (timeToX (pos.getStart().inSeconds()),
                                               (float) (laneTop() + row.y - scrollY),
                                               (float) (pos.getLength().inSeconds() * pxPerSec),
                                               (float) getLaneHeight (track));

            return body.expanded (kClipPaintMargin).getSmallestIntegerContainer()
                       .getIntersection ({ kHeaderWidth, laneTop(),
                                           getWidth() - kHeaderWidth, laneBottom() - laneTop() });
        }

        return {};
    }

    static bool isClipEditDrag (DragMode m)
    {
        return m == DragMode::move || m == DragMode::trimLeft || m == DragMode::trimRight
            || m == DragMode::fadeLeft || m == DragMode::fadeRight;
    }

    /** A track's lane height: what its bottom edge was dragged to, saved with
        the project on the track's state (IDs::laneHeight), kTrackH by default. */
    static int getLaneHeight (const tracktion::Track* t)
    {
        return juce::jlimit (kMinTrackH, kMaxTrackH, (int) t->state.getProperty (IDs::laneHeight, kTrackH));
    }

    /** The whole row: the lane plus its automation lane when shown. */
    int getTrackHeight (tracktion::Track* t) const
    {
        const int lane = getLaneHeight (t);
        if (automationVisibleTracks.contains (t->itemID.toString()))
            return lane + kAutoLaneH;
        return lane;
    }

    /** The track whose lane's bottom edge is under `pos` in the header
        column, or nullptr. Dragging that edge resizes the track. */
    tracktion::Track* getLaneEdgeTrackAt (juce::Point<int> pos)
    {
        if (pos.x < 0 || pos.x >= kHeaderWidth || pos.y < laneTop() || pos.y >= laneBottom())
            return nullptr;

        const int targetY = pos.y - kRulerH + scrollY;
        for (auto& row : getVisibleRows())
        {
            const int edge = row.y + getLaneHeight (row.track);
            if (std::abs (targetY - edge) <= kResizeGrabH)
                return row.track;
            if (row.y > targetY + kResizeGrabH)
                break;
        }
        return nullptr;
    }

    /** Sets lane heights and lays the Timeline out again. The Edit is marked
        changed (heights are saved with the project) but not through the undo
        manager: resizing a track is not an edit to undo. */
    bool setLaneHeights (const juce::Array<tracktion::Track*>& tracks, int height)
    {
        height = juce::jlimit (kMinTrackH, kMaxTrackH, height);
        bool changed = false;
        for (auto* t : tracks)
        {
            if (getLaneHeight (t) == height)
                continue;
            if (height == kTrackH)
                t->state.removeProperty (IDs::laneHeight, nullptr);
            else
                t->state.setProperty (IDs::laneHeight, height, nullptr);
            changed = true;
        }

        if (changed)
        {
            updateScrollBar();
            repaint();
        }
        return changed;
    }

    /** setLaneHeights for a finished gesture: marks the project changed. */
    void commitLaneHeights (const juce::Array<tracktion::Track*>& tracks, int height)
    {
        if (setLaneHeights (tracks, height))
            audioEngine.getEdit().markAsChanged();
    }

    /** The tracks a resize of `t` applies to: all selected tracks when `t` is
        one of them, otherwise just `t`. */
    juce::Array<tracktion::Track*> getResizeGroup (tracktion::Track* t)
    {
        auto selected = getSelectedTracks();
        if (selected.contains (t))
            return selected;
        return { t };
    }

    int drawTrackRow(juce::Graphics& g, tracktion::Track* track, int topIndex, int indent, int y)
    {
        auto* folder = dynamic_cast<tracktion::FolderTrack*>(track);
        auto* audio  = dynamic_cast<tracktion::AudioTrack*>(track);
        int   rowH   = getTrackHeight(track);
        const int laneH = getLaneHeight (track);

        AERION_PROFILE_COUNT ("Timeline.rowsVisited", 1);

        // mouseDown hit-tests the M/S/R/A buttons through this cache, so it has
        // to be populated even for rows we are about to skip drawing - otherwise
        // culling would silently stop those buttons responding.
        const int textX = 14 + indent;
        const int btnY  = y + 32;
        trackButtonCache[track->itemID.toString()] = {
            juce::Rectangle<int> (textX,      btnY, 24, 22),
            juce::Rectangle<int> (textX + 28, btnY, 24, 22),
            juce::Rectangle<int> (textX + 56, btnY, 24, 22),
            juce::Rectangle<int> (textX + 84, btnY, 24, 22)
        };

        // Rows outside the invalidated region still cost a full walk of their
        // clips, waveform cache lookups and engine queries even though every
        // drawing call is thrown away by the clip. Skip that work, but keep
        // advancing y and still descend into folder children, which may be
        // visible even when their parent row is not.
        // + 1: the row's bottom line is anti-aliased into the next row's first pixel.
        if (! g.clipRegionIntersects (juce::Rectangle<int> (0, y, getWidth(), rowH + 1)))
        {
            y += rowH;

            if (folder != nullptr && ! collapsedFolders.contains (folder->itemID.toString()))
                for (auto* child : folder->getAllAudioSubTracks (false))
                    y = drawTrackRow (g, child, topIndex, indent + 16, y);

            return y;
        }

        AERION_PROFILE_COUNT ("Timeline.rowsDrawn", 1);

        // The row test above is vertical only. Partial repaints (playhead strip,
        // dragged clip) are narrow but span many rows, so also skip the header
        // and any clip that lies entirely left or right of the invalidated area.
        // Direct2D does not discard off-clip drawing cheaply, so this matters
        // more there than with the software renderer. clipRegionIntersects tests
        // each invalidated rectangle, not their combined bounding box.

        juce::Colour tColor = Theme::colourForTrack(topIndex);
        bool isSel = selectedIds.contains(track->itemID.toString());
        bool isAuto = automationVisibleTracks.contains (track->itemID.toString());

        // Lane bg
        g.setColour((topIndex % 2 == 1 ? Theme::bgPanel.withAlpha(0.2f) : juce::Colours::transparentBlack));
        g.fillRect(kHeaderWidth, y, getWidth() - kHeaderWidth, rowH);
        g.setColour(Theme::border.withAlpha(0.3f));
        g.drawLine((float)kHeaderWidth, (float)(y + rowH), (float)getWidth(), (float)(y + rowH));

        // Header
        juce::Rectangle<int> hb(0, y, kHeaderWidth, rowH);
        if (g.clipRegionIntersects (hb))
        {
            AERION_PROFILE_SCOPE ("Timeline.rowHeader");
            g.setColour(isSel ? Theme::surface : Theme::bgPanel);
            g.fillRect(hb);
            g.setColour(Theme::border);
            g.drawLine(0.0f, (float)(y + rowH), (float)kHeaderWidth, (float)(y + rowH));
            g.fillRect (kHeaderWidth - 1, y, 1, rowH);

            g.setColour(tColor);
            const bool submixFolder = folder != nullptr && audioEngine.isFolderSubmix (folder);
            const float barW = submixFolder ? 3.0f : 4.0f;
            g.fillRect((float)indent, (float)y, barW, (float)rowH);

            g.setColour(Theme::textMain);
            g.setFont (Theme::uiSize (13.0f).withStyle (juce::Font::bold));
        
            if (folder != nullptr)
            {
                // Draw expand/collapse chevron
                auto chevronR = juce::Rectangle<int> (textX - 10, y + 10, 10, 10);
                g.setColour (Theme::textMuted);
                juce::Path p;
                if (collapsedFolders.contains (folder->itemID.toString())) {
                    p.startNewSubPath (chevronR.getX() + 2, chevronR.getY());
                    p.lineTo (chevronR.getRight(), chevronR.getCentreY());
                    p.lineTo (chevronR.getX() + 2, chevronR.getBottom());
                } else {
                    p.startNewSubPath (chevronR.getX(), chevronR.getY() + 2);
                    p.lineTo (chevronR.getCentreX(), chevronR.getBottom());
                    p.lineTo (chevronR.getRight(), chevronR.getY() + 2);
                }
                g.strokePath (p, juce::PathStrokeType (1.5f));
                g.setColour (Theme::textMain);
            }

            juce::String label = track->getName();
            if (folder != nullptr && ! submixFolder)
                label += "  [GROUP]";
            g.drawText(label, textX, y + 8, kHeaderWidth - textX - 8, 20, juce::Justification::left);

            if (submixFolder)
            {
                auto br = juce::Rectangle<int> (kHeaderWidth - 22, y + 6, 14, 14);
                g.setColour (tColor.withAlpha (0.45f));
                g.fillRoundedRectangle (br.toFloat(), 2.0f);
                g.setColour (Theme::textMain);
                g.setFont (Theme::uiSize (9.0f).boldened());
                g.drawText ("S", br, juce::Justification::centred);
            }

            // M / S / R / A buttons (top row), FX button below. Bounds were cached
            // for hit-testing above, before the row-culling early-out.
            const auto& btns = trackButtonCache[track->itemID.toString()];
            const auto mB = btns.m;
            const auto sB = btns.s;
            const auto rB = btns.r;
            const auto aB = btns.a;

            bool isMute = track->isMuted(false);
            bool isSolo = track->isSolo(false);
            bool isArm  = audioEngine.isTrackArmed(track);

            paintLetterButton (g, mB, "M", isMute, Theme::meterYellow);
            paintLetterButton (g, sB, "S", isSolo, Theme::accent);
            paintLetterButton (g, rB, "R", isArm,  Theme::recordRed);
            paintLetterButton (g, aB, "A", isAuto, Theme::active);

            // Below kTrackH the FX badge does not fit; the inserts stay in
            // the Inspector and the Mixer.
            if (laneH >= kTrackH)
            {
                int fxY = btnY + 24;
                auto fxB = juce::Rectangle<int>(textX, fxY, 76, 20);
                int  numFx = track->pluginList.size();
                drawFxBadge (g, fxB, numFx);
            }

            if (audio != nullptr)
            {
                const bool frozen = audioEngine.isTrackFrozen (audio);
                const bool freezing = audioEngine.isTrackFreezing (audio);
                if (frozen || freezing)
                {
                    auto badge = juce::Rectangle<int> (kHeaderWidth - 88, y + laneH - 24, 76, 18);
                    const auto badgeColour = freezing ? Theme::meterYellow : juce::Colours::skyblue;
                    g.setColour (badgeColour.withAlpha (0.20f));
                    g.fillRoundedRectangle (badge.toFloat(), 4.0f);
                    g.setColour (badgeColour.withAlpha (0.95f));
                    g.drawRoundedRectangle (badge.toFloat(), 4.0f, 1.0f);
                    g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
                    g.drawText (freezing ? "FREEZING..." : "FROZEN", badge, juce::Justification::centred);
                }
            }
        }

        // Automation lane (volume / pan curve, editable).
        if (isAuto)
            drawAutomationLane (g, track, topIndex, y + laneH);

        // Clips (audio tracks only)
        if (audio != nullptr)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (kHeaderWidth, y, getWidth() - kHeaderWidth, rowH);

            if (activeTool == EditTool::comp)
            {
                // Draw in sub-lanes
                juce::Array<double> laneEndTimes;
                auto clips = audio->getClips();
                for (auto* clip : clips)
                {
                    double startT = clip->getPosition().getStart().inSeconds();
                    double endT   = clip->getPosition().getEnd().inSeconds();
                    
                    int laneIdx = 0;
                    while (laneIdx < laneEndTimes.size() && laneEndTimes[laneIdx] > startT)
                        laneIdx++;
                    
                    if (laneIdx == laneEndTimes.size())
                        laneEndTimes.add (endT);
                    else
                        laneEndTimes.set (laneIdx, endT);

                    float subLaneH = (float) (laneH - 10) / (float) juce::jmax (1, laneEndTimes.size());
                    float clipY = (float) y + 5.0f + (float) laneIdx * subLaneH;
                    
                    juce::Rectangle<float> cb (timeToX (startT), clipY, (float) (endT - startT) * pxPerSec, laneH - 2.0f);
                    if (cb.getRight() < kHeaderWidth || cb.getX() > getWidth()) continue;
                    if (! g.clipRegionIntersects (cb.expanded (kClipPaintMargin).getSmallestIntegerContainer())) continue;

                    g.setColour (tColor.withAlpha (clip->isMuted() ? 0.15f : 0.6f));
                    g.fillRoundedRectangle (cb, 2.0f);
                    g.setColour (Theme::border.withAlpha (clip->isMuted() ? 0.3f : 1.0f));
                    g.drawRoundedRectangle (cb, 2.0f, 1.0f);
                    
                    g.setColour (Theme::textMain.withAlpha (clip->isMuted() ? 0.4f : 0.9f));
                    g.setFont (Theme::uiSize (9.0f));
                    g.drawText (clip->getName(), cb.reduced (4, 1).toNearestInt(), juce::Justification::centredLeft);
                }
            }
            else
            {
                AERION_PROFILE_COUNT ("Timeline.clipsVisited", audio->getClips().size());

                for (auto* clip : audio->getClips())
                {
                    auto start = (float)clip->getPosition().getStart().inSeconds();
                    auto len   = (float)clip->getPosition().getLength().inSeconds();

                    // Real-time recording expansion: if the engine is recording and this track is armed,
                    // check if this clip's file matches an active recording destination.
                    tracktion::RecordingThumbnailManager::Thumbnail::Ptr recThumb;
                    if (audioEngine.isRecording() && audioEngine.isTrackArmed (audio))
                    {
                        if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (clip))
                        {
                            for (auto idi : audioEngine.getEdit().getEditInputDevices().getDevicesForTargetTrack (*audio))
                            {
                                if (idi->isRecordingActive (audio->itemID))
                                {
                                    auto rf = idi->getRecordingFile (audio->itemID);
                                    if (rf == wave->getAudioFile().getFile())
                                    {
                                        recThumb = audioEngine.getEngine().getRecordingThumbnailManager().getThumbnailFor (rf);
                                        double cur = audioEngine.getTransportPosition();
                                        if (cur > start)
                                            len = juce::jmax ((float)len, (float)(cur - start));
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    juce::Rectangle<float> cb(timeToX(start),
                                              (float)y + 2.0f, len * pxPerSec, (float)laneH - 4.0f);

                    // Whole-pixel clip edges, so the frame and waveform are copied
                    // from cached images rather than resampled at fractional offsets.
                    // floor (x + 0.5), not std::round: a scroll by whole pixels must
                    // move every edge by exactly that much (see scrollCachedPixels).
                    cb = cb.withLeft (std::floor (cb.getX() + 0.5f)).withRight (std::floor (cb.getRight() + 0.5f));

                    if (cb.getRight() < kHeaderWidth || cb.getX() > getWidth()) continue;
                    if (! g.clipRegionIntersects (cb.expanded (kClipPaintMargin).getSmallestIntegerContainer())) continue;

                    // Clip styling: subtle shadow, rich gradient, crisp highlight.
                    AERION_PROFILE_SECTION (frameZone, "Timeline.clipFrame");
                    clipFrames.draw (g, cb, tColor, selectedClip == clip, clip->isMuted());
                    AERION_PROFILE_SECTION_END (frameZone);

                    if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*>(clip))
                    {
                        double offset  = wave->getPosition().getOffset().inSeconds();
                        double clipLen = (double) len;

                        // Crop to visible viewport  -  avoids rendering enormous rects
                        // for long clips at high zoom (e.g. a 30s clip at 2000px/s = 60000px wide)
                        auto innerCb    = cb.reduced (2);
                        auto viewportCb = innerCb.getIntersection (
                            juce::Rectangle<float> ((float) kHeaderWidth, innerCb.getY(),
                                                    (float) (getWidth() - kHeaderWidth - kVScrollW),
                                                    innerCb.getHeight()));

                        if (! viewportCb.isEmpty() && innerCb.getWidth() > 0.0f)
                        {
                            AERION_PROFILE_SCOPE ("Timeline.waveform");
                            // innerCb has whole-pixel edges (cb is rounded above).
                            const int fullW = (int) innerCb.getWidth();
                            const int h     = juce::jmax (1, (int) innerCb.getHeight());
                            const int waveX = (int) innerCb.getX();
                            const int waveY = (int) innerCb.getY();

                            juce::int64 samplesNow = 0;
                            if (recThumb != nullptr)
                                samplesNow = recThumb->thumb->getNumSamplesFinished();
                            else
                                samplesNow = audioEngine.getThumbnailForClip (*wave, *this).getNumSamplesFinished();

                            auto& entry = waveformCache[wave->itemID.getRawID()];
                            const bool softwareCtx = isSoftwareContext (g);
                            const float scale = physicalScaleOf (g);
                            if (entry.width != fullW || entry.height != h || entry.software != softwareCtx
                                || ! juce::approximatelyEqual (entry.scale, scale)
                                || ! juce::approximatelyEqual (entry.pxPerSec, pxPerSec)
                                || ! juce::approximatelyEqual (entry.offset, offset)
                                || ! juce::approximatelyEqual (entry.clipLen, clipLen)
                                || entry.samplesLoaded != samplesNow)
                            {
                                entry.tiles.clear();
                                entry.pxPerSec      = pxPerSec;
                                entry.offset        = offset;
                                entry.clipLen       = clipLen;
                                entry.width         = fullW;
                                entry.height        = h;
                                entry.software      = softwareCtx;
                                entry.scale         = scale;
                                entry.samplesLoaded = samplesNow;
                            }

                            const int firstTile = juce::jmax (0, ((int) viewportCb.getX() - waveX) / kWaveTileW);
                            const int lastTile  = juce::jmin ((fullW - 1) / kWaveTileW,
                                                              ((int) std::ceil (viewportCb.getRight()) - 1 - waveX) / kWaveTileW);

                            for (int tile = firstTile; tile <= lastTile; ++tile)
                            {
                                const int tileX = tile * kWaveTileW;
                                const int tileW = juce::jmin (kWaveTileW, fullW - tileX);
                                const juce::Rectangle<int> onScreen (waveX + tileX, waveY, tileW, h);

                                if (! g.clipRegionIntersects (onScreen))
                                    continue;

                                // The tile in physical pixels (the same as logical
                                // at 100 %), cut at whole physical pixels from the
                                // waveform's left edge so neighbouring tiles meet.
                                const int physX0 = juce::roundToInt ((float) tileX * scale);
                                const int physX1 = juce::roundToInt ((float) (tileX + tileW) * scale);
                                const int physW  = juce::jmax (1, physX1 - physX0);
                                const int physH  = juce::jmax (1, juce::roundToInt ((float) h * scale));

                                auto& image = entry.tiles[tile];
                                if (image.isNull())
                                {
                                    const double audioStart    = offset + (double) physX0 / scale * clipLen / (double) fullW;
                                    const double audioDuration = (double) physW / scale * clipLen / (double) fullW;

                                    // Opaque and matched to the renderer, so drawing it is a plain copy.
                                    image = makeImageFor (softwareCtx, juce::Image::RGB, physW, physH, false);
                                    juce::Graphics ig (image);
                                    auto drawWave = [&] ()
                                    {
                                        if (recThumb != nullptr)
                                        {
                                            recThumb->thumb->drawChannels (ig, { 0, 0, physW, physH }, audioStart, audioStart + audioDuration, 1.0f);
                                        }
                                        else
                                        {
                                            auto& thumb = audioEngine.getThumbnailForClip (*wave, *this);
                                            tracktion::TimeRange vRange (tracktion::TimePosition::fromSeconds (audioStart),
                                                                         tracktion::TimeDuration::fromSeconds (audioDuration));
                                            thumb.drawChannel (ig, { 0, 0, physW, physH }, vRange, 0, 1.0f);
                                        }
                                    };
                                    const float thicken = (float) juce::jmax (1, juce::roundToInt (scale));

                                    // Dark background so the waveform is always readable
                                    // regardless of clip colour, then white waveform on top.
                                    ig.setColour (juce::Colours::black);
                                    ig.fillAll();

                                    ig.setColour (juce::Colours::white.withAlpha (1.0f));
                                    drawWave();
                                    {
                                        juce::Graphics::ScopedSaveState ss (ig);
                                        ig.addTransform (juce::AffineTransform::translation (0.0f, -thicken));
                                        drawWave();
                                    }
                                    {
                                        juce::Graphics::ScopedSaveState ss (ig);
                                        ig.addTransform (juce::AffineTransform::translation (0.0f,  thicken));
                                        drawWave();
                                    }
                                }

                                drawPhysicalImage (g, image, scale, { waveX, waveY }, { physX0, 0 });
                            }

                            // Keep the tiles near the view; a long clip at high zoom
                            // would otherwise collect hundreds of them while scrolling.
                            const int keepFrom = firstTile - 8, keepTo = lastTile + 8;
                            for (auto it = entry.tiles.begin(); it != entry.tiles.end();)
                                it = (it->first < keepFrom || it->first > keepTo) ? entry.tiles.erase (it) : std::next (it);
                        }

                        // Fade curves (drawn over full unclipped clip bounds)
                        g.setColour (juce::Colours::white.withAlpha (0.4f));
                        float fi = (float) wave->getFadeIn().inSeconds() * pxPerSec;
                        float fo = (float) wave->getFadeOut().inSeconds() * pxPerSec;
                        if (fi > 0) {
                            juce::Path p;
                            p.startNewSubPath (cb.getX(), cb.getBottom());
                            p.lineTo (cb.getX() + fi, cb.getY());
                            g.strokePath (p, juce::PathStrokeType (1.0f));
                        }
                        if (fo > 0) {
                            juce::Path p;
                            p.startNewSubPath (cb.getRight(), cb.getBottom());
                            p.lineTo (cb.getRight() - fo, cb.getY());
                            g.strokePath (p, juce::PathStrokeType (1.0f));
                        }

                        // Handles (Top-left, Top-right for fades)
                        g.setColour (juce::Colours::white);
                        float hSize = 6.0f;
                        g.fillRect (cb.getX(), cb.getY(), hSize, hSize);
                        g.fillRect (cb.getRight() - hSize, cb.getY(), hSize, hSize);
                    }
                    else if (auto* midi = dynamic_cast<tracktion::MidiClip*>(clip))
                    {
                        // Reaper-style MIDI item: a small piano-roll preview inside the item,
                        // with beat divisions and note bars anchored to their real clip beats.
                        auto innerCb = cb.reduced (3.0f, 16.0f).withTrimmedBottom (2.0f);
                        if (innerCb.getHeight() <= 8.0f)
                            innerCb = cb.reduced (3.0f);

                        g.setColour (juce::Colours::black.withAlpha (clip->isMuted() ? 0.42f : 0.30f));
                        g.fillRoundedRectangle (innerCb, 3.0f);

                        const auto& ts = audioEngine.getEdit().tempoSequence;
                        const double clipStartBeat = ts.toBeats (midi->getPosition().getStart()).inBeats();
                        const double clipEndBeat   = ts.toBeats (midi->getPosition().getEnd()).inBeats();
                        const double clipBeats     = juce::jmax (0.001, clipEndBeat - clipStartBeat);

                        const double minorStep = (snapEnabled && snapInterval > 0.0) ? snapInterval : 0.25;
                        double visibleMinorStep = minorStep;
                        while (visibleMinorStep * pxPerSec * (len / clipBeats) < 4.0)
                            visibleMinorStep *= 2.0;

                        for (double beat = visibleMinorStep; beat < clipBeats; beat += visibleMinorStep)
                        {
                            const float x = innerCb.getX() + (float) (beat / clipBeats) * innerCb.getWidth();
                            const bool isBeat = std::abs (beat - std::round (beat)) < 0.0001;
                            g.setColour (Theme::border.withAlpha (isBeat ? 0.24f : 0.14f));
                            g.drawLine (x, innerCb.getY(), x, innerCb.getBottom(), 1.0f);
                        }

                        for (int i = 1; i < 6; ++i)
                        {
                            const float yLane = innerCb.getY() + innerCb.getHeight() * (float) i / 6.0f;
                            g.setColour (Theme::border.withAlpha (0.08f));
                            g.drawLine (innerCb.getX(), yLane, innerCb.getRight(), yLane, 1.0f);
                        }

                        auto& seq = midi->getSequence();
                        auto notes = seq.getNotes();

                        if (notes.size() > 0)
                        {
                            // Find note range (pitch)
                            int minNote = 127, maxNote = 0;
                            for (auto* note : notes) {
                                minNote = juce::jmin(minNote, note->getNoteNumber());
                                maxNote = juce::jmax(maxNote, note->getNoteNumber());
                            }

                            int noteRange = juce::jmax(12, maxNote - minNote + 1);

                            // Draw notes as small rectangles
                            for (auto* note : notes) {
                                int pitch = note->getNoteNumber();
                                double startBeat = note->getStartBeat().inBeats();
                                double endBeat = note->getEndBeat().inBeats();
                                if (endBeat <= 0.0 || startBeat >= clipBeats)
                                    continue;

                                startBeat = juce::jlimit (0.0, clipBeats, startBeat);
                                endBeat   = juce::jlimit (0.0, clipBeats, endBeat);

                                float noteY = innerCb.getBottom() - (float)(pitch - minNote + 1) / (float) noteRange * innerCb.getHeight();
                                float noteH = juce::jlimit (2.0f, 7.0f, innerCb.getHeight() / (float) noteRange);
                                float noteX = innerCb.getX() + (float)(startBeat / clipBeats) * innerCb.getWidth();
                                float noteW = (float)((endBeat - startBeat) / clipBeats) * innerCb.getWidth();

                                g.setColour (juce::Colours::white.withAlpha (clip->isMuted() ? 0.32f : 0.78f));
                                g.fillRoundedRectangle (noteX, noteY - noteH, juce::jmax (2.0f, noteW), noteH, 1.5f);
                            }
                        }
                        else
                        {
                            // Empty MIDI clip
                            g.setColour(juce::Colours::white.withAlpha(0.26f));
                            g.setFont(Theme::uiSize(9.0f));
                            g.drawText("MIDI", innerCb, juce::Justification::centred);
                        }
                    }

                    {
                        AERION_PROFILE_SCOPE ("Timeline.clipText");
                        g.setColour(Theme::textMain);
                        g.setFont (Theme::uiSize (10.0f));
                        g.drawText(clip->getName(), cb.reduced(6, 2).toNearestInt(), juce::Justification::topLeft);
                    }

                    if (audio != nullptr && (audioEngine.isTrackFrozen (audio) || audioEngine.isTrackFreezing (audio)))
                    {
                        const bool freezing = audioEngine.isTrackFreezing (audio);
                        auto tag = juce::Rectangle<float> (cb.getRight() - 74.0f, cb.getY() + 5.0f, 66.0f, 17.0f);
                        const auto tagColour = freezing ? Theme::meterYellow : juce::Colours::skyblue;
                        g.setColour (juce::Colours::black.withAlpha (0.34f));
                        g.fillRoundedRectangle (tag, 4.0f);
                        g.setColour (tagColour.withAlpha (0.95f));
                        g.drawRoundedRectangle (tag, 4.0f, 1.0f);
                        g.setFont (Theme::uiSize (8.5f).withStyle (juce::Font::bold));
                        g.drawText (freezing ? "FREEZING" : "FROZEN", tag.toNearestInt(), juce::Justification::centred);
                    }
                }
            }
        }

        // Live-recording overlay: while the engine is actively writing to this track,
        // Tracktion only commits the WaveAudioClip on stop, so getClips() is empty during
        // capture. Render the in-flight RecordingThumbnailManager::Thumbnail directly so
        // the user sees the waveform grow live during recording.
        if (audio != nullptr && audioEngine.isRecording())
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (kHeaderWidth, y, getWidth() - kHeaderWidth, rowH);

            for (auto* idi : audioEngine.getEdit().getEditInputDevices().getDevicesForTargetTrack (*audio))
            {
                if (idi == nullptr || ! idi->isRecordingActive (audio->itemID))
                    continue;

                const auto recFile = idi->getRecordingFile (audio->itemID);
                if (recFile == juce::File()) continue;

                auto recThumb = audioEngine.getEngine().getRecordingThumbnailManager().getThumbnailFor (recFile);
                if (recThumb == nullptr || recThumb->thumb == nullptr) continue;

                const double startSec = idi->getPunchInTime (audio->itemID).inSeconds();
                const double nowSec   = audioEngine.getTransportPosition();
                if (nowSec <= startSec) continue;

                const float startX = timeToX ((float) startSec);
                const float endX   = timeToX ((float) nowSec);
                if (endX <= startX) continue;

                juce::Rectangle<float> cb (startX, (float) y + 2.0f, endX - startX, (float) laneH - 4.0f);
                if (cb.getRight() < kHeaderWidth || cb.getX() > getWidth()) continue;

                {
                    auto shadow = cb.translated (0.0f, 1.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.35f));
                    g.fillRoundedRectangle (shadow, 6.0f);
                }

                juce::ColourGradient grad (Theme::recordRed.brighter (0.20f), cb.getX(), cb.getY(),
                                           Theme::recordRed.darker   (0.30f), cb.getX(), cb.getBottom(), false);
                grad.addColour (0.18, Theme::recordRed.brighter (0.30f));
                g.setGradientFill (grad);
                g.fillRoundedRectangle (cb, 6.0f);

                g.setColour (Theme::recordRed.brighter (0.5f).withAlpha (0.95f));
                g.drawRoundedRectangle (cb, 6.0f, 1.5f);

                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.drawLine (cb.getX() + 2.0f, cb.getY() + 1.0f, cb.getRight() - 2.0f, cb.getY() + 1.0f, 1.0f);

                auto innerCb = cb.reduced (2);
                auto viewportCb = innerCb.getIntersection (
                    juce::Rectangle<float> ((float) kHeaderWidth, innerCb.getY(),
                                            (float) (getWidth() - kHeaderWidth - kVScrollW),
                                            innerCb.getHeight()));
                if (! viewportCb.isEmpty() && innerCb.getWidth() > 0.0f)
                {
                    const double clipLen      = nowSec - startSec;
                    const double fullW        = (double) innerCb.getWidth();
                    const double audioStart   = (double) (viewportCb.getX() - innerCb.getX()) * clipLen / fullW;
                    const double audioEnd     = audioStart + (double) viewportCb.getWidth() * clipLen / fullW;

                    juce::Rectangle<int> wfArea (juce::roundToInt (viewportCb.getX()),
                                                 juce::roundToInt (viewportCb.getY()),
                                                 juce::jmax (1, juce::roundToInt (viewportCb.getWidth())),
                                                 juce::jmax (1, juce::roundToInt (viewportCb.getHeight())));

                    auto drawWave = [&] ()
                    {
                        recThumb->thumb->drawChannels (g, wfArea, audioStart, audioEnd, 1.0f);
                    };
                    g.setColour (juce::Colours::white.withAlpha (0.95f));
                    drawWave();
                    {
                        juce::Graphics::ScopedSaveState ss (g);
                        g.addTransform (juce::AffineTransform::translation (0.0f, -1.0f));
                        g.setColour (juce::Colours::white.withAlpha (0.7f));
                        drawWave();
                    }
                    {
                        juce::Graphics::ScopedSaveState ss (g);
                        g.addTransform (juce::AffineTransform::translation (0.0f,  1.0f));
                        g.setColour (juce::Colours::white.withAlpha (0.7f));
                        drawWave();
                    }
                }

                g.setColour (Theme::textMain);
                g.setFont (Theme::uiSize (10.0f).boldened());
                g.drawText ("REC", cb.reduced (6, 2).toNearestInt(), juce::Justification::topLeft);
            }
        }

        // ── Live MIDI recording overlay ──────────────────────────────────────────
        if (audio != nullptr && audioEngine.isRecording())
        {
            bool isMidiTrack = (bool) audio->state.getProperty (IDs::isMidiTrack, false);
            if (isMidiTrack && audioEngine.isTrackArmed (audio))
            {
                tracktion::MidiClip* recMidiClip = nullptr;
                double recStartSec = 0.0;
                for (auto* clip : audio->getClips())
                {
                    if (auto* mc = dynamic_cast<tracktion::MidiClip*> (clip))
                    {
                        double s = mc->getPosition().getStart().inSeconds();
                        if (s <= audioEngine.getTransportPosition() + 0.01)
                        {
                            recMidiClip = mc;
                            recStartSec = s;
                        }
                    }
                }

                if (recMidiClip != nullptr)
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (kHeaderWidth, y, getWidth() - kHeaderWidth, rowH);

                    const double nowSec = audioEngine.getTransportPosition();
                    if (nowSec > recStartSec)
                    {
                        const float startX = timeToX ((float) recStartSec);
                        const float endX   = timeToX ((float) nowSec);
                        juce::Rectangle<float> cb (startX, (float)y + 2.0f,
                                                   endX - startX, (float)laneH - 4.0f);

                        if (cb.getRight() >= kHeaderWidth && cb.getX() <= (float)getWidth())
                        {
                            // Shadow
                            g.setColour (juce::Colours::black.withAlpha (0.35f));
                            g.fillRoundedRectangle (cb.translated (0, 1), 6.0f);

                            // Body — red gradient
                            juce::ColourGradient grad (
                                Theme::recordRed.brighter (0.20f), cb.getX(), cb.getY(),
                                Theme::recordRed.darker   (0.30f), cb.getX(), cb.getBottom(), false);
                            grad.addColour (0.18, Theme::recordRed.brighter (0.30f));
                            g.setGradientFill (grad);
                            g.fillRoundedRectangle (cb, 6.0f);

                            // Outline
                            g.setColour (Theme::recordRed.brighter (0.5f).withAlpha (0.95f));
                            g.drawRoundedRectangle (cb, 6.0f, 1.5f);

                            // Top highlight
                            g.setColour (juce::Colours::white.withAlpha (0.18f));
                            g.drawLine (cb.getX()+2, cb.getY()+1, cb.getRight()-2, cb.getY()+1, 1.0f);

                            // Draw MIDI notes as minimap
                            auto& seq = recMidiClip->getSequence();
                            auto notes = seq.getNotes();
                            if (notes.size() > 0)
                            {
                                int minNote = 127, maxNote = 0;
                                for (auto* n : notes) {
                                    minNote = juce::jmin (minNote, n->getNoteNumber());
                                    maxNote = juce::jmax (maxNote, n->getNoteNumber());
                                }
                                int noteRange = juce::jmax (1, maxNote - minNote + 4);
                                double clipSpan = juce::jmax (0.001, nowSec - recStartSec);
                                auto innerCb = cb.reduced (2);
                                auto& ts = audioEngine.getEdit().tempoSequence;

                                for (auto* n : notes)
                                {
                                    double nsSec = ts.toTime (n->getStartBeat()).inSeconds() - recStartSec;
                                    double neSec = ts.toTime (n->getEndBeat()).inSeconds()   - recStartSec;
                                    int    pitch = n->getNoteNumber();
                                    float noteY  = innerCb.getBottom() - (float)(pitch - minNote + 1) / noteRange * innerCb.getHeight();
                                    float noteH  = innerCb.getHeight() / noteRange;
                                    float noteX  = innerCb.getX() + (float)(nsSec / clipSpan) * innerCb.getWidth();
                                    float noteW  = (float)((neSec - nsSec) / clipSpan) * innerCb.getWidth();
                                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                                    g.fillRect (noteX, noteY - noteH, juce::jmax (1.5f, noteW), noteH);
                                }
                            }

                            // Label
                            g.setColour (Theme::textMain);
                            g.setFont (Theme::uiSize (10.0f).boldened());
                            g.drawText ("REC", cb.reduced (6, 2).toNearestInt(), juce::Justification::topLeft);
                        }
                    }
                }
            }
        }

        y += rowH;

        if (folder != nullptr && ! collapsedFolders.contains (folder->itemID.toString()))
            for (auto* child : folder->getAllAudioSubTracks(false))
                y = drawTrackRow(g, child, topIndex, indent + 16, y);

        return y;
    }

    static void drawFxBadge (juce::Graphics& g, juce::Rectangle<int> b, int numPlugins)
    {
        bool on = numPlugins > 0;
        juce::ColourGradient grad (Theme::active.withAlpha (on ? 0.35f : 0.18f), b.getX(), b.getY(),
                                    Theme::active.withAlpha (on ? 0.18f : 0.08f), b.getX(), b.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b.toFloat(), 3.0f);
        g.setColour (Theme::active.withAlpha (on ? 1.0f : 0.6f));
        g.drawRoundedRectangle (b.toFloat(), 3.0f, 1.0f);
        g.setColour (on ? Theme::active : Theme::textMuted);
        g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
        g.drawText ("FX", b, juce::Justification::centred);
    }

    // ---- Automation lane helpers ----
    // The lane is split: a header column on the left with a Vol/Pan toggle, and
    // the curve area to the right (aligned to the timeline ruler).
    AudioEngineManager::AutomationParamKind paramKindFor (tracktion::Track* t) const
    {
        auto id = t->itemID.toString();
        if (automationParamChoice.contains (id) && automationParamChoice[id] == 1)
            return AudioEngineManager::AutomationParamKind::Pan;
        return AudioEngineManager::AutomationParamKind::Volume;
    }

    juce::Rectangle<int> getAutomationCurveArea (int laneTopY) const
    {
        return { kHeaderWidth, laneTopY,
                 juce::jmax (0, getWidth() - kHeaderWidth - kVScrollW), kAutoLaneH };
    }

    juce::Rectangle<int> getAutomationToggleArea (int laneTopY) const
    {
        // Pill in the lane header column
        return { 14, laneTopY + kAutoLaneH / 2 - 11, 70, 22 };
    }

    float valueToY (tracktion::AutomatableParameter* param,
                    float value,
                    juce::Rectangle<int> area) const
    {
        if (param->getParameterName().contains ("Pan"))
        {
            float norm = juce::jlimit (-1.0f, 1.0f, value);
            return (float) area.getCentreY() - (norm * (float) area.getHeight() * 0.45f);
        }

        // Volume: native param value -> dB via Tracktion formula, then linear fader position -> Y
        float db = (value > 0.0f) ? (20.0f * std::log (value)) + 6.0f : -100.0f;
        float sPos = AudioEngineManager::getFaderPosFromDb (db);
        return (float) area.getBottom() - (sPos * (float) area.getHeight());
    }

    float yToValue (tracktion::AutomatableParameter* param,
                    float y,
                    juce::Rectangle<int> area) const
    {
        if (param->getParameterName().contains ("Pan"))
        {
            float norm = (float) (area.getCentreY() - y) / (area.getHeight() * 0.45f);
            return juce::jlimit (-1.0f, 1.0f, norm);
        }

        // Volume: Y -> linear fader position -> dB -> native param value via Tracktion formula
        float sPos = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - y) / (float) area.getHeight());
        float db = AudioEngineManager::getDbFromFaderPos (sPos);
        return (db > -100.0f) ? std::exp ((db - 6.0f) / 20.0f) : 0.0f;
    }

    void drawAutomationLane (juce::Graphics& g, tracktion::Track* track, int /*topIndex*/, int laneTopY)
    {
        auto curveArea = getAutomationCurveArea (laneTopY);

        // Background bands (header + curve area).
        g.setColour (Theme::bgPanel.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<int> (0, laneTopY, kHeaderWidth, kAutoLaneH));
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRect (curveArea);
        g.setColour (Theme::border);
        g.drawLine (0.0f, (float) laneTopY, (float) getWidth(), (float) laneTopY);
        g.fillRect (kHeaderWidth - 1, laneTopY, 1, kAutoLaneH);

        // Header: param selector + label.
        auto kind = paramKindFor (track);
        bool isPan = (kind == AudioEngineManager::AutomationParamKind::Pan);

        auto pill = getAutomationToggleArea (laneTopY);
        g.setColour (Theme::surface);
        g.fillRoundedRectangle (pill.toFloat(), 3.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (pill.toFloat(), 3.0f, 1.0f);
        auto half = pill.getWidth() / 2;
        juce::Rectangle<int> volR (pill.getX(),         pill.getY(), half,                    pill.getHeight());
        juce::Rectangle<int> panR (pill.getX() + half,  pill.getY(), pill.getWidth() - half,  pill.getHeight());
        g.setColour (! isPan ? Theme::active.withAlpha (0.85f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (volR.toFloat(), 3.0f);
        g.setColour (isPan ? Theme::active.withAlpha (0.85f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (panR.toFloat(), 3.0f);
        g.setFont (Theme::uiSize (9.5f).withStyle (juce::Font::bold));
        g.setColour (! isPan ? juce::Colours::black : Theme::textMuted);
        g.drawText ("VOL", volR, juce::Justification::centred);
        g.setColour (isPan ? juce::Colours::black : Theme::textMuted);
        g.drawText ("PAN", panR, juce::Justification::centred);

        g.setColour (Theme::textMuted);
        g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
        g.drawText ("AUTOMATION", 14, laneTopY + 6, kHeaderWidth - 28, 14, juce::Justification::left);

        auto* param = audioEngine.getAutomationParam (track, kind);
        if (param == nullptr)
        {
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (11.0f));
            g.drawText ("(no parameter for this track)", curveArea, juce::Justification::centred);
            return;
        }

        auto& curve = param->getCurve();
        const int n = curve.getNumPoints();

        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (curveArea);

        // The path runs through the nearest point beyond each edge of the view,
        // or level past the edge before the first and after the last point. It
        // is not anchored at the view's edges, so its shape does not depend on
        // the scroll position (scrollCachedPixels moves these pixels). Points up
        // to edgeMargin outside the view count, as their dots reach into it.
        const float edgeMargin = 8.0f;
        const double tLeft  = xToTime ((float) curveArea.getX() - edgeMargin);
        const double tRight = xToTime ((float) curveArea.getRight() + edgeMargin);
        auto pointTime = [&] (int i) { return curve.getPointTime (i).inSeconds(); };
        auto pointPos  = [&] (int i)
        {
            return juce::Point<float> (timeToX (pointTime (i)), valueToY (param, curve.getPointValue (i), curveArea));
        };

        juce::Path path;

        if (n == 0)
        {
            const float y = valueToY (param, curve.getValueAt (tracktion::TimePosition()), curveArea);
            path.startNewSubPath ((float) curveArea.getX() - edgeMargin, y);
            path.lineTo ((float) curveArea.getRight() + edgeMargin, y);
        }
        else
        {
            int first = 0;
            while (first < n && pointTime (first) < tLeft) ++first;
            int last = n - 1;
            while (last >= 0 && pointTime (last) > tRight) --last;

            const int from = juce::jmax (0, first - 1);
            const int to   = juce::jmin (n - 1, last + 1);

            if (from == first) // no point left of the view: level line in from the edge
                path.startNewSubPath ((float) curveArea.getX() - edgeMargin, pointPos (from).y);
            else
                path.startNewSubPath (pointPos (from));

            for (int i = (from == first ? from : from + 1); i <= to; ++i)
                path.lineTo (pointPos (i));

            if (to == last) // no point right of the view: level line out past the edge
                path.lineTo ((float) curveArea.getRight() + edgeMargin, pointPos (to).y);
        }

        g.setColour (Theme::active.withAlpha (0.7f));
        g.strokePath (path, juce::PathStrokeType (1.6f));

        // Centre line for pan (helps visualise centre).
        if (isPan)
        {
            float yMid = valueToY (param, 0.0f, curveArea);
            g.setColour (Theme::textMuted.withAlpha (0.35f));
            g.drawLine ((float) curveArea.getX(), yMid, (float) curveArea.getRight(), yMid, 1.0f);
        }

        // Points.
        for (int i = 0; i < n; ++i)
        {
            double pt = curve.getPointTime (i).inSeconds();
            if (pt < tLeft || pt > tRight) continue;
            float px = timeToX (pt);
            float py = valueToY (param, curve.getPointValue (i), curveArea);
            const float r = (draggingParam == param && draggingPointIdx == i) ? 5.0f : 3.5f;
            g.setColour (Theme::bgBase);
            g.fillEllipse (px - r - 1.0f, py - r - 1.0f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f);
            g.setColour (Theme::active);
            g.fillEllipse (px - r, py - r, r * 2.0f, r * 2.0f);

            if (draggingParam == param && draggingPointIdx == i)
            {
                float val = curve.getPointValue (i);
                if (isPan)
                {
                    int panPercent = (int)std::round(val * 100.0f);
                    if (panPercent == 0) currentTooltip.text = "C";
                    else if (panPercent < 0) currentTooltip.text = juce::String::formatted ("L %d", -panPercent);
                    else currentTooltip.text = juce::String::formatted ("R %d", panPercent);
                }
                else
                {
                    float db = (val > 0.0f) ? (20.0f * std::log (val)) + 6.0f : -100.0f;
                    if (db < AudioEngineManager::kMinVolumeDb + 1.0f) currentTooltip.text = "-inf dB";
                    else currentTooltip.text = juce::String::formatted ("%+.1f dB", db);
                }

                juce::Font font = Theme::uiSize (12.0f);
                int tw = font.getStringWidth (currentTooltip.text) + 10;
                currentTooltip.bounds = juce::Rectangle<int> ((int)px - tw / 2, (int)py - 24, tw, 18);
                currentTooltip.isValid = true;
            }
        }

        // Current parameter value indicator on the right edge.
        float yNow = valueToY (param, param->getCurrentBaseValue(), curveArea);
        g.setColour (Theme::active.withAlpha (0.45f));
        g.fillRect ((float) (curveArea.getRight() - 4), yNow - 1.0f, 4.0f, 2.0f);
    }

    int hitTestAutomationPoint (tracktion::AutomatableParameter* param,
                                juce::Rectangle<int> curveArea,
                                juce::Point<int> p) const
    {
        if (param == nullptr) return -1;
        auto& curve = param->getCurve();
        const int n = curve.getNumPoints();
        const float radius = 7.0f;
        for (int i = 0; i < n; ++i)
        {
            double pt = curve.getPointTime (i).inSeconds();
            float px = timeToX (pt);
            float py = valueToY (param, curve.getPointValue (i), curveArea);
            if (juce::Point<float> (px, py).getDistanceFrom (p.toFloat()) <= radius)
                return i;
        }
        return -1;
    }

    bool handleAutomationMouseDown (const juce::MouseEvent& e)
    {
        const int targetY = e.y - kRulerH + scrollY;
        auto rows = getVisibleRows();

        for (int rIdx = 0; rIdx < rows.size(); ++rIdx)
        {
            auto& row = rows.getReference (rIdx);
            if (! automationVisibleTracks.contains (row.track->itemID.toString()))
                continue;
            // The automation lane is below the track's own lane.
            const int trackLaneH = getLaneHeight (row.track);
            if (targetY < row.y + trackLaneH || targetY >= row.y + row.height)
                continue;

            const int laneTopScreenY = kRulerH + row.y + trackLaneH - scrollY;

            // Vol/Pan toggle in lane header column.
            if (e.x < kHeaderWidth)
            {
                auto pill = getAutomationToggleArea (laneTopScreenY);
                if (pill.contains (e.getPosition()))
                {
                    auto id = row.track->itemID.toString();
                    int half = pill.getWidth() / 2;
                    bool clickedPan = (e.x >= pill.getX() + half);
                    automationParamChoice.set (id, clickedPan ? 1 : 0);
                    repaint();
                    return true;
                }
                return true; // consume other header-column clicks inside the lane
            }

            auto curveArea = getAutomationCurveArea (laneTopScreenY);
            if (! curveArea.contains (e.getPosition()))
                return true;

            auto* param = audioEngine.getAutomationParam (row.track, paramKindFor (row.track));
            if (param == nullptr)
                return true;

            auto& curve = param->getCurve();
            int hit = hitTestAutomationPoint (param, curveArea, e.getPosition());

            if (e.mods.isPopupMenu())
            {
                if (hit >= 0)
                {
                    curve.removePoint (hit);
                    repaint();
                }
                return true;
            }

            if (hit < 0)
            {
                // Ensure the volume param range is extended before storing the point value.
                if (paramKindFor (row.track) == AudioEngineManager::AutomationParamKind::Volume)
                    audioEngine.ensureVolumeRange (row.track);
                float val = yToValue (param, (float) e.y, curveArea);
                auto t = tracktion::TimePosition::fromSeconds (juce::jmax (0.0, xToTime ((float) e.x)));
                hit = curve.addPoint (t, val, 0.0f);
            }

            param->parameterChangeGestureBegin();
            automationGestureActive = true;
            draggingParam     = param;
            draggingPointIdx  = hit;
            draggingCurveArea = curveArea;
            automationDragStartY   = e.y;
            automationDragStartVal = curve.getPointValue (hit);
            repaint();
            return true;
        }

        return false;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        snapEnabled  = (bool)   projectData.getProjectTree().getProperty (IDs::snapEnabled,  true);
        snapInterval = (double) projectData.getProjectTree().getProperty (IDs::snapInterval, 1.0);
        // Footer / vertical scrollbar  -  don't interfere.
        if (e.y >= laneBottom() || e.x >= getWidth() - kVScrollW) return;

        // A lane's bottom edge in the header column: resize the track (and
        // the other selected tracks when it is selected).
        if (! e.mods.isPopupMenu())
            if (auto* edgeTrack = getLaneEdgeTrackAt (e.getPosition()))
            {
                resizeTrack  = edgeTrack;
                resizeGroup  = getResizeGroup (edgeTrack);
                resizeStartY = e.y;
                resizeStartH = getLaneHeight (edgeTrack);
                return;
            }

        if (activeTool == EditTool::comp)
        {
            auto rows = getVisibleRows();
            int targetY = e.y - kRulerH + scrollY;
            for (auto& row : rows)
            {
                if (targetY >= row.y && targetY < row.y + row.height)
                {
                    if (auto* audio = dynamic_cast<tracktion::AudioTrack*> (row.track))
                    {
                        juce::Array<double> laneEndTimes;
                        for (auto* clip : audio->getClips())
                        {
                            double startT = clip->getPosition().getStart().inSeconds();
                            double endT   = clip->getPosition().getEnd().inSeconds();
                            int laneIdx = 0;
                            while (laneIdx < laneEndTimes.size() && laneEndTimes[laneIdx] > startT) laneIdx++;
                            if (laneIdx == laneEndTimes.size()) laneEndTimes.add (endT);
                            else laneEndTimes.set (laneIdx, endT);

                            float laneH = (float) (getLaneHeight (row.track) - 10) / (float) juce::jmax (1, laneEndTimes.size());
                            float clipY = (float) row.y + 5.0f + (float) laneIdx * laneH;
                            float screenY = (float) kRulerH + clipY - (float) scrollY;
                            juce::Rectangle<float> cb (timeToX (startT), screenY, (float) (endT - startT) * pxPerSec, laneH - 2.0f);
                            
                            if (cb.contains (e.position.toFloat()))
                            {
                                for (auto* other : audio->getClips())
                                    if (other->getPosition().time.overlaps (clip->getPosition().time))
                                        other->setMuted (other != clip);
                                repaint();
                                return;
                            }
                        }
                    }
                }
            }
            return;
        }

        // + Audio / + MIDI / + Folder bar
        if (e.y < kHeaderBarH && e.x < kHeaderWidth)
        {
            if (addTrackBtn.contains(e.getPosition()))  { if (onAddTrack)  onAddTrack();  return; }
            if (addMidiTrackBtn.contains(e.getPosition())) { if (onAddMidiTrack) onAddMidiTrack(); return; }
            if (addFolderBtn.contains(e.getPosition())) { if (onAddFolder) onAddFolder(); return; }
            return;
        }

        // Ruler -> seek / loop / markers
        if (e.y < kRulerH) {
            if (tempoLaneArea().contains (e.getPosition()))
            {
                const int tempoIndex = getTempoNodeAt (e.getPosition());
                if (tempoIndex >= 0)
                {
                    selectedTempoIndex = tempoIndex;
                    if (e.mods.isPopupMenu())
                    {
                        audioEngine.removeTempoAtIndex (tempoIndex);
                        selectedTempoIndex = -1;
                        repaint();
                        return;
                    }

                    dragMode = DragMode::tempoNode;
                    tempoDragStartBpm = audioEngine.getTempoBpm (tempoIndex);
                    tempoDragStartY = e.y;
                    repaint();
                    return;
                }

                return;
            }

            if (timeSigLaneArea().contains (e.getPosition()))
            {
                const int timeSigIndex = getTimeSigNodeAt (e.getPosition());
                if (timeSigIndex >= 0)
                {
                    selectedTimeSigIndex = timeSigIndex;
                    selectedTempoIndex = -1;

                    if (e.mods.isPopupMenu())
                    {
                        if (timeSigIndex > 0)
                            audioEngine.removeTimeSigAtIndex (timeSigIndex);
                        selectedTimeSigIndex = -1;
                        repaint();
                        return;
                    }

                    dragMode = DragMode::timeSigNode;
                    repaint();
                    return;
                }

                if (e.mods.isPopupMenu())
                {
                    showTimeSigMenuAt (getBarStartBeatForX ((float) e.x));
                    return;
                }

                return;
            }

            auto& transport = audioEngine.getEdit().getTransport();
            auto loopRange = transport.getLoopRange();
            float x0 = timeToX (loopRange.getStart().inSeconds());
            float x1 = timeToX (loopRange.getEnd().inSeconds());

            // Hit test markers first (they are on top)
            if (auto* mt = audioEngine.getEdit().getMarkerTrack())
            {
                auto clips = mt->getClips();
                for (auto* clip : clips)
                {
                    if (auto* marker = dynamic_cast<tracktion::MarkerClip*> (clip))
                    {
                        float mx = timeToX (marker->getPosition().getStart().inSeconds());
                        if (std::abs (e.x - mx) < 10.0f) {
                            if (e.mods.isRightButtonDown()) {
                                marker->removeFromParent();
                                repaint();
                                return;
                            }
                            dragMode = DragMode::marker;
                            draggingMarker = marker;
                            return;
                        }
                    }
                }
            }

            if (std::abs (e.x - x0) < 8.0f) {
                dragMode = DragMode::loopStart;
                return;
            }
            if (std::abs (e.x - x1) < 8.0f) {
                dragMode = DragMode::loopEnd;
                return;
            }

            if (e.mods.isAltDown()) {
                double t = xToTime ((float) e.x);
                if (snapEnabled) {
                    auto& ts = audioEngine.getEdit().tempoSequence;
                    auto b = ts.toBeats (tracktion::TimePosition::fromSeconds (t));
                    double sb = std::round (b.inBeats() / snapInterval) * snapInterval;
                    t = ts.toTime (tracktion::BeatPosition::fromBeats (sb)).inSeconds();
                }
                transport.setLoopRange ({ tracktion::TimePosition::fromSeconds (t), tracktion::TimePosition::fromSeconds (t + 0.1) });
                dragMode = DragMode::loopEnd;
                repaint();
                return;
            }

            if (e.mods.isShiftDown()) {
                if (auto* mt = audioEngine.getEdit().getMarkerTrack()) {
                    double t = xToTime ((float) e.x);
                    if (snapEnabled) {
                        auto& ts = audioEngine.getEdit().tempoSequence;
                        auto b = ts.toBeats (tracktion::TimePosition::fromSeconds (t));
                        double sb = std::round (b.inBeats() / snapInterval) * snapInterval;
                        t = ts.toTime (tracktion::BeatPosition::fromBeats (sb)).inSeconds();
                    }
                    juce::String name = juce::String ("Marker ") + juce::String (mt->getClips().size() + 1);
                    mt->insertNewClip (tracktion::TrackItem::Type::marker, name, { tracktion::TimePosition::fromSeconds (t), tracktion::TimePosition::fromSeconds (t + 1.0) }, nullptr);
                    repaint();
                }
                return;
            }

            if (beatRulerArea().contains (e.getPosition()))
            {
                audioEngine.setTransportPosition (juce::jmax (0.0, xToTime ((float) e.x)));
                repaint();
            }
            return;
        }

        if (handleAutomationMouseDown (e))
            return;

        if (e.x < kHeaderWidth)
        {
            int targetY = e.y - kRulerH + scrollY;
            auto rows = getVisibleRows();
            
            for (int i = 0; i < rows.size(); ++i)
            {
                auto& row = rows.getReference (i);
                if (targetY >= row.y && targetY < row.y + row.height)
                {
                    int rowTop = kRulerH + row.y - scrollY;
                    int btnY    = rowTop + 32;
                    int fxY     = btnY + 24;
                    int textX   = 14 + row.indent;
                    auto* clickedTrack = row.track;
                    auto* folder = dynamic_cast<tracktion::FolderTrack*> (row.track);

                    // Chevron hit test for folders
                    if (folder != nullptr)
                    {
                        auto chevronR = juce::Rectangle<int> (textX - 10, rowTop + 10, 10, 10);
                        if (chevronR.contains (e.getPosition()))
                        {
                            juce::String fid = folder->itemID.toString();
                            if (collapsedFolders.contains (fid)) collapsedFolders.removeString (fid);
                            else                                  collapsedFolders.add (fid);
                            updateScrollBar();
                            repaint();
                            return;
                        }
                    }

                    // Right-click  -  context menu.
                    if (e.mods.isPopupMenu())
                    {
                        showTrackContextMenu (clickedTrack, e.getScreenPosition());
                        return;
                    }

                    // M / S / R / A hits - use cached button bounds
                    auto trackId = clickedTrack->itemID.toString();
                    auto it = trackButtonCache.find(trackId);
                    if (it != trackButtonCache.end())
                    {
                        auto& btns = it->second;
                        if (btns.m.contains(e.getPosition())) {
                            audioEngine.toggleTrackMute(clickedTrack); repaint(); return;
                        }
                        if (btns.s.contains(e.getPosition())) {
                            audioEngine.toggleTrackSolo(clickedTrack); repaint(); return;
                        }
                        if (btns.r.contains(e.getPosition())) {
                            audioEngine.setTrackArmed(clickedTrack, ! audioEngine.isTrackArmed(clickedTrack));
                            repaint();
                            return;
                        }
                        if (btns.a.contains(e.getPosition())) {
                            if (automationVisibleTracks.contains (trackId)) automationVisibleTracks.removeString (trackId);
                            else                                           automationVisibleTracks.add (trackId);
                            updateScrollBar();
                            repaint();
                            return;
                        }
                    }

                    // FX hit.
                    if (getLaneHeight (clickedTrack) >= kTrackH
                        && juce::Rectangle<int>(textX, fxY, 76, 20).contains (e.getPosition()))
                    {
                        auto* w = new PluginManagerWindow (clickedTrack, audioEngine);
                        w->toFront(true);
                        return;
                    }

                    auto id = clickedTrack->itemID.toString();
                    if (e.mods.isShiftDown()) {
                        if (selectedIds.contains(id)) selectedIds.removeString(id);
                        else                          selectedIds.add(id);
                    } else {
                        selectedIds.clearQuick();
                        selectedIds.add(id);
                    }
                    if (onSelectionChanged) onSelectionChanged(getSelectedTracks());
                    
                    if (onTrackSelected)    onTrackSelected(clickedTrack);

                    dragging             = true;
                    dragSourceTrack      = clickedTrack;
                    dropInsertBeforeRowIdx = -1;
                    dropFolderTarget     = nullptr;
                    dropFolderAsFirstChild = false;
                    dropDetachToTopLevel = false;
                    dropPreviewY         = -1;
                    repaint();
                    return;
                }
            }
            
            selectedIds.clearQuick();
            if (onSelectionChanged) onSelectionChanged({});
            if (onTrackSelected)    onTrackSelected(nullptr);
            repaint();
            return;
        }

        if (activeTool == EditTool::razor)
        {
            if (auto* clip = getClipAt(e.getPosition()))
            {
                if (isClipTrackFrozenOrFreezing (audioEngine, clip))
                    return;

                if (auto* ct = clip->getClipTrack())
                {
                    double t = xToTime ((float) e.x);
                    if (snapEnabled)
                    {
                        auto& ts = audioEngine.getEdit().tempoSequence;
                        auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (t));
                        double snappedBeats = std::round (beats.inBeats() / snapInterval) * snapInterval;
                        t = ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds();
                    }
                    ct->splitClip (*clip, tracktion::TimePosition::fromSeconds (t));
                    repaint();
                }
            }
            return;
        }

        setSelectedClip (getClipAt (e.getPosition()));
        if (selectedClip)
        {
            dragOffset = selectedClip->getPosition().getStart().inSeconds() - xToTime((float)e.x);

            // Use the same decision as hover so the UI feels "smart tool" consistent.
            dragMode = isClipTrackFrozenOrFreezing (audioEngine, selectedClip)
                ? DragMode::none
                : getSmartToolDragModeFor (*selectedClip, e.getPosition());

            if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (selectedClip))
            {
                if (dragMode == DragMode::fadeLeft)
                    dragStartVal = wave->getFadeIn().inSeconds();
                else if (dragMode == DragMode::fadeRight)
                    dragStartVal = wave->getFadeOut().inSeconds();
            }
            repaint();
        }
        else
        {
            dragMode = DragMode::none;
        }
    }

    void showTrackContextMenu (tracktion::Track* track, juce::Point<int> screenPos)
    {
        auto rows = getVisibleRows();

        // Find the row for this track to check its parent
        tracktion::FolderTrack* parentFolder = nullptr;
        for (auto& row : rows)
            if (row.track == track) { parentFolder = row.parent; break; }

        // Collect available folder targets (exclude the track itself if it is a folder)
        juce::Array<tracktion::FolderTrack*> folders;
        for (auto& row : rows)
            if (auto* f = dynamic_cast<tracktion::FolderTrack*>(row.track))
                if (f != track) folders.add (f);

        juce::PopupMenu m;
        m.addItem (1, "Add Plugin...");
        m.addSeparator();

        if (parentFolder != nullptr)
            m.addItem (3, "Detach from folder");

        if (!folders.isEmpty())
        {
            juce::PopupMenu folderSub;
            for (int i = 0; i < folders.size(); ++i)
                folderSub.addItem (100 + i, folders[i]->getName());
            m.addSubMenu ("Move into folder", folderSub);
        }

        m.addSeparator();
        m.addItem (10, "Add Send to New Bus...");

        if (auto* at = dynamic_cast<tracktion::AudioTrack*> (track))
        {
            const bool freezing = audioEngine.isTrackFreezing (at);
            const bool frozen = audioEngine.isTrackFrozen (at);
            m.addItem (11, freezing ? "Freeze in progress..." : (frozen ? "Unfreeze Track" : "Freeze Track"),
                       ! freezing, false);
        }

        m.addSeparator();

        if (auto* f = dynamic_cast<tracktion::FolderTrack*> (track))
        {
            if (audioEngine.isFolderSubmix (f))
                m.addItem (20, "Convert to Folder (organizational)");
            else
                m.addItem (21, "Convert to Submix");
            m.addSeparator();
        }

        // Also reachable by dragging the lane's bottom edge in the header.
        {
            const int current = getLaneHeight (track);
            juce::PopupMenu heightSub;
            for (int i = 0; i < kNumTrackHeightPresets; ++i)
                heightSub.addItem (30 + i, kTrackHeightPresets[i].name, true, current == kTrackHeightPresets[i].height);
            m.addSubMenu ("Track Height", heightSub);
            m.addSeparator();
        }

        m.addItem (2, "Delete Track");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
            [this, track, parentFolder, folders] (int chosen) {
                if (chosen >= 30 && chosen < 30 + kNumTrackHeightPresets)
                {
                    commitLaneHeights (getResizeGroup (track), kTrackHeightPresets[chosen - 30].height);
                    return;
                }

                if (chosen == 1)
                {
                    auto here = juce::Rectangle<int> (juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition()).expanded (8);
                    PluginPicker::show (audioEngine, here,
                        [this, track] (const juce::PluginDescription& d) {
                            audioEngine.addPluginToTrack (track, d);
                            repaint();
                        });
                }
                else if (chosen == 2)
                {
                    selectedIds.removeString (track->itemID.toString());
                    // Clear selection before delete: clips on this track are destroyed and
                    // selectedClip would otherwise dangle until parentChanged runs.
                    if (selectedClip != nullptr && selectedClip->getTrack() == track)
                        clearSelectedClip();
                    audioEngine.deleteTrack (track);
                    if (onTrackSelected) onTrackSelected (nullptr);
                    repaint();
                }
                else if (chosen == 10)
                {
                    audioEngine.addSendToNewBus (track);
                    repaint();
                }
                else if (chosen == 11)
                {
                    if (auto* at = dynamic_cast<tracktion::AudioTrack*> (track))
                    {
                        if (audioEngine.isTrackFrozen (at))
                            audioEngine.unfreezeTrack (at);
                        else
                            audioEngine.freezeTrack (at);
                    }
                    repaint();
                }
                else if (chosen == 20 || chosen == 21)
                {
                    if (auto* f = dynamic_cast<tracktion::FolderTrack*> (track))
                        audioEngine.setFolderSubmix (f, chosen == 21);
                    repaint();
                }
                else if (chosen == 3)
                {
                    // Detach: move to top level after the parent folder
                    audioEngine.moveTrackAfter (track, parentFolder, nullptr);
                    repaint();
                }
                else if (chosen >= 100 && chosen < 100 + folders.size())
                {
                    auto* targetFolder = folders[chosen - 100];
                    tracktion::Track* lastChild = nullptr;
                    for (auto* child : targetFolder->getAllAudioSubTracks (false))
                        lastChild = child;
                    audioEngine.moveTrackAfter (track, lastChild, targetFolder);
                    repaint();
                }
            });
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (resizeTrack != nullptr)
        {
            setLaneHeights (resizeGroup, resizeStartH + (e.y - resizeStartY));
            return;
        }

        lastMouseX = e.x;
        snapEnabled  = (bool)   projectData.getProjectTree().getProperty (IDs::snapEnabled,  true);
        snapInterval = (double) projectData.getProjectTree().getProperty (IDs::snapInterval, 1.0);
        if (automationGestureActive && draggingParam != nullptr && draggingPointIdx >= 0)
        {
            auto& curve = draggingParam->getCurve();
            if (draggingPointIdx < curve.getNumPoints())
            {
                double t = juce::jmax (0.0, xToTime ((float) e.x));
                float v;
                
                // Delta-based drag: default 0.1 dB/pixel, Shift = 0.01 dB/pixel (fine)
                bool isPanDrag = draggingParam->getParameterName().contains ("Pan");
                int deltaY = e.y - automationDragStartY;

                if (isPanDrag)
                {
                    float sens = e.mods.isShiftDown() ? 0.001f : 0.01f;
                    v = juce::jlimit (-1.0f, 1.0f, automationDragStartVal - (float) deltaY * sens);
                }
                else
                {
                    float sens = e.mods.isShiftDown() ? 0.01f : 0.1f;
                    float startDb = (automationDragStartVal > 0.0f)
                                  ? (20.0f * std::log (automationDragStartVal)) + 6.0f
                                  : AudioEngineManager::kMinVolumeDb;
                    float newDb = juce::jlimit (AudioEngineManager::kMinVolumeDb,
                                               AudioEngineManager::kMaxVolumeDb,
                                               startDb - (float) deltaY * sens);
                    v = (newDb > AudioEngineManager::kMinVolumeDb)
                      ? std::exp ((newDb - 6.0f) / 20.0f) : 0.0f;
                }

                draggingPointIdx = curve.movePoint (draggingPointIdx,
                                                    tracktion::TimePosition::fromSeconds (t),
                                                    v, false);
                repaint();
            }
            return;
        }

        if (dragMode == DragMode::none
            && beatRulerArea().contains (e.getPosition()))
        {
            audioEngine.setTransportPosition (juce::jmax (0.0, xToTime ((float) e.x)));
            repaint();
            return;
        }
        if (dragging && dragSourceTrack != nullptr)
        {
            int virtualY = e.y - kRulerH + scrollY;
            auto rows = getVisibleRows();

            dropInsertBeforeRowIdx = rows.size();  // default: after all rows
            dropFolderTarget       = nullptr;
            dropFolderAsFirstChild = false;
            dropDetachToTopLevel   = false;

            int sourceIndent = 0;
            tracktion::FolderTrack* sourceParent = nullptr;
            for (auto& row : rows)
                if (row.track == dragSourceTrack)
                {
                    sourceParent = row.parent;
                    sourceIndent = row.indent;
                    break;
                }

            if (sourceParent != nullptr && e.x < sourceIndent - 16)
            {
                dropDetachToTopLevel = true;
                dropInsertBeforeRowIdx = -1;
                dropPreviewY         = -1;
                repaint();
                return;
            }

            for (int i = 0; i < rows.size(); ++i)
            {
                auto& row = rows.getReference (i);
                if (virtualY < row.y + row.height)
                {
                    // Hovering over the middle third of a folder -> drop inside it
                    if (auto* f = dynamic_cast<tracktion::FolderTrack*>(row.track))
                    {
                        int midTop = row.y + row.height / 4;
                        int midBot = row.y + row.height * 3 / 4;
                        if (virtualY >= midTop && virtualY < midBot && f != dragSourceTrack)
                        {
                            dropFolderTarget       = f;
                            dropFolderAsFirstChild = (virtualY < midTop + (midBot - midTop) / 3);
                            dropInsertBeforeRowIdx = -1;
                            break;
                        }
                    }
                    // Top half -> insert before; bottom half -> insert after
                    dropInsertBeforeRowIdx = (virtualY < row.y + row.height / 2) ? i : i + 1;
                    break;
                }
            }

            // Compute the screen Y of the insert line
            if (dropInsertBeforeRowIdx >= 0)
            {
                if (rows.isEmpty())
                    dropPreviewY = laneTop();
                else if (dropInsertBeforeRowIdx == 0)
                    dropPreviewY = kRulerH + rows[0].y - scrollY;
                else if (dropInsertBeforeRowIdx >= rows.size())
                    dropPreviewY = kRulerH + rows.getLast().y + rows.getLast().height - scrollY;
                else
                    dropPreviewY = kRulerH + rows[dropInsertBeforeRowIdx].y - scrollY;
            }
            else
            {
                dropPreviewY = -1;
            }

            repaint();
            return;
        }
        if (dragMode == DragMode::loopStart || dragMode == DragMode::loopEnd)
        {
            auto& transport = audioEngine.getEdit().getTransport();
            double t = xToTime ((float) e.x);
            if (snapEnabled) {
                auto& ts = audioEngine.getEdit().tempoSequence;
                auto b = ts.toBeats (tracktion::TimePosition::fromSeconds (t));
                double sb = std::round (b.inBeats() / snapInterval) * snapInterval;
                t = ts.toTime (tracktion::BeatPosition::fromBeats (sb)).inSeconds();
            }
            auto range = transport.getLoopRange();
            if (dragMode == DragMode::loopStart)
                transport.setLoopRange ({ tracktion::TimePosition::fromSeconds (juce::jmin (t, range.getEnd().inSeconds() - 0.1)), range.getEnd() });
            else
                transport.setLoopRange ({ range.getStart(), tracktion::TimePosition::fromSeconds (juce::jmax (t, range.getStart().inSeconds() + 0.1)) });
            repaint();
            return;
        }

        if (dragMode == DragMode::marker && draggingMarker != nullptr)
        {
            double t = xToTime ((float) e.x);
            if (snapEnabled) {
                auto& ts = audioEngine.getEdit().tempoSequence;
                auto b = ts.toBeats (tracktion::TimePosition::fromSeconds (t));
                double sb = std::round (b.inBeats() / snapInterval) * snapInterval;
                t = ts.toTime (tracktion::BeatPosition::fromBeats (sb)).inSeconds();
            }
            draggingMarker->setStart (tracktion::TimePosition::fromSeconds (juce::jmax (0.0, t)), false, true);
            repaint();
            return;
        }

        if (dragMode == DragMode::tempoNode && selectedTempoIndex >= 0)
        {
            auto& ts = audioEngine.getEdit().tempoSequence;
            const double beat = ts.toBeats (tracktion::TimePosition::fromSeconds (juce::jmax (0.0, xToTime ((float) e.x)))).inBeats();
            const double snappedBeat = snapEnabled && snapInterval > 0.0
                ? std::round (beat / snapInterval) * snapInterval
                : beat;

            const double bpmDelta = (double) (tempoDragStartY - e.y) * 0.75;
            const double newBpm = juce::jlimit (20.0, 300.0, tempoDragStartBpm + bpmDelta);
            if (selectedTempoIndex > 0)
            {
                // Clamp so a node can't be dragged across its neighbours (keeps ordering stable).
                const double minGap = 0.0625; // 1/16 beat
                double lo = audioEngine.getTempoStartBeat (selectedTempoIndex - 1) + minGap;
                double hi = (selectedTempoIndex + 1 < audioEngine.getNumTempos())
                                ? audioEngine.getTempoStartBeat (selectedTempoIndex + 1) - minGap
                                : juce::jmax (lo, snappedBeat);
                audioEngine.moveTempoAtIndexToBeat (selectedTempoIndex,
                                                    juce::jlimit (lo, juce::jmax (lo, hi), snappedBeat));
            }
            audioEngine.setTempoBpmAtIndex (selectedTempoIndex, newBpm);
            repaint();
            return;
        }

        if (dragMode == DragMode::timeSigNode && selectedTimeSigIndex >= 0)
        {
            if (selectedTimeSigIndex > 0)
                audioEngine.moveTimeSigAtIndexToBeat (selectedTimeSigIndex, getBarStartBeatForX ((float) e.x));
            repaint();
            return;
        }

        if (selectedClip && dragMode != DragMode::none) {
            if (isClipTrackFrozenOrFreezing (audioEngine, selectedClip))
            {
                dragMode = DragMode::none;
                return;
            }

            double mouseTime = xToTime((float)e.x);

            // Only the clip's old and new footprint change while it is dragged,
            // so repaint those instead of the whole Timeline on every mouse move.
            // The final full repaint happens in mouseUp.
            const bool partialRepaint = isClipEditDrag (dragMode);
            const auto oldClipArea    = partialRepaint ? getClipPaintBounds (*selectedClip)
                                                       : juce::Rectangle<int>();
            const auto oldTooltipArea = currentTooltip.isValid ? currentTooltip.bounds
                                                               : juce::Rectangle<int>();

            if (dragMode == DragMode::move)
            {
                double newStart = mouseTime + dragOffset;
                if (snapEnabled)
                {
                    auto& ts = audioEngine.getEdit().tempoSequence;
                    auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (newStart));
                    double snappedBeats = std::floor (beats.inBeats() / snapInterval + 0.5) * snapInterval;
                    newStart = ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds();
                }
                selectedClip->setStart(tracktion::TimePosition::fromSeconds(juce::jmax(0.0, newStart)), false, true);
            }
            else if (dragMode == DragMode::fadeLeft)
            {
                if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (selectedClip))
                {
                    double newFade = juce::jmax (0.0, mouseTime - wave->getPosition().getStart().inSeconds());
                    if (snapEnabled) {
                        auto& ts = audioEngine.getEdit().tempoSequence;
                        auto b = ts.toBeats (tracktion::TimePosition::fromSeconds (newFade));
                        double sb = std::round (b.inBeats() / snapInterval) * snapInterval;
                        newFade = ts.toTime (tracktion::BeatPosition::fromBeats (sb)).inSeconds();
                    }
                    newFade = juce::jmin (newFade, wave->getPosition().getLength().inSeconds());
                    wave->setFadeIn (tracktion::TimeDuration::fromSeconds (newFade));
                    wave->state.setProperty ("aerionAutoFadeIn", false, nullptr);
                    
                    currentTooltip.text = juce::String (newFade, 2) + "s";
                    currentTooltip.bounds = juce::Rectangle<int> (e.x - 30, e.y - 30, 60, 20);
                    currentTooltip.isValid = true;
                }
            }
            else if (dragMode == DragMode::fadeRight)
            {
                if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (selectedClip))
                {
                    double newFade = juce::jmax (0.0, wave->getPosition().getEnd().inSeconds() - mouseTime);
                    if (snapEnabled) {
                        auto& ts = audioEngine.getEdit().tempoSequence;
                        auto b = ts.toBeats (tracktion::TimePosition::fromSeconds (newFade));
                        double sb = std::round (b.inBeats() / snapInterval) * snapInterval;
                        newFade = ts.toTime (tracktion::BeatPosition::fromBeats (sb)).inSeconds();
                    }
                    newFade = juce::jmin (newFade, wave->getPosition().getLength().inSeconds());
                    wave->setFadeOut (tracktion::TimeDuration::fromSeconds (newFade));
                    wave->state.setProperty ("aerionAutoFadeOut", false, nullptr);

                    currentTooltip.text = juce::String (newFade, 2) + "s";
                    currentTooltip.bounds = juce::Rectangle<int> (e.x - 30, e.y - 30, 60, 20);
                    currentTooltip.isValid = true;
                }
            }
            else if (dragMode == DragMode::trimLeft)
            {
                auto oldEnd = selectedClip->getPosition().getEnd();
                double newStart = mouseTime;
                if (snapEnabled)
                {
                    auto& ts = audioEngine.getEdit().tempoSequence;
                    auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (newStart));
                    double snappedBeats = std::floor (beats.inBeats() / snapInterval + 0.5) * snapInterval;
                    newStart = ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds();
                }
                newStart = juce::jmin(newStart, oldEnd.inSeconds() - 0.01);
                selectedClip->setStart(tracktion::TimePosition::fromSeconds(juce::jmax(0.0, newStart)), true, false);
            }
            else if (dragMode == DragMode::trimRight)
            {
                auto start = selectedClip->getPosition().getStart();
                double newEnd = mouseTime;
                if (snapEnabled)
                {
                    auto& ts = audioEngine.getEdit().tempoSequence;
                    auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (newEnd));
                    double snappedBeats = std::floor (beats.inBeats() / snapInterval + 0.5) * snapInterval;
                    newEnd = ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds();
                }
                double newLen = juce::jmax(0.01, newEnd - start.inSeconds());
                selectedClip->setLength(tracktion::TimeDuration::fromSeconds(newLen), true);
            }

            // selectedClip can be dropped by valueTreeParentChanged during the
            // edit above; fall back to a full repaint rather than guess.
            if (partialRepaint && selectedClip != nullptr)
            {
                repaint (oldClipArea.getUnion (getClipPaintBounds (*selectedClip)));

                if (! oldTooltipArea.isEmpty())
                    repaint (oldTooltipArea);
                if (currentTooltip.isValid)
                    repaint (currentTooltip.bounds);
            }
            else
            {
                repaint();
            }
        }
    }

    bool snapEnabled = true;
    double snapInterval = 1.0;
    EditTool activeTool = EditTool::select;
    tracktion::Clip* selectedClip = nullptr;
    juce::ValueTree selectedClipState;
    bool selectedClipRebindPending = false;
    tracktion::Track* trackBeingRenamed = nullptr;
    juce::TextEditor  trackNameEditor;
    juce::TextEditor  rulerValueEditor;

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (resizeTrack != nullptr)
        {
            for (auto* t : resizeGroup)
                if (getLaneHeight (t) != resizeStartH)
                {
                    audioEngine.getEdit().markAsChanged();
                    break;
                }
            resizeTrack = nullptr;
            resizeGroup.clear();
            hoverResizeTrack = getLaneEdgeTrackAt (e.getPosition());
            setMouseCursor (getMouseCursor());
            return;
        }

        if (automationGestureActive)
        {
            if (draggingParam != nullptr)
                draggingParam->parameterChangeGestureEnd();
            automationGestureActive = false;
            draggingParam    = nullptr;
            draggingPointIdx = -1;
            repaint();
            return;
        }

        if (dragging)
        {
            if (dragSourceTrack != nullptr)
            {
                auto rows = getVisibleRows();

                if (dropDetachToTopLevel)
                {
                    tracktion::FolderTrack* parent = nullptr;
                    for (auto& row : rows)
                        if (row.track == dragSourceTrack) { parent = row.parent; break; }
                    if (parent != nullptr)
                        audioEngine.moveTrackAfter (dragSourceTrack, parent, nullptr);
                }
                else if (dropFolderTarget != nullptr)
                {
                    if (dropFolderAsFirstChild)
                        audioEngine.moveTrackAfter (dragSourceTrack, nullptr, dropFolderTarget);
                    else
                    {
                        tracktion::Track* lastChild = nullptr;
                        for (auto* child : dropFolderTarget->getAllAudioSubTracks (false))
                            lastChild = child;
                        audioEngine.moveTrackAfter (dragSourceTrack, lastChild, dropFolderTarget);
                    }
                }
                else if (dropInsertBeforeRowIdx >= 0)
                {
                    // Determine parent context at the insertion point
                    tracktion::FolderTrack* targetParent =
                        (dropInsertBeforeRowIdx < rows.size()) ? rows[dropInsertBeforeRowIdx].parent : nullptr;

                    // Walk backwards to find the nearest preceding track at the same level
                    tracktion::Track* preceding = nullptr;
                    for (int j = dropInsertBeforeRowIdx - 1; j >= 0; --j)
                    {
                        if (rows[j].parent == targetParent)
                        {
                            preceding = rows[j].track;
                            break;
                        }
                    }

                    // Don't move if nothing changed
                    bool alreadyThere = (preceding == dragSourceTrack) ||
                                        (preceding == nullptr && dropInsertBeforeRowIdx == 0 &&
                                         !rows.isEmpty() && rows[0].track == dragSourceTrack);
                    if (!alreadyThere)
                        audioEngine.moveTrackAfter (dragSourceTrack, preceding, targetParent);
                }
            }

            dragging             = false;
            dragSourceTrack      = nullptr;
            dropInsertBeforeRowIdx = -1;
            dropFolderTarget     = nullptr;
            dropFolderAsFirstChild = false;
            dropDetachToTopLevel = false;
            dropPreviewY         = -1;
            repaint();
            return;
        }

        if (selectedClip && dragMode == DragMode::move)
        {
            if (e.x >= kHeaderWidth && e.y >= kRulerH && e.y < laneBottom())
            {
                const int targetY = e.y - kRulerH + scrollY;
                auto rows = getVisibleRows();
                for (auto& row : rows)
                {
                    if (targetY >= row.y && targetY < row.y + row.height)
                    {
                        if (auto* newTrack = dynamic_cast<tracktion::AudioTrack*>(row.track))
                        {
                            if (newTrack != selectedClip->getTrack())
                                selectedClip->moveTo(*newTrack);
                        }
                        break;
                    }
                }
            }
        }

        // Auto-crossfade any overlaps created by move/trim operations.
        // Keep this simple and conservative: only wave clips on their current track.
        if ((bool) projectData.getProjectTree().getProperty (IDs::autoCrossfadeEnabled, true))
        {
            if (selectedClip != nullptr
                && (dragMode == DragMode::move || dragMode == DragMode::trimLeft || dragMode == DragMode::trimRight))
            {
                if (auto* t = selectedClip->getTrack())
                    applyAutoCrossfadesForTrack (*t);
            }
        }

        // Clip drags repaint only the clip while moving (see mouseDrag). A drop
        // can move the clip to another track or crossfade its neighbours, so
        // bring the whole view up to date once here.
        if (isClipEditDrag (dragMode))
            repaint();

        dragMode = DragMode::none;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        // Double-click a lane's bottom edge: back to the default height.
        if (auto* edgeTrack = getLaneEdgeTrackAt (e.getPosition()))
        {
            commitLaneHeights (getResizeGroup (edgeTrack), kTrackH);
            return;
        }

        if (e.y < kRulerH)
        {
            if (tempoLaneArea().contains (e.getPosition()))
            {
                if (const int labelIndex = getTempoBpmLabelAt (e.getPosition()); labelIndex >= 0)
                {
                    beginTempoBpmEdit (labelIndex, getTempoBpmEditBounds (labelIndex));
                    return;
                }

                if (const int existingNode = getTempoNodeAt (e.getPosition()); existingNode >= 0)
                {
                    selectedTempoIndex = existingNode;
                    beginTempoBpmEdit (existingNode, getTempoBpmEditBounds (existingNode));
                    repaint();
                    return;
                }

                auto& ts = audioEngine.getEdit().tempoSequence;
                const double time = juce::jmax (0.0, xToTime ((float) e.x));
                double beat = ts.toBeats (tracktion::TimePosition::fromSeconds (time)).inBeats();
                if (snapEnabled && snapInterval > 0.0)
                    beat = std::round (beat / snapInterval) * snapInterval;

                const double bpm = audioEngine.getTempoAtPosition (time);
                audioEngine.insertTempoAtBeat (beat, bpm);
                selectedTempoIndex = audioEngine.getNumTempos() - 1;
                beginTempoBpmEdit (selectedTempoIndex, getTempoBpmEditBounds (selectedTempoIndex));
                repaint();
            }
            else if (timeSigLaneArea().contains (e.getPosition()))
            {
                if (const int labelIndex = getTimeSigLabelAt (e.getPosition()); labelIndex >= 0)
                {
                    beginTimeSigEdit (labelIndex, getTimeSigLabelBounds (labelIndex));
                    return;
                }

                const int existingIndex = getTimeSigNodeAt (e.getPosition());
                const double beat = existingIndex >= 0
                    ? audioEngine.getTimeSigStartBeat (existingIndex)
                    : getBarStartBeatForX ((float) e.x);
                showTimeSigMenuAt (beat, existingIndex);
            }
            return;
        }

        if (e.x < kHeaderWidth)
        {
            const int targetY = e.y - kRulerH + scrollY;
            auto rows = getVisibleRows();
            for (auto& row : rows)
            {
                if (targetY >= row.y && targetY < row.y + row.height)
                {
                    trackBeingRenamed = row.track;
                    trackNameEditor.setText (trackBeingRenamed->getName(), false);
                    trackNameEditor.setBounds (8, kRulerH + row.y - scrollY + 8, kHeaderWidth - 80, 24);
                    trackNameEditor.setVisible (true);
                    trackNameEditor.grabKeyboardFocus();
                    trackNameEditor.selectAll();
                    return;
                }
            }
            return;
        }

        const int targetY = e.y - kRulerH + scrollY;
        auto rows = getVisibleRows();
        for (int rIdx = 0; rIdx < rows.size(); ++rIdx)
        {
            auto& row = rows.getReference (rIdx);
            if (targetY < row.y || targetY >= row.y + row.height) continue;

            // Don't add MIDI clips when double-clicking inside an automation lane.
            if (automationVisibleTracks.contains (row.track->itemID.toString())
                && targetY >= row.y + getLaneHeight (row.track))
                return;

            if (auto* a = dynamic_cast<tracktion::AudioTrack*>(row.track))
            {
                if (audioEngine.isTrackFrozen (a) || audioEngine.isTrackFreezing (a))
                    return;

                // Check if this is a MIDI-only track
                bool isMidiTrack = (bool) a->state.getProperty(IDs::isMidiTrack, false);

                // If clicking an existing clip, handle it
                if (auto* existingClip = getClipAt (e.getPosition()))
                {
                    if (auto* midi = dynamic_cast<tracktion::MidiClip*>(existingClip))
                    {
                        // Notify MainComponent about MIDI clip double-click (for embedded editor)
                        if (onMidiClipDoubleClicked)
                            onMidiClipDoubleClicked (*midi);
                        else
                        {
                            // Fallback: open floating window if no callback set
                            auto* win = new PianoRollWindow (*midi, audioEngine.getEdit(), projectData, audioEngine);
                            (void) win;
                        }
                        return;
                    }
                    else if (isMidiTrack && dynamic_cast<tracktion::WaveAudioClip*>(existingClip))
                    {
                        // Delete unwanted wave clips on MIDI tracks
                        existingClip->removeFromParent();
                        repaint();
                        return;
                    }
                }

                // Empty space  -  insert a new 2-bar MIDI clip (only on MIDI tracks)
                if (isMidiTrack)
                {
                    const double t0 = xToTime ((float) e.x);
                    tracktion::TimeRange range (tracktion::TimePosition::fromSeconds (juce::jmax (0.0, t0)),
                                                tracktion::TimeDuration::fromSeconds (2.0));
                    a->insertMIDIClip (range, nullptr);
                    repaint();
                }
            }
            return;
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        bool inRuler = (e.y < kRulerH && e.x >= kHeaderWidth);
        bool ctrlDown = e.mods.isCtrlDown();
        bool shiftDown = e.mods.isShiftDown();

        if (inRuler || ctrlDown)
        {
            double mouseTime = xToTime ((float)e.x);
            double zoomFactor = 1.0 + wheel.deltaY * 0.1;
            pxPerSec = juce::jlimit (1.0, 2000.0, pxPerSec * zoomFactor);
            zoomSlider.setValue (pxPerSec, juce::dontSendNotification);

            scrollPx = pixelsForStartTime (mouseTime - (e.x - kHeaderWidth) / pxPerSec);

            // Now, not on the async refresh: a scroll arriving first must not
            // reuse pixels drawn at the old zoom.
            repaint();
            requestTimelineRefresh (true);
        }
        else if (shiftDown)
        {
            scrollTo (pixelsForStartTime (getStartTime() - wheel.deltaY * (100.0 / pxPerSec)), scrollY);
            updateScrollBar();
        }
        else
        {
            // Default vertical scroll through the track list.
            scrollTo (scrollPx, clampScrollY (scrollY - (int) (wheel.deltaY * 60.0)));
            updateScrollBar();
        }
    }

    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override
    {
        // mouseDrag repaints just the dragged clip's area, and mouseUp repaints
        // everything; a full refresh here would undo that on every mouse move.
        if (isClipEditDrag (dragMode) && selectedClipState.isValid()
            && (v == selectedClipState || v.isAChildOf (selectedClipState)))
            return;

        if (i == IDs::snapEnabled)
            snapEnabled = v.getProperty (i);
        else if (i == IDs::snapInterval)
            snapInterval = v.getProperty (i);

        requestTimelineRefresh (i != IDs::snapEnabled && i != IDs::snapInterval);
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { requestTimelineRefresh (true); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { requestTimelineRefresh (true); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { requestTimelineRefresh (true); }
    void valueTreeParentChanged (juce::ValueTree& tree) override
    {
        if (tree != selectedClipState)
            return;

        // Tracktion clip moves briefly detach then re-parent the ValueTree. Drop the
        // raw pointer immediately (object may be destroyed when this returns), then
        // asynchronously recover selection if the node was reattached.
        if (! selectedClipState.getParent().isValid())
        {
            selectedClip = nullptr;
            dragMode = DragMode::none;
            selectedClipRebindPending = true;

            juce::Component::SafePointer<Timeline> safe (this);
            const auto state = selectedClipState;
            juce::MessageManager::callAsync ([safe, state]
            {
                if (safe == nullptr || safe->selectedClipState != state
                    || ! safe->selectedClipRebindPending)
                    return;

                safe->selectedClipRebindPending = false;

                if (state.getParent().isValid())
                {
                    safe->selectedClip = safe->findClipWithState (state);
                    return;
                }

                if (safe->selectedClipState.isValid())
                    safe->selectedClipState.removeListener (safe.getComponent());
                safe->selectedClipState = {};
            });
            return;
        }

        if (selectedClipRebindPending)
        {
            selectedClipRebindPending = false;
            selectedClip = findClipWithState (selectedClipState);
        }
    }

    void editStateChanged() override { requestTimelineRefresh (false); }

private:
    void requestTimelineRefresh (bool updateLayout)
    {
        pendingScrollUpdate = pendingScrollUpdate || updateLayout;
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        if (pendingScrollUpdate)
        {
            pendingScrollUpdate = false;
            updateScrollBar();
        }

        repaint();
    }

    tracktion::Clip* getClipAt(juce::Point<int> p)
    {
        if (p.x < kHeaderWidth || p.y < kRulerH || p.y >= laneBottom()) return nullptr;
        const int targetY = p.y - kRulerH + scrollY;
        auto rows = getVisibleRows();
        for (int rIdx = 0; rIdx < rows.size(); ++rIdx)
        {
            auto& row = rows.getReference (rIdx);
            if (targetY < row.y || targetY >= row.y + row.height) continue;
            // Ignore clicks inside the track's automation lane area.
            if (automationVisibleTracks.contains (row.track->itemID.toString())
                && targetY >= row.y + getLaneHeight (row.track))
                return nullptr;
            if (auto* a = dynamic_cast<tracktion::AudioTrack*> (row.track)) {
                double time = xToTime ((float) p.x);
                for (auto* c : a->getClips())
                    if (c->getPosition().time.contains (tracktion::TimePosition::fromSeconds (time)))
                        return c;
            }
            return nullptr;
        }
        return nullptr;
    }

    // -- File-drag ghost state ------------------------------------------------
    bool              fileDragActive        = false;
    juce::StringArray fileDragFiles;
    double            fileDragSnappedTime   = 0.0;
    int               fileDragTargetRowIdx  = -1;   // -1 = drop creates new track
    double            fileDragPreviewLength = 0.0;  // sum of dragged file durations (seconds)

    // -- Plugin-drag state (DragAndDropTarget from Browser) ------------------
    bool pluginDragActive    = false;
    int  pluginDragTargetRow = -1;

    AudioEngineManager& audioEngine;
    ProjectData& projectData;
    int lastMouseX = -1;
    juce::StringArray selectedIds;
    juce::StringArray automationVisibleTracks;
    juce::StringArray collapsedFolders;
    juce::HashMap<juce::String, int> automationParamChoice;  // 0=Volume, 1=Pan
    tracktion::AutomatableParameter* draggingParam = nullptr;
    int draggingPointIdx = -1;
    juce::Rectangle<int> draggingCurveArea;
    float automationDragStartVal = 0.0f;
    int   automationDragStartY   = 0;
    bool automationGestureActive = false;
    bool dragging = false;
    tracktion::Track*      dragSourceTrack      = nullptr;
    int                    dropInsertBeforeRowIdx = -1;
    tracktion::FolderTrack* dropFolderTarget    = nullptr;
    bool                    dropFolderAsFirstChild = false;
    bool                    dropDetachToTopLevel = false;
    int  dropPreviewY  = -1;
    juce::Rectangle<int> addTrackBtn, addMidiTrackBtn, addFolderBtn;
    bool pendingScrollUpdate = false;

    // Cache track button bounds keyed by track itemID string
    struct TrackButtonBounds {
        juce::Rectangle<int> m, s, r, a;  // mute, solo, arm, automation
    };
    std::map<juce::String, TrackButtonBounds> trackButtonCache;

    // Horizontal scroll position in whole pixels at the current zoom: the time
    // at the lane area's left edge is scrollPx / pxPerSec.
    double scrollPx  = 0.0;
    double pxPerSec  = 100.0;
    int    scrollY   = 0;
    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar   { true };
    juce::Slider    zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    struct TooltipInfo
    {
        juce::String text;
        juce::Rectangle<int> bounds;
        bool isValid = false;
    };
    TooltipInfo currentTooltip;

    // Per-clip waveform images, keyed by Tracktion EditItemID raw value. A
    // clip's waveform is cut into tiles kWaveTileW pixels wide, counted from the
    // clip's left edge, and only tiles on screen are rendered. Because tiles are
    // anchored to the clip rather than to the visible area, scrolling moves a
    // waveform without changing a single pixel of it, and newly scrolled-in
    // tiles are the only ones rendered. Any change of zoom, clip size, trim or
    // loaded audio drops the clip's tiles.
    static constexpr int kWaveTileW = 256;

    struct WaveformCacheEntry {
        std::map<int, juce::Image> tiles;      // tile index -> image
        double        pxPerSec      = 0.0;
        double        offset        = 0.0;     // clip offset into the source, seconds
        double        clipLen       = 0.0;
        int           width         = 0;       // whole waveform, pixels
        int           height        = 0;
        juce::int64   samplesLoaded = -1;
        bool          software      = false;   // image type matches the renderer
        float         scale         = 1.0f;    // tiles are in physical pixels at this scale
    };
    std::map<uint64_t, WaveformCacheEntry> waveformCache;
    ClipFrameCache clipFrames;   // nine-slice clip bodies, see UI/ClipFrame.h

    // -- Private helpers ------------------------------------------------------

    void updateFileDragState (int x, int y)
    {
        // Time position (snapped to beat grid if snap is on)
        double rawTime = xToTime ((float) x);
        if (snapEnabled)
        {
            auto& ts = audioEngine.getEdit().tempoSequence;
            auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (rawTime));
            double snapped = std::round (beats.inBeats() / snapInterval) * snapInterval;
            rawTime = ts.toTime (tracktion::BeatPosition::fromBeats (snapped)).inSeconds();
        }
        fileDragSnappedTime = juce::jmax (0.0, rawTime);

        // Which track row is under the cursor?
        const int virtualY = y - kRulerH + scrollY;
        auto rows = getVisibleRows();
        fileDragTargetRowIdx = -1;
        for (int i = 0; i < rows.size(); ++i)
        {
            if (virtualY >= rows[i].y && virtualY < rows[i].y + rows[i].height)
            {
                // Only land on audio tracks; folders and others -> create new track
                if (dynamic_cast<tracktion::AudioTrack*> (rows[i].track) != nullptr)
                    fileDragTargetRowIdx = i;
                break;
            }
        }

        // Ghost clip width = sum of dragged audio file durations
        fileDragPreviewLength = 0.0;
        for (auto& s : fileDragFiles)
        {
            juce::File f (s);
            if (f.existsAsFile())
            {
                tracktion::AudioFile af (audioEngine.getEngine(), f);
                double len = af.getLength();
                if (len > 0.0) fileDragPreviewLength += len;
            }
        }
        if (fileDragPreviewLength <= 0.0)
            fileDragPreviewLength = 2.0; // fallback width so ghost is visible
    }

    void updatePluginDragTarget (int x, int y)
    {
        if (x >= kHeaderWidth || y < kRulerH || y >= laneBottom())
        {
            pluginDragTargetRow = -1;
            return;
        }
        const int virtualY = y - kRulerH + scrollY;
        auto rows = getVisibleRows();
        pluginDragTargetRow = -1;
        for (int i = 0; i < rows.size(); ++i)
            if (virtualY >= rows[i].y && virtualY < rows[i].y + rows[i].height)
                { pluginDragTargetRow = i; break; }
    }
};

//==============================================================================
// The playhead, on its own transparent layer above the Timeline and the same
// size. It is a sibling of the Timeline, not a child: moving it makes the
// parent redraw the old and new strips, which draws the Timeline from its
// cached layer instead of re-running Timeline::paint.
class TimelinePlayheadOverlay : public juce::Component
{
public:
    TimelinePlayheadOverlay (Timeline& t, AudioEngineManager& ae) : timeline (t), audioEngine (ae)
    {
        setInterceptsMouseClicks (false, false);
    }

    /** Call once per display frame: repaints only when the playhead moved to
        another pixel, whether from playback, a locate, a scroll or a zoom. */
    void update()
    {
        const int x = juce::roundToInt (timeline.timeToX (audioEngine.getTransportPosition()));
        if (x == lastX)
            return;

        repaintStrip (lastX);
        repaintStrip (x);
        lastX = x;
    }

    void paint (juce::Graphics& g) override
    {
        const float x = timeline.timeToX (audioEngine.getTransportPosition());
        if (x <= (float) Timeline::kHeaderWidth || x >= (float) (getWidth() - Timeline::kVScrollW))
            return;

        // Drawn over the lanes but not over the footer.
        g.setColour (Theme::playhead);
        g.drawLine (x, 0.0f, x, (float) (getHeight() - Timeline::kFooterH), 1.5f);
        juce::Path head;
        head.addTriangle (x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 10.0f);
        g.fillPath (head);
    }

private:
    // Wide enough for the 12 px head and anti-aliased line edges.
    void repaintStrip (int x)
    {
        if (x > -9000)
            repaint (x - 8, 0, 16, getHeight());
    }

    Timeline& timeline;
    AudioEngineManager& audioEngine;
    int lastX = -10000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelinePlayheadOverlay)
};
