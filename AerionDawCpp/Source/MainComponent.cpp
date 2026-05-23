#include "MainComponent.h"

namespace te = tracktion;

MainComponent::MainComponent()
{
    const auto ctorStartMs = juce::Time::getMillisecondCounterHiRes();

    juce::LookAndFeel::setDefaultLookAndFeel (&metalLookAndFeel);
    tooltipWindow = std::make_unique<AerionTooltipWindow> (this, 500);

    // Ensure default project directory exists
    juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Aerion Projects").createDirectory();

    addAndMakeVisible (menuBar);
    addAndMakeVisible (toolbar);
    addAndMakeVisible (inspector);
    addAndMakeVisible (browser);
    addAndMakeVisible (timeline);
    addAndMakeVisible (mixer);
    addAndMakeVisible (transport);
    addAndMakeVisible (mixerResizer);
    addAndMakeVisible (inspectorToggle);
    addAndMakeVisible (browserToggle);

    inspectorToggle.onClick = [this] {
        toolbar.inspectorVisible = !inspectorToggle.collapsed;
        toolbar.repaint();
        resized(); 
    };
    browserToggle.onClick   = [this] {
        toolbar.browserVisible = !browserToggle.collapsed;
        toolbar.repaint();
        resized(); 
    };

    toolbar.onToggleInspector = [this] {
        inspectorToggle.collapsed = !toolbar.inspectorVisible;
        resized();
    };

    toolbar.onToggleBrowser = [this] {
        browserToggle.collapsed = !toolbar.browserVisible;
        resized();
    };

    projectData.getProjectTree().addListener (this);

    audioEngine.addListener (this);
    attachToCurrentEditState();
    addKeyListener (this);
    setWantsKeyboardFocus (true);

    // Defer plugin scan so the main window can appear before the cache load
    // and disk scan starts. The scan runs on a background thread and fires
    // onScanFinished / broadcastChange when done.
    if (audioEngine.shouldRunStartupScan())
        juce::MessageManager::callAsync ([this] { audioEngine.scanPlugins(); });

    // Plugins: add only by dragging from the Browser onto a track header or mixer strip
    // (Browser::mouseDrag). Single-click no longer inserts onto the selection.

    browser.onRescanRequested = [this] { audioEngine.scanPlugins(); browser.repaint(); };

    mixer.onDetachRequested = [this] {
        if (mixer.detached) reattachMixer();
        else                detachMixer();
    };

    browser.onFilePicked = [this] (const juce::File& f) {
        // Selection only for preview/picking; do nothing on single click besides what Browser already does
    };

    browser.onFileDoubleClicked = [this] (const juce::File& f) {
        auto sel = timeline.getSelectedTracks();
        tracktion::AudioTrack* targetTrack = nullptr;
        if (! sel.isEmpty())
            targetTrack = dynamic_cast<tracktion::AudioTrack*> (sel[0]);

        double pos = audioEngine.getTransportPosition();
        if (targetTrack != nullptr) {
            if (audioEngine.isTrackFrozen(targetTrack)) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Track Frozen", "Cannot add clips to frozen tracks. Unfreeze the track first.");
                return;
            }
            audioEngine.insertAudioClipOnTrack (targetTrack, f, pos);
        } else {
            audioEngine.importAudioFileAtPosition (f, pos);
        }

        timeline.repaint();
        mixer.repaint();
    };

    driveClient.onFileDownloaded = [this] (const juce::File& f) {
        audioEngine.importAudioFile (f);
        timeline.repaint();
        mixer.repaint();
    };

    browser.setDriveClient (&driveClient);

    toolbar.onToggleSnap = [this] {
        projectData.getProjectTree().setProperty (IDs::snapEnabled, ! (bool)projectData.getProjectTree().getProperty (IDs::snapEnabled), nullptr);
    };

    toolbar.onSnapIntervalChanged = [this] (double interval) {
        projectData.getProjectTree().setProperty (IDs::snapInterval, interval, nullptr);
    };

    toolbar.onToggleAutoCrossfade = [this]
    {
        auto& tree = projectData.getProjectTree();
        tree.setProperty (IDs::autoCrossfadeEnabled,
                          ! (bool) tree.getProperty (IDs::autoCrossfadeEnabled, true),
                          nullptr);
    };

    toolbar.onToggleMetronome = [this] {
        audioEngine.toggleMetronome();
    };

    toolbar.onShowMetronomeSettings = [this] {
        auto popup = std::make_unique<MetronomeSettingsPopup> (audioEngine);
        auto bounds = toolbar.localAreaToGlobal (toolbar.getClickBtnBounds());
        juce::CallOutBox::launchAsynchronously (std::move (popup), bounds, this);
    };

    toolbar.onToolChanged = [this] (EditTool t) {
        timeline.activeTool = t;
    };

    toolbar.onPunchChanged   = [this] (bool on)   { audioEngine.setPunchEnabled (on); };
    toolbar.onPdcChanged     = [this] (bool on)   { audioEngine.setLatencyCompensationEnabled (on); };
    toolbar.onCountInChanged = [this] (int bars)  { audioEngine.setCountInMode (bars); };

    menuBar.recentProjects   = &audioEngine.getRecentProjects();
    menuBar.onNew      = [this] { createNewProject(); };
    menuBar.onOpen     = [this] { openProject(); };
    menuBar.onOpenRecent = [this] (juce::File f)
    {
        auto doOpen = [this, f]()
        {
            detachFromObservedEditState();
            audioEngine.loadProject (f, &projectData);
            attachToCurrentEditState();
            aiManager.setEdit (audioEngine.getEdit());
            currentProjectFile = f;
            hasUnsavedChanges  = false;
            audioEngine.getRecentProjects().addFile (f);
            updateTitleBar();
            syncToolbarFromEngine();
            projectData.syncWithEngine(audioEngine.getEdit());
            syncMenuBarState();
            mixer.repaint();
            timeline.repaint();
        };

        if (!hasUnsavedChanges) { doOpen(); return; }

        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withTitle("Open Recent")
                .withMessage("Save changes to the current project?")
                .withButton("Cancel")
                .withButton("Discard")
                .withButton("Save"),
            [this, doOpen](int result) {
                if (result == 0) return;
                if (result == 2 && currentProjectFile.existsAsFile())
                {
                    audioEngine.saveProject(currentProjectFile, &projectData);
                    hasUnsavedChanges = false;
                    updateTitleBar();
                }
                if (result != 0) doOpen();
            });
    };
    menuBar.onClearRecent   = [this] { audioEngine.clearRecentProjects(); };
    menuBar.onCollectSaveAs = [this] { collectAndSaveAs(); };
    menuBar.onSave     = [this] { saveProject(); };
    menuBar.onSaveAs   = [this] { saveProjectAs(); };
    menuBar.onImport   = [this] { importAudioFile(); };
    menuBar.onExportMixdown = [this] { exportMixdown(); };
    menuBar.onSettings = [this] { showAudioSettings(); };

    menuBar.onUndo = [this] { audioEngine.undo(); };
    menuBar.onRedo = [this] { audioEngine.redo(); };

    menuBar.onToggleMetronome       = [this] { audioEngine.toggleMetronome(); transport.repaint(); };
    menuBar.onShowMetronomeSettings = [this] {
        auto popup = std::make_unique<MetronomeSettingsPopup> (audioEngine);
        juce::CallOutBox::launchAsynchronously (std::move (popup), menuBar.getScreenBounds(), this);
    };
    menuBar.onToggleSnap = [this] {
        projectData.getProjectTree().setProperty (IDs::snapEnabled,
            ! (bool) projectData.getProjectTree().getProperty (IDs::snapEnabled), nullptr);
    };
    menuBar.onSnapIntervalChanged = [this] (double v) {
        projectData.getProjectTree().setProperty (IDs::snapInterval, v, nullptr);
    };
    menuBar.onCountInChanged = [this] (int bars) {
        audioEngine.setCountInMode (bars);
        syncToolbarFromEngine();
    };

    menuBar.onAddAudioTrack  = [this] { audioEngine.addAudioTrack();   timeline.repaint(); mixer.repaint(); };
    menuBar.onAddMidiTrack   = [this] { audioEngine.addMidiTrack();    timeline.repaint(); mixer.repaint(); };
    menuBar.onAddFolderTrack = [this] { audioEngine.addFolderTrack(); audioEngine.syncFolderRouting(); timeline.repaint(); mixer.repaint(); };
    menuBar.onDeleteTrack    = [this] {
        for (auto* t : timeline.getSelectedTracks()) audioEngine.deleteTrack (t);
        syncInspectorToTrack (nullptr);
        timeline.repaint(); mixer.repaint();
    };
    menuBar.onToggleTrackArm  = [this] {
        auto sel = timeline.getSelectedTracks();
        if (sel.isEmpty()) return;
        auto* t = sel[0];
        audioEngine.setTrackArmed (t, ! audioEngine.isTrackArmed (t));
        syncInspectorToTrack (t);
        timeline.repaint();
    };
    menuBar.onToggleTrackMute = [this] {
        auto sel = timeline.getSelectedTracks();
        if (! sel.isEmpty()) { audioEngine.toggleTrackMute (sel[0]); timeline.repaint(); }
    };
    menuBar.onToggleTrackSolo = [this] {
        auto sel = timeline.getSelectedTracks();
        if (! sel.isEmpty()) { audioEngine.toggleTrackSolo (sel[0]); timeline.repaint(); }
    };

    menuBar.onNudgeLeft  = [this] {
        if (auto* clip = timeline.selectedClip)
        {
            double iv = (bool) projectData.getProjectTree().getProperty (IDs::snapEnabled)
                        ? (double) projectData.getProjectTree().getProperty (IDs::snapInterval) : 0.1;
            auto& ts = audioEngine.getEdit().tempoSequence;
            auto b = ts.toBeats (clip->getPosition().getStart());
            clip->setStart (ts.toTime (tracktion::BeatPosition::fromBeats (juce::jmax (0.0, b.inBeats() - iv))), false, true);
            timeline.repaint();
        }
    };
    menuBar.onNudgeRight = [this] {
        if (auto* clip = timeline.selectedClip)
        {
            double iv = (bool) projectData.getProjectTree().getProperty (IDs::snapEnabled)
                        ? (double) projectData.getProjectTree().getProperty (IDs::snapInterval) : 0.1;
            auto& ts = audioEngine.getEdit().tempoSequence;
            auto b = ts.toBeats (clip->getPosition().getStart());
            clip->setStart (ts.toTime (tracktion::BeatPosition::fromBeats (b.inBeats() + iv)), false, true);
            timeline.repaint();
        }
    };
    menuBar.onTrimLeft   = [this] {
        if (auto* clip = timeline.selectedClip)
        {
            double iv = (bool) projectData.getProjectTree().getProperty (IDs::snapEnabled)
                        ? (double) projectData.getProjectTree().getProperty (IDs::snapInterval) : 0.1;
            auto& ts  = audioEngine.getEdit().tempoSequence;
            auto start = clip->getPosition().getStart();
            auto bStart = ts.toBeats (start);
            auto bEnd   = ts.toBeats (clip->getPosition().getEnd());
            auto newEnd = tracktion::BeatPosition::fromBeats (juce::jmax (bStart.inBeats() + 0.01, bEnd.inBeats() - iv));
            clip->setLength (ts.toTime (newEnd) - start, true);
            timeline.repaint();
        }
    };
    menuBar.onTrimRight  = [this] {
        if (auto* clip = timeline.selectedClip)
        {
            double iv = (bool) projectData.getProjectTree().getProperty (IDs::snapEnabled)
                        ? (double) projectData.getProjectTree().getProperty (IDs::snapInterval) : 0.1;
            auto& ts  = audioEngine.getEdit().tempoSequence;
            auto start = clip->getPosition().getStart();
            auto bStart = ts.toBeats (start);
            auto bEnd   = ts.toBeats (clip->getPosition().getEnd());
            auto newEnd = tracktion::BeatPosition::fromBeats (juce::jmax (bStart.inBeats() + 0.01, bEnd.inBeats() + iv));
            clip->setLength (ts.toTime (newEnd) - start, true);
            timeline.repaint();
        }
    };
    menuBar.onDeleteEvent = [this] {
        if (timeline.selectedClip != nullptr)
        {
            timeline.selectedClip->removeFromParent();
            timeline.selectedClip = nullptr;
            timeline.repaint();
        }
    };

    menuBar.onRescanPlugins = [this] { audioEngine.scanPlugins(); browser.repaint(); };
    menuBar.onTogglePdc     = [this] {
        audioEngine.setLatencyCompensationEnabled (! audioEngine.isLatencyCompensationEnabled());
        syncToolbarFromEngine();
    };

    menuBar.onToggleAutoCrossfade = [this]
    {
        auto& tree = projectData.getProjectTree();
        tree.setProperty (IDs::autoCrossfadeEnabled,
                          ! (bool) tree.getProperty (IDs::autoCrossfadeEnabled, true),
                          nullptr);
    };

    menuBar.onAutoCrossfadeMaxChanged = [this] (int ms)
    {
        auto& tree = projectData.getProjectTree();
        tree.setProperty (IDs::autoCrossfadeMaxMs, ms, nullptr);
    };

    menuBar.onPlay      = [this] { if (audioEngine.isPlaying()) audioEngine.stop(); else audioEngine.play(); transport.repaint(); };
    menuBar.onStop      = [this] { audioEngine.stop(); transport.repaint(); };
    menuBar.onRecord    = [this] { audioEngine.record(); transport.repaint(); };
    menuBar.onGoToStart = [this] {
        bool wasPlaying = audioEngine.isPlaying();
        audioEngine.setTransportPosition (0.0);
        timeline.scrollViewToBarOne();
        if (wasPlaying)
            audioEngine.play();
        timeline.repaint();
        transport.repaint();
    };
    menuBar.onToggleLoop  = [this] { audioEngine.toggleLoop(); transport.repaint(); };
    menuBar.onTogglePunch = [this] {
        audioEngine.setPunchEnabled (! audioEngine.isPunchEnabled());
        syncToolbarFromEngine();
    };

    menuBar.onToggleInspector = [this] {
        inspectorToggle.collapsed = ! inspectorToggle.collapsed;
        toolbar.inspectorVisible  = ! inspectorToggle.collapsed;
        toolbar.repaint();
        resized();
    };
    menuBar.onToggleBrowser = [this] {
        browserToggle.collapsed = ! browserToggle.collapsed;
        toolbar.browserVisible  = ! browserToggle.collapsed;
        toolbar.repaint();
        resized();
    };
    menuBar.onToggleMixerDetach = [this] {
        if (mixer.detached) reattachMixer();
        else                detachMixer();
    };
    menuBar.onShowKeyboardShortcuts = [this] {
        KeyboardShortcutsDialog::launch (audioEngine.getKeymap(), audioEngine.getUserSettings());
    };

    menuBar.onBeforeMenuOpen = [this] { syncMenuBarState(); };

    timeline.onAddTrack = [this]
    {
        audioEngine.addAudioTrack();
        timeline.repaint();
        mixer.repaint();
    };

    timeline.onAddMidiTrack = [this]
    {
        auto* track = audioEngine.addMidiTrack();
        if (track != nullptr)
        {
            double pos = audioEngine.getTransportPosition();
            tracktion::TimeRange range (tracktion::TimePosition::fromSeconds (pos),
                                        tracktion::TimeDuration::fromSeconds (2.0));
            track->insertMIDIClip (range, nullptr);
        }
        timeline.repaint();
        mixer.repaint();
    };

    timeline.onAddFolder = [this]
    {
        auto sel = timeline.getSelectedTracks();
        if (! sel.isEmpty()) audioEngine.groupTracks (sel);
        else                  audioEngine.addFolderTrack();
        audioEngine.syncFolderRouting();
        timeline.repaint();
        mixer.repaint();
    };

    timeline.onImportFile = [this] (const juce::File& f)
    {
        audioEngine.importAudioFileAtPosition (f, 0.0); // legacy menu-import path
        timeline.repaint();
        mixer.repaint();
    };

    // Multi-file drag-and-drop with insertion mode dialog
    timeline.onImportFiles = [this] (const juce::Array<juce::File>& files,
                                      tracktion::AudioTrack* targetTrack,
                                      double insertTime)
    {
        namespace te = tracktion;

        // If only one file, just insert it sequentially on the target track
        if (files.size() == 1)
        {
            if (targetTrack != nullptr)
                audioEngine.insertAudioClipOnTrack (targetTrack, files[0], insertTime);
            else
                audioEngine.importAudioFileAtPosition (files[0], insertTime);
            timeline.repaint();
            mixer.repaint();
            return;
        }

        // Multiple files: show insertion mode dialog
        InsertMultipleMediaDialog::launch ([this, files, targetTrack, insertTime] (InsertMultipleMediaDialog::InsertMode mode) mutable
        {
            namespace te = tracktion;

            switch (mode)
            {
                case InsertMultipleMediaDialog::InsertMode::separateTracks:
                {
                    // One file per track, all at the same time position
                    te::AudioTrack* currentTrack = targetTrack;
                    for (auto& f : files)
                    {
                        if (currentTrack == nullptr)
                            currentTrack = audioEngine.addAudioTrack();
                        audioEngine.insertAudioClipOnTrack (currentTrack, f, insertTime);
                        currentTrack = nullptr;  // Force creation of new track for next file
                    }
                    break;
                }

                case InsertMultipleMediaDialog::InsertMode::sequentialSingleTrack:
                {
                    // All files on one track, sequential time positions
                    if (targetTrack == nullptr)
                        targetTrack = audioEngine.addAudioTrack();

                    double cursor = insertTime;
                    for (auto& f : files)
                    {
                        audioEngine.insertAudioClipOnTrack (targetTrack, f, cursor);
                        te::AudioFile af (audioEngine.getEngine(), f);
                        double len = af.getLength();
                        if (len > 0.0) cursor += len;
                    }
                    break;
                }

                case InsertMultipleMediaDialog::InsertMode::fixedLanes:
                {
                    // All files on one track, same time position (overlapping)
                    if (targetTrack == nullptr)
                        targetTrack = audioEngine.addAudioTrack();

                    for (auto& f : files)
                        audioEngine.insertAudioClipOnTrack (targetTrack, f, insertTime);
                    break;
                }
            }

            timeline.repaint();
            mixer.repaint();
        });
    };

    // Plugin dropped on a track header from the Browser Plugins tab
    timeline.onPluginDroppedOnTrack = [this] (tracktion::Track* track,
                                               const juce::PluginDescription& desc)
    {
        if (auto p = audioEngine.addPluginToTrack (track, desc))
            p->showWindowExplicitly();
        timeline.repaint();
    };

    // Plugin dropped on a mixer strip from the Browser Plugins tab
    mixer.onPluginDroppedOnStrip = [this] (tracktion::Track* track,
                                            const juce::PluginDescription& desc)
    {
        if (auto p = audioEngine.addPluginToTrack (track, desc))
            p->showWindowExplicitly();
        mixer.repaint();
    };

    timeline.onTrackSelected = [this] (tracktion::Track* track)
    {
        syncInspectorToTrack (track);
    };

    // Initialize auto-save interval from settings
    autoSaveIntervalMs = audioEngine.getAutoSaveIntervalMins() * 60 * 1000;

    // Crash recovery prompt
    if (audioEngine.hasCrashRecovery())
    {
        juce::MessageManager::callAsync ([this]
        {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withTitle("Crash Recovery")
                    .withMessage("Aerion DAW didn't shut down cleanly. Restore the auto-saved session?")
                    .withButton("Discard")
                    .withButton("Restore"),
                [this](int result)
                {
                    if (result == 1)
                    {
                        detachFromObservedEditState();
                        audioEngine.loadProject(audioEngine.getRecoveryFile(), &projectData);
                        attachToCurrentEditState();
                        aiManager.setEdit (audioEngine.getEdit());
                        currentProjectFile = juce::File();
                        hasUnsavedChanges = true;
                        updateTitleBar();
                        syncToolbarFromEngine();
                        projectData.syncWithEngine(audioEngine.getEdit());
                        syncInspectorToTrack (nullptr);
                        syncMenuBarState();
                        mixer.repaint();
                        timeline.repaint();
                    }
                });
        });
    }

    // Tab buttons for bottom panel (Mixer / Piano Roll switcher)
    addAndMakeVisible (tabMixer);
    addAndMakeVisible (tabPianoRoll);
    tabMixer.setColour (juce::TextButton::buttonColourId,    Theme::surface);
    tabMixer.setColour (juce::TextButton::buttonOnColourId,  Theme::active.withAlpha (0.25f));
    tabMixer.setColour (juce::TextButton::textColourOnId,    Theme::active);
    tabMixer.setColour (juce::TextButton::textColourOffId,   Theme::textMuted);
    tabPianoRoll.setColour (juce::TextButton::buttonColourId,    Theme::surface);
    tabPianoRoll.setColour (juce::TextButton::buttonOnColourId,  Theme::active.withAlpha (0.25f));
    tabPianoRoll.setColour (juce::TextButton::textColourOnId,    Theme::active);
    tabPianoRoll.setColour (juce::TextButton::textColourOffId,   Theme::textMuted);

    tabMixer.onClick = [this] {
        bottomPanel = BottomPanel::Mixer;
        resized();
    };
    tabPianoRoll.onClick = [this] {
        if (embeddedPianoRoll != nullptr) { bottomPanel = BottomPanel::PianoRoll; resized(); }
    };

    // MIDI clip double-click callback for embedded editor
    timeline.onMidiClipDoubleClicked = [this] (tracktion::MidiClip& clip)
    {
        // Same clip already embedded — just switch to it
        if (embeddedPianoRoll != nullptr && embeddedClip == &clip)
        {
            bottomPanel = BottomPanel::PianoRoll;
            resized();
            return;
        }

        // New clip — destroy old editor
        if (embeddedPianoRoll != nullptr)
            removeChildComponent (embeddedPianoRoll.get());

        embeddedClip = &clip;
        embeddedPianoRoll = std::make_unique<PianoRollEditor> (clip, audioEngine.getEdit(), projectData, audioEngine);
        addAndMakeVisible (*embeddedPianoRoll);

        // Set up detach callback.
        // Defer destruction via callAsync: the callback fires from inside the
        // editor's own button-click handler, so we must not destroy the editor
        // synchronously while it is still on the call stack.
        embeddedPianoRoll->onDetachRequested = [this] {
            auto* clipPtr = embeddedClip;
            juce::MessageManager::callAsync ([this, clipPtr]
            {
                if (embeddedPianoRoll != nullptr)
                    removeChildComponent (embeddedPianoRoll.get());
                embeddedPianoRoll.reset();
                embeddedClip = nullptr;
                bottomPanel = BottomPanel::Mixer;
                resized();
                if (clipPtr != nullptr)
                {
                    auto* win = new PianoRollWindow (*clipPtr, audioEngine.getEdit(), projectData, audioEngine);
                    (void) win;
                }
            });
        };

        bottomPanel = BottomPanel::PianoRoll;
        resized();
    };

    setSize (1400, 860);
    // 25 Hz: playhead + meters; avoids piling on top of other ~30 Hz component timers.
    startTimerHz (25);

    juce::Logger::writeToLog ("Startup: MainComponent ctor completed in "
                              + juce::String (juce::Time::getMillisecondCounterHiRes() - ctorStartMs, 1)
                              + " ms");
}

MainComponent::~MainComponent()
{
    stopTimer();
    detachFromObservedEditState();
    projectData.getProjectTree().removeListener (this);
    audioEngine.removeListener (this);
    removeKeyListener (this);
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    auto& km = audioEngine.getKeymap();

    if (km.matches ("edit.undo", key)) { audioEngine.undo(); return true; }
    if (km.matches ("edit.redo", key)) { audioEngine.redo(); return true; }
    if (km.matches ("file.save", key)) { saveProject(); return true; }
    if (km.matches ("file.new",  key)) { if (menuBar.onNew)  menuBar.onNew();  return true; }
    if (km.matches ("file.open", key)) { if (menuBar.onOpen) menuBar.onOpen(); return true; }
    if (km.matches ("transport.record", key)) { audioEngine.record(); transport.repaint(); return true; }

    if (km.matches ("transport.playStop", key))
    {
        if (audioEngine.isPlaying())
            audioEngine.stop();
        else
            audioEngine.play();

        transport.repaint();
        return true;
    }

    // Force a crossfade on the selected clip with any overlapping neighbor (Studio One-style quick action).
    if (km.matches ("audio.crossfade", key))
    {
        if ((bool) projectData.getProjectTree().getProperty (IDs::autoCrossfadeEnabled, true))
        {
            if (auto* clip = timeline.selectedClip)
            {
                if (auto* t = clip->getTrack())
                    timeline.applyAutoCrossfadesForTrack (*t);
            }
            timeline.repaint();
            return true;
        }
    }

    const bool nudgeL = km.matches ("clip.nudgeLeft",  key);
    const bool nudgeR = km.matches ("clip.nudgeRight", key);
    const bool trimL  = km.matches ("clip.trimLeft",   key);
    const bool trimR  = km.matches ("clip.trimRight",  key);
    if (nudgeL || nudgeR || trimL || trimR)
    {
        if (auto* clip = timeline.selectedClip)
        {
            double interval = projectData.getProjectTree().getProperty (IDs::snapInterval);
            if (! (bool) projectData.getProjectTree().getProperty (IDs::snapEnabled))
                interval = 0.1; // small nudge if snap is off

            double delta = (nudgeL || trimL) ? -interval : interval;

            auto& ts = audioEngine.getEdit().tempoSequence;
            if (trimL || trimR)
            {
                // Nudge length (trim right)
                auto start = clip->getPosition().getStart();
                auto end = clip->getPosition().getEnd();
                auto bStart = ts.toBeats (start);
                auto bEnd = ts.toBeats (end);
                auto newBEnd = tracktion::BeatPosition::fromBeats (juce::jmax (bStart.inBeats() + 0.01, bEnd.inBeats() + delta));
                clip->setLength (ts.toTime (newBEnd) - start, true);
            }
            else
            {
                // Nudge position
                auto b = ts.toBeats (clip->getPosition().getStart());
                clip->setStart (ts.toTime (tracktion::BeatPosition::fromBeats (juce::jmax (0.0, b.inBeats() + delta))), false, true);
            }
            timeline.repaint();
            return true;
        }
    }

    if (km.matches ("transport.goToStart", key))
    {
        bool wasPlaying = audioEngine.isPlaying();
        audioEngine.setTransportPosition (0.0);
        timeline.scrollViewToBarOne();

        if (wasPlaying)
            audioEngine.play();

        timeline.repaint();
        transport.repaint();
        return true;
    }

    if (km.matches ("clip.delete", key))
    {
        if (timeline.selectedClip != nullptr)
        {
            timeline.selectedClip->removeFromParent();
            timeline.selectedClip = nullptr;
            timeline.repaint();
            return true;
        }
    }

    auto selected = timeline.getSelectedTracks();
    if (selected.isEmpty()) return false;

    auto* t = selected[0];

    if (km.matches ("track.mute", key)) { audioEngine.toggleTrackMute (t); timeline.repaint(); return true; }
    if (km.matches ("track.solo", key)) { audioEngine.toggleTrackSolo (t); timeline.repaint(); return true; }
    if (km.matches ("track.arm", key)) {
        audioEngine.setTrackArmed (t, ! audioEngine.isTrackArmed (t));
        syncInspectorToTrack (t);
        timeline.repaint();
        return true;
    }
    if (km.matches ("clip.delete", key))
    {
        for (auto* track : selected)
            audioEngine.deleteTrack (track);
        syncInspectorToTrack (nullptr);
        timeline.repaint();
        mixer.repaint();
        return true;
    }

    return false;
}

void MainComponent::doCreateNewProject()
{
    detachFromObservedEditState();
    audioEngine.createNewProject();
    attachToCurrentEditState();
    aiManager.setEdit (audioEngine.getEdit());
    currentProjectFile = juce::File();
    hasUnsavedChanges = false;
    syncInspectorToTrack (nullptr);
    updateTitleBar();
    syncToolbarFromEngine();
    mixer.repaint();
    timeline.repaint();
    syncMenuBarState();
}

void MainComponent::createNewProject()
{
    if (!hasUnsavedChanges)
    {
        doCreateNewProject();
        return;
    }

    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withTitle ("New Project")
            .withMessage ("Save changes to the current project?")
            .withButton ("Cancel")
            .withButton ("Discard")
            .withButton ("Save"),
        [this] (int result) {
            if (result == 0) return;                // Cancel
            if (result == 1) { doCreateNewProject(); return; }  // Discard
            // Save
            if (currentProjectFile.existsAsFile())
            {
                audioEngine.saveProject (currentProjectFile, &projectData);
                hasUnsavedChanges = false;
                updateTitleBar();
                doCreateNewProject();
            }
            else
            {
                pendingNewProjectAfterSave = true;
                saveProjectAs();
            }
        });
}

void MainComponent::updateTitleBar()
{
    juce::String name = currentProjectFile.existsAsFile()
                        ? currentProjectFile.getFileNameWithoutExtension()
                        : "My Song";
    if (hasUnsavedChanges)
        name = "*" + name + "*";
    menuBar.projectTitle = name;
    menuBar.repaint();

    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
        dw->setName (name + " - Aerion DAW");
}

void MainComponent::detachFromObservedEditState()
{
    if (observedEditState.isValid())
    {
        observedEditState.removeListener (this);
        observedEditState = {};
    }
}

void MainComponent::attachToCurrentEditState()
{
    detachFromObservedEditState();
    observedEditState = audioEngine.getEdit().state;
    observedEditState.addListener (this);
}

void MainComponent::syncInspectorToTrack (tracktion::Track* track)
{
    if (track != nullptr)
    {
        inspector.trackIndex     = -1;
        inspector.trackName      = track->getName();
        inspector.armed          = audioEngine.isTrackArmed (track);
        inspector.muted          = track->isMuted (false);
        inspector.solo           = track->isSolo  (false);
        inspector.selectedTrack  = track;
    }
    else
    {
        inspector.trackIndex     = -1;
        inspector.trackName      = "(no selection)";
        inspector.armed = inspector.muted = inspector.solo = false;
        inspector.selectedTrack  = nullptr;
    }

    inspector.repaint();
}

void MainComponent::syncToolbarFromEngine()
{
    toolbar.punchEnabled = audioEngine.isPunchEnabled();
    toolbar.pdcEnabled   = audioEngine.isLatencyCompensationEnabled();
    toolbar.countInBars  = audioEngine.getCountInBars();
    toolbar.repaint();
}

void MainComponent::syncMenuBarState()
{
    menuBar.snapEnabled      = (bool)   projectData.getProjectTree().getProperty (IDs::snapEnabled);
    menuBar.snapInterval     = (double) projectData.getProjectTree().getProperty (IDs::snapInterval, 0.25);
    menuBar.autoCrossfadeOn  = (bool)   projectData.getProjectTree().getProperty (IDs::autoCrossfadeEnabled, true);
    menuBar.autoCrossfadeMaxMs = (int)  projectData.getProjectTree().getProperty (IDs::autoCrossfadeMaxMs, 120);
    menuBar.metronomeOn      = audioEngine.isMetronomeEnabled();
    menuBar.countInBars      = audioEngine.getCountInBars();
    menuBar.punchEnabled     = audioEngine.isPunchEnabled();
    menuBar.pdcEnabled       = audioEngine.isLatencyCompensationEnabled();
    menuBar.loopEnabled      = audioEngine.isLooping();
    menuBar.inspectorVisible = ! inspectorToggle.collapsed;
    menuBar.browserVisible   = ! browserToggle.collapsed;
    menuBar.mixerDetached    = (mixerWindow != nullptr);

    auto sel = timeline.getSelectedTracks();
    menuBar.hasSelectedTrack = ! sel.isEmpty();
    menuBar.hasSelectedClip  = (timeline.selectedClip != nullptr);
    if (! sel.isEmpty())
    {
        auto* t = sel[0];
        menuBar.trackArmed = audioEngine.isTrackArmed (t);
        menuBar.trackMuted = t->isMuted (false);
        menuBar.trackSolo  = t->isSolo  (false);
    }
    else
    {
        menuBar.trackArmed = menuBar.trackMuted = menuBar.trackSolo = false;
    }
}

void MainComponent::doOpenProjectChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open Project...", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Aerion Projects"), "*.aerion");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  detachFromObservedEditState();
                                  audioEngine.loadProject (file, &projectData);
                                  attachToCurrentEditState();
                                  aiManager.setEdit (audioEngine.getEdit());
                                  currentProjectFile = file;
                                  hasUnsavedChanges = false;
                                  audioEngine.getRecentProjects().addFile (file);
                                  updateTitleBar();
                                  syncToolbarFromEngine();
                                  projectData.syncWithEngine(audioEngine.getEdit());
                                  syncInspectorToTrack (nullptr);
                                  syncMenuBarState();
                                  mixer.repaint();
                                  timeline.repaint();
                              }
                          });
}

void MainComponent::openProject()
{
    if (!hasUnsavedChanges)
    {
        doOpenProjectChooser();
        return;
    }

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withTitle("Open Project")
            .withMessage("Save changes to the current project?")
            .withButton("Cancel")
            .withButton("Discard")
            .withButton("Save"),
        [this](int result) {
            if (result == 0) return;                // Cancel
            if (result == 1) { doOpenProjectChooser(); return; }  // Discard
            // Save
            if (currentProjectFile.existsAsFile())
            {
                audioEngine.saveProject(currentProjectFile, &projectData);
                hasUnsavedChanges = false;
                updateTitleBar();
                doOpenProjectChooser();
            }
            else
            {
                saveProjectAs();
            }
        });
}

void MainComponent::requestQuit()
{
    if (!hasUnsavedChanges) { juce::JUCEApplication::getInstance()->quit(); return; }

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withTitle("Quit Aerion DAW")
            .withMessage("Save changes before quitting?")
            .withIconType(juce::AlertWindow::InfoIcon)
            .withButton("Cancel")
            .withButton("Discard & Quit")
            .withButton("Save & Quit"),
        [this](int result) {
            if (result == 1) return;   // Cancel (button 1)
            if (result == 3)           // Save & Quit (button 3)
            {
                if (currentProjectFile.existsAsFile())
                {
                    audioEngine.saveProject(currentProjectFile, &projectData);
                    hasUnsavedChanges = false;
                }
                else
                {
                    // Need to save to a new file first
                    pendingQuitAfterSave = true;
                    saveProjectAs();
                    return;
                }
            }
            // result == 2 is Discard & Quit, fall through to quit
            juce::JUCEApplication::getInstance()->quit();
        });
}

void MainComponent::saveProject()
{
    if (currentProjectFile.existsAsFile())
    {
        audioEngine.saveProject (currentProjectFile, &projectData);
        hasUnsavedChanges = false;
        updateTitleBar();
    }
    else
    {
        saveProjectAs();
    }
}

void MainComponent::saveProjectAs()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Save Project As...", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Aerion Projects"), "*.aerion");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file != juce::File())
                              {
                                  if (file.getFileExtension() != ".aerion")
                                      file = file.withFileExtension (".aerion");
                                  audioEngine.saveProject (file, &projectData);
                                  currentProjectFile = file;
                                  hasUnsavedChanges = false;
                                  audioEngine.getRecentProjects().addFile (file);
                                  updateTitleBar();

                                  if (pendingNewProjectAfterSave)
                                  {
                                      pendingNewProjectAfterSave = false;
                                      doCreateNewProject();
                                  }

                                  if (pendingQuitAfterSave)
                                  {
                                      pendingQuitAfterSave = false;
                                      juce::JUCEApplication::getInstance()->quit();
                                  }
                              }
                          });
}

void MainComponent::collectAndSaveAs()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Collect & Save As...", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Aerion Projects"), "*.aerion");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file != juce::File())
                              {
                                  if (file.getFileExtension() != ".aerion")
                                      file = file.withFileExtension (".aerion");
                                  auto skipped = audioEngine.collectAndSave (file, &projectData);
                                  currentProjectFile = file;
                                  hasUnsavedChanges = false;
                                  audioEngine.getRecentProjects().addFile (file);
                                  updateTitleBar();

                                  if (!skipped.isEmpty())
                                  {
                                      juce::String message = "The following audio files could not be collected:\n\n";
                                      for (const auto& filePath : skipped)
                                          message += filePath + "\n";
                                      message += "\nProject saved, but these clips will need to be re-linked.";
                                      juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                               "Missing Audio Files", message);
                                  }
                              }
                          });
}

void MainComponent::importAudioFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Import Audio...", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Aerion Projects"), "*.wav;*.mp3;*.aif;*.flac");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  audioEngine.importAudioFile (file);
                              }
                          });
}

void MainComponent::showAudioSettings()
{
    // Wraps the JUCE selector with a "Reset to Recommended Defaults" button so a
    // user who already has a saved (and possibly suboptimal) audio config can
    // get the same plug-and-play settings a first-run user gets, without
    // having to delete the .settings file by hand.
    class AudioSettingsPanel : public juce::Component
    {
    public:
        AudioSettingsPanel (AudioEngineManager& ae, MainComponent& mc, juce::LookAndFeel& lf)
            : audioEngine (ae), mainComponent (mc)
        {
            selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
                audioEngine.getEngine().getDeviceManager().deviceManager,
                0, 2, 0, 2, true, true, true, false);
            selector->setLookAndFeel (&lf);
            addAndMakeVisible (*selector);

            recommendBtn.setButtonText ("Reset Audio Settings");
            recommendBtn.setTooltip ("Wipe saved audio config and fall back to a safe default "
                                     "(ASIO if installed, otherwise Windows Audio shared). "
                                     "Use this if a driver change locked you into a bad sample rate.");
            recommendBtn.setLookAndFeel (&lf);
            recommendBtn.onClick = [this] { audioEngine.applyRecommendedAudioDefaults(); };
            addAndMakeVisible (recommendBtn);

            // Auto-save interval combo
            autoSaveCombo.setLookAndFeel (&lf);
            autoSaveCombo.addItem ("Auto-save: Off", 1);
            autoSaveCombo.addItem ("Auto-save: 1 min", 2);
            autoSaveCombo.addItem ("Auto-save: 2 min", 3);
            autoSaveCombo.addItem ("Auto-save: 5 min", 4);
            autoSaveCombo.addItem ("Auto-save: 10 min", 5);

            // Set current value
            int intervalMins = audioEngine.getAutoSaveIntervalMins();
            if (intervalMins == 0) autoSaveCombo.setSelectedId (1);
            else if (intervalMins == 1) autoSaveCombo.setSelectedId (2);
            else if (intervalMins == 2) autoSaveCombo.setSelectedId (3);
            else if (intervalMins == 5) autoSaveCombo.setSelectedId (4);
            else if (intervalMins == 10) autoSaveCombo.setSelectedId (5);

            autoSaveCombo.onChange = [this]
            {
                int mins = 0;
                switch (autoSaveCombo.getSelectedId())
                {
                    case 1: mins = 0; break;
                    case 2: mins = 1; break;
                    case 3: mins = 2; break;
                    case 4: mins = 5; break;
                    case 5: mins = 10; break;
                }
                audioEngine.setAutoSaveIntervalMins(mins);
                mainComponent.autoSaveIntervalMs = mins > 0 ? mins * 60 * 1000 : 0;
                mainComponent.autoSaveElapsedMs = 0;
            };
            addAndMakeVisible (autoSaveCombo);

            setSize (500, 530);
        }

        ~AudioSettingsPanel() override
        {
            recommendBtn.setLookAndFeel (nullptr);
            autoSaveCombo.setLookAndFeel (nullptr);
            if (selector) selector->setLookAndFeel (nullptr);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto bottom = r.removeFromBottom (80).reduced (8, 6);

            // Auto-save combo
            autoSaveCombo.setBounds (bottom.removeFromTop (35));
            bottom.removeFromTop (4);

            // Reset button
            recommendBtn.setBounds (bottom);

            selector->setBounds (r);
        }

    private:
        AudioEngineManager& audioEngine;
        MainComponent& mainComponent;
        std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
        juce::TextButton recommendBtn;
        juce::ComboBox autoSaveCombo;
    };

    auto* panel = new AudioSettingsPanel (audioEngine, *this, metalLookAndFeel);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel);
    options.dialogTitle                   = "Audio Settings";
    options.dialogBackgroundColour        = Theme::bgPanel;
    options.escapeKeyTriggersCloseButton  = true;
    options.useNativeTitleBar             = false;
    options.resizable                     = false;

    options.launchAsync();
}

void MainComponent::exportMixdown()
{
    juce::Logger::writeToLog ("ExportMixdown: clicked");
    std::optional<tracktion::TimeRange> sel;
    if (timeline.selectedClip != nullptr)
    {
        juce::Logger::writeToLog ("ExportMixdown: selectedClip=" + timeline.selectedClip->getName());
        sel = tracktion::TimeRange (timeline.selectedClip->getPosition().getStart(),
                                    timeline.selectedClip->getPosition().getEnd());
    }
    else
    {
        juce::Logger::writeToLog ("ExportMixdown: no selectedClip");
    }

    juce::Logger::writeToLog ("ExportMixdown: constructing dialog");
    auto* dialog = new MixdownExportDialog (audioEngine.getEngine(),
                                            audioEngine.getEdit(),
                                            menuBar.projectTitle,
                                            sel);
    juce::Logger::writeToLog ("ExportMixdown: dialog constructed");

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (dialog);
    options.dialogTitle = "Export Mixdown";
    options.dialogBackgroundColour = Theme::bgBase;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.componentToCentreAround = this;
    juce::Logger::writeToLog ("ExportMixdown: launching async dialog window");
    options.launchAsync();
    juce::Logger::writeToLog ("ExportMixdown: launchAsync returned");
}

void MainComponent::timerCallback()
{
    const double pos = audioEngine.getTransportPosition();
    const bool playing = audioEngine.isPlaying();

    if (pos != lastTransportPos || playing != lastIsPlaying)
    {
        transport.repaint();
        lastTransportPos = pos;
        lastIsPlaying = playing;
    }

    // While playing, avoid full repaints: only move the playhead + update meters.
    // Full timeline repaints are reserved for recording or explicit edits.
    if (playing && ! audioEngine.isRecording())
    {
        // Mixer: strips only (CONSOLE header is static). Mixer no longer runs its own 30 Hz timer.
        mixer.repaintStripMetersArea();

        // Inspector: fader/meters refresh on Inspector's own timer (partial repaint).

        // Timeline: repaint only the old/new playhead strips.
        // Needs to be wide enough to fully clear anti-aliased strokes + waveform pixels.
        const float newX = timeline.timeToX (pos);
        const int laneH = timeline.getHeight();
        const int w = 16;
        const int newXi = (int) std::round (newX);

        if (lastPlayheadX < -9000.0f)
        {
            timeline.repaint (newXi - w / 2, 0, w, laneH);
            lastPlayheadX = newX;
        }
        else
        {
            const int oldXi = (int) std::round (lastPlayheadX);
            if (oldXi != newXi)
            {
                timeline.repaint (oldXi - w / 2, 0, w, laneH);
                timeline.repaint (newXi - w / 2, 0, w, laneH);
                lastPlayheadX = newX;
            }
        }
        return;
    }

    if (audioEngine.isRecording())
    {
        // Recording changes waveform data continuously; keep invalidation to live rows/meters.
        mixer.repaintStripMetersArea();
        timeline.repaintRecordingRows();
        return;
    }

    // Stopped: refresh transport CPU / buffer line occasionally (otherwise it looks "frozen").
    if (! playing && ++idleCpuRefreshTick >= 5) // ~5 Hz at 25 Hz main timer
    {
        idleCpuRefreshTick = 0;
        transport.repaint();
    }

    // Auto-save countdown
    if (autoSaveIntervalMs > 0)
    {
        autoSaveElapsedMs += 40;  // 25 Hz = 40 ms per tick
        if (autoSaveElapsedMs >= autoSaveIntervalMs)
        {
            autoSaveElapsedMs = 0;
            if (! audioEngine.isRecording())
                audioEngine.autoSave(&projectData);
        }
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bgBase);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    menuBar.setBounds  (bounds.removeFromTop (28));
    toolbar.setBounds  (bounds.removeFromTop (40));
    transport.setBounds(bounds.removeFromBottom (60));

    // Inspector (left panel)  -  collapses to zero width, toggle strip stays visible
    {
        const int panelW = inspectorToggle.collapsed ? 0 : kInspectorW;
        auto strip = bounds.removeFromLeft (panelW + kToggleW);
        inspector.setBounds (strip.removeFromLeft (panelW));
        inspector.setVisible (! inspectorToggle.collapsed);
        inspectorToggle.setBounds (strip); // remaining kToggleW px
    }

    // Browser (right panel)
    {
        const int panelW = browserToggle.collapsed ? 0 : kBrowserW;
        auto strip = bounds.removeFromRight (panelW + kToggleW);
        browserToggle.setBounds (strip.removeFromRight (kToggleW));
        browser.setBounds (strip);
        browser.setVisible (! browserToggle.collapsed);
    }

    auto centerBounds = bounds;
    static constexpr int kTabH = 22;

    if (! mixer.detached)
    {
        auto bottomArea = centerBounds.removeFromBottom (mixerHeight + 4 + kTabH);

        // Tab bar strip
        auto tabStrip = bottomArea.removeFromTop (kTabH);
        tabMixer.setBounds (tabStrip.removeFromLeft (80));
        tabPianoRoll.setBounds (tabStrip.removeFromLeft (110));
        tabMixer.setToggleState (bottomPanel == BottomPanel::Mixer, juce::dontSendNotification);
        tabPianoRoll.setToggleState (bottomPanel == BottomPanel::PianoRoll, juce::dontSendNotification);

        // Resizer
        mixerResizer.setBounds (bottomArea.removeFromTop (4));

        // Content: either Mixer or PianoRoll
        if (bottomPanel == BottomPanel::Mixer || embeddedPianoRoll == nullptr)
        {
            mixer.setVisible (true);
            mixer.setBounds (bottomArea);
            if (embeddedPianoRoll) { embeddedPianoRoll->setVisible (false); }
        }
        else  // PianoRoll
        {
            mixer.setVisible (false);
            embeddedPianoRoll->setVisible (true);
            embeddedPianoRoll->setBounds (bottomArea);
        }
    }
    else
    {
        // Mixer detached
        mixerResizer.setBounds (0, 0, 0, 0);
        auto tabStrip = centerBounds.removeFromBottom (kTabH);
        tabMixer.setBounds (tabStrip.removeFromLeft (80));
        tabPianoRoll.setBounds (tabStrip.removeFromLeft (110));

        if (embeddedPianoRoll && bottomPanel == BottomPanel::PianoRoll)
        {
            auto prArea = centerBounds.removeFromBottom (mixerHeight);
            embeddedPianoRoll->setVisible (true);
            embeddedPianoRoll->setBounds (prArea);
        }
        else if (embeddedPianoRoll)
            embeddedPianoRoll->setVisible (false);
    }

    timeline.setBounds (centerBounds);
}

//==============================================================================
class MainComponent::MixerWindow : public juce::DocumentWindow
{
public:
    MixerWindow (MainComponent& mc)
        : DocumentWindow ("Mixer - Aerion DAW", Theme::bgPanel,
                          juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          owner (mc)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (28);
        setColour (juce::DocumentWindow::textColourId, Theme::textMain);
        setResizable (true, true);
        setContentNonOwned (&mc.mixer, false);
        centreWithSize (1100, 460);
        setVisible (true);
    }

    void closeButtonPressed() override { owner.reattachMixer(); }

private:
    MainComponent& owner;
};

void MainComponent::detachMixer()
{
    if (mixerWindow != nullptr) return;
    removeChildComponent (&mixer);
    mixer.detached = true;
    mixerWindow = std::make_unique<MixerWindow> (*this);
    resized();
}

void MainComponent::reattachMixer()
{
    if (mixerWindow == nullptr) return;
    // Detach from window first so addAndMakeVisible doesn't double-parent.
    mixerWindow->clearContentComponent();
    mixerWindow.reset();
    mixer.detached = false;
    addAndMakeVisible (mixer);
    resized();
}

void MainComponent::editStateChanged()
{
    const auto syncStartMs = juce::Time::getMillisecondCounterHiRes();

    hasUnsavedChanges = true;
    updateTitleBar();
    projectData.syncWithEngine (audioEngine.getEdit());
    const auto syncElapsedMs = juce::Time::getMillisecondCounterHiRes() - syncStartMs;
    if (syncElapsedMs > 8.0)
        juce::Logger::writeToLog ("Performance: editStateChanged sync/UI took "
                                  + juce::String (syncElapsedMs, 1) + " ms");

    syncToolbarFromEngine();
    syncMenuBarState();

    auto selected = timeline.getSelectedTracks();
    syncInspectorToTrack (selected.isEmpty() ? nullptr : selected[0]);

    browser.repaint();
    mixer.repaint();
    timeline.repaint();
    transport.repaint();
}

void MainComponent::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    if (i == IDs::snapEnabled)
    {
        toolbar.snapEnabled = v.getProperty (i);
        toolbar.repaint();
    }
    else if (i == IDs::snapInterval)
    {
        toolbar.snapInterval = v.getProperty (i);
        toolbar.repaint();
    }
    else if (i == IDs::autoCrossfadeEnabled)
    {
        toolbar.autoCrossfadeEnabled = (bool) v.getProperty (i);
        toolbar.repaint();
    }

    // Reactive synchronization: Tracktion Engine -> ProjectData
    if (v.hasType (tracktion::IDs::TRACK) || v.hasType (tracktion::IDs::FOLDERTRACK))
    {
        auto trackID = v.getProperty (tracktion::IDs::id).toString();
        auto trackTree = projectData.getTrackTree (trackID);
        
        if (trackTree.isValid())
        {
            if (i == tracktion::IDs::mute)
                trackTree.setProperty (IDs::mute, v.getProperty(i), nullptr);
            else if (i == tracktion::IDs::solo)
                trackTree.setProperty (IDs::solo, v.getProperty(i), nullptr);
        }
    }
    else if (v.hasType (tracktion::IDs::PLUGIN))
    {
        if (auto* p = audioEngine.getPluginFor (v))
        {
            if (auto* vp = dynamic_cast<tracktion::VolumeAndPanPlugin*> (p))
            {
                if (auto* t = vp->getOwnerTrack())
                {
                    auto trackID = t->itemID.toString();
                    auto trackTree = projectData.getTrackTree (trackID);
                    if (trackTree.isValid())
                    {
                        if (i == tracktion::IDs::volume)
                        {
                            // Tracktion stores fader position values, not raw gain.
                            // Convert using the native Tracktion formula: db = 20*ln(pos) + 6
                            float nativeVal = v.getProperty (i);
                            float db = (nativeVal > 0.0f) ? (20.0f * std::log (nativeVal)) + 6.0f : -100.0f;
                            trackTree.setProperty (IDs::level, db, nullptr);
                        }
                        else if (i == tracktion::IDs::pan)
                            trackTree.setProperty (IDs::pan, vp->panParam->getCurrentValue(), nullptr);
                    }
                }
            }
        }
    }
}

void MainComponent::valueTreeChildRemoved (juce::ValueTree& /*parent*/,
                                           juce::ValueTree& child, int)
{
    // If the embedded MIDI clip is deleted, close the Piano Roll and revert to Mixer
    if (embeddedPianoRoll && embeddedClip && child == embeddedClip->state)
    {
        removeChildComponent (embeddedPianoRoll.get());
        embeddedPianoRoll.reset();
        embeddedClip = nullptr;
        bottomPanel = BottomPanel::Mixer;
        resized();
    }
}
