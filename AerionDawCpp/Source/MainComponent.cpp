#include "MainComponent.h"
#include "UI/Dialogs.h"
#include "CrashReporter.h"

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
    addAndMakeVisible (playheadOverlay);   // directly above the Timeline
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
            if (audioEngine.isTrackFrozen(targetTrack) || audioEngine.isTrackFreezing(targetTrack)) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Track Unavailable", "Cannot add clips to frozen tracks or tracks that are currently freezing.");
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
    menuBar.onSaveAsTemplate      = [this] { saveProjectAsTemplate(); };
    menuBar.onShowTemplatesFolder = [this]
    {
        auto folder = audioEngine.getTemplatesFolder();
        folder.createDirectory();
        folder.revealToUser();
    };
    menuBar.onNewFromTemplate = [this] (juce::File templateFile)
    {
        if (! projectHasUnsavedChanges()) { openTemplate (templateFile); return; }

        Dialogs::askToSaveChanges ("New from Template", "Save changes to the current project?",
            [this, templateFile] (Dialogs::SaveChoice choice)
            {
                if (choice == Dialogs::SaveChoice::cancel) return;
                if (choice == Dialogs::SaveChoice::save)
                {
                    if (! currentProjectFile.existsAsFile()) { saveProjectAs(); return; }
                    audioEngine.saveProject (currentProjectFile, &projectData);
                    markProjectClean();
                }
                openTemplate (templateFile);
            });
    };
    menuBar.onOpen     = [this] { openProject(); };
    menuBar.onOpenRecent = [this] (juce::File f)
    {
        auto doOpen = [this, f]()
        {
            closeEmbeddedPianoRoll();
            timeline.clearSelectedClip();
            detachFromObservedEditState();
            audioEngine.loadProject (f, &projectData);
            attachToCurrentEditState();
            aiManager.setEdit (audioEngine.getEdit());
            currentProjectFile = f;
            markProjectClean();
            audioEngine.getRecentProjects().addFile (f);
            updateTitleBar();
            syncToolbarFromEngine();
            projectData.syncWithEngine(audioEngine.getEdit());
            syncMenuBarState();
            mixer.repaint();
            timeline.repaint();
        };

        if (! projectHasUnsavedChanges()) { doOpen(); return; }

        Dialogs::askToSaveChanges ("Open Recent", "Save changes to the current project?",
            [this, doOpen] (Dialogs::SaveChoice choice)
            {
                if (choice == Dialogs::SaveChoice::cancel)
                    return;

                if (choice == Dialogs::SaveChoice::save)
                {
                    // An unsaved project has no file yet: save it under a name
                    // first instead of opening the other project over it.
                    if (! currentProjectFile.existsAsFile())
                    {
                        saveProjectAs();
                        return;
                    }

                    audioEngine.saveProject (currentProjectFile, &projectData);
                    markProjectClean();
                    updateTitleBar();
                }

                doOpen();
            });
    };
    menuBar.onClearRecent   = [this] { audioEngine.clearRecentProjects(); };
    menuBar.onCollectSaveAs = [this] { collectAndSaveAs(); };
    menuBar.onSave     = [this] { saveProject(); };
    menuBar.onSaveAs   = [this] { saveProjectAs(); };
    menuBar.onImport   = [this] { importAudioFile(); };
    menuBar.onExportMixdown = [this] { exportMixdown(); };
    menuBar.onSettings = [this] { showAudioSettings(); };

   #if JUCE_MAC
    // Mac users expect the menus at the top of the screen, with About, Check
    // for Updates and Settings in the application menu.
    menuBar.inMacMenuBar = true;
    menuBar.keymap = &audioEngine.getKeymap();   // the application menu's Settings shortcut
    menuBar.setVisible (false);
    systemMenuBar = std::make_unique<DAWMenuBar::SystemMenuBar> (menuBar);
    const auto applicationMenu = systemMenuBar->applicationMenuItems();
    juce::MenuBarModel::setMacMainMenu (systemMenuBar.get(), &applicationMenu);
   #endif

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
        timeline.clearSelectedClip();
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
            if (isClipTrackFrozenOrFreezing (audioEngine, clip))
                return;

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
            if (isClipTrackFrozenOrFreezing (audioEngine, clip))
                return;

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
            if (isClipTrackFrozenOrFreezing (audioEngine, clip))
                return;

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
            if (isClipTrackFrozenOrFreezing (audioEngine, clip))
                return;

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
            if (isClipTrackFrozenOrFreezing (audioEngine, timeline.selectedClip))
                return;

            timeline.selectedClip->removeFromParent();
            timeline.clearSelectedClip();
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
    menuBar.onToggleFollowPlayback = [this] { setFollowPlayback (! timeline.isFollowingPlayback()); };
    toolbar.onToggleFollowPlayback = [this] { setFollowPlayback (toolbar.followPlayback); };

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
    menuBar.onApplyWorkspace  = [this] (juce::String n) { applyWorkspaceLayoutByName (n); };
    menuBar.onSaveWorkspace   = [this] { saveCurrentWorkspaceLayout(); };
    menuBar.onDeleteWorkspace = [this] (juce::String n) { deleteWorkspaceLayout (n); };
    menuBar.onLightweightUiChanged = [this] (int choice)
    {
        if (auto* s = audioEngine.getUserSettings())
            s->setValue (kLightweightUiKey, choice);
        applyLightweightUi (choice);
    };
    menuBar.onUiSizeChanged = [this] (int choice)
    {
        if (auto* s = audioEngine.getUserSettings())
            s->setValue (UiScale::settingsKey, choice);
        applyUiSize (choice);
    };
    menuBar.onGraphicsEngineChanged = [this] (int choice)
    {
        graphicsEngine.setChoice (GraphicsEngine::choiceFromInt (choice));
        if (auto* s = audioEngine.getUserSettings())
            s->setValue (GraphicsEngine::settingsKey, choice);
        syncMenuBarState();
    };
    menuBar.onShowKeyboardShortcuts = [this] {
        KeyboardShortcutsDialog::launch (audioEngine.getKeymap(), audioEngine.getUserSettings());
    };

    menuBar.onCheckForUpdates        = [this] { updater.checkNow(); };
    menuBar.onAutoUpdateCheckChanged = [this] (bool on) { updater.setAutoCheckEnabled (on); };
    updater.onInstallRequested       = [this] { requestQuit(); };

    menuBar.onBeforeMenuOpen = [this]
    {
        syncMenuBarState();
        menuBar.projectTemplates = audioEngine.getProjectTemplates();
    };

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

        if (targetTrack != nullptr
            && (audioEngine.isTrackFrozen (targetTrack) || audioEngine.isTrackFreezing (targetTrack)))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                "Track Unavailable",
                "Cannot add clips to frozen tracks or tracks that are currently freezing.");
            return;
        }

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

    timeline.onClipSelected = [this] (tracktion::Clip* clip)
    {
        inspector.setSelectedClip (clip);
    };

    mixer.onStripSelected = [this] (tracktion::Track* track)
    {
        if (track != inspector.selectedTrack)
            syncInspectorToTrack (track);
    };

    timeline.onSelectionChanged = [this] (juce::Array<tracktion::Track*> tracks)
    {
        audioEngine.setSelectedTracks (tracks);
    };

    // Initialize auto-save interval from settings
    autoSaveIntervalMs = audioEngine.getAutoSaveIntervalMins() * 60 * 1000;

    // After a crash: point to the crash report, then offer the auto-saved session.
    auto offerCrashRecovery = [this]
    {
        if (! audioEngine.hasCrashRecovery())
            return;

        Dialogs::confirm ("Crash Recovery",
            "Aerion DAW didn't shut down cleanly. Restore the auto-saved session?",
            "Restore", "Discard",
            [this] (bool restore)
            {
                if (restore)
                {
                    closeEmbeddedPianoRoll();
                    timeline.clearSelectedClip();
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
    };

    const auto crashReport = CrashReporter::findUnseenReport (CrashReporter::getDefaultReportsFolder());

    if (crashReport != juce::File() || audioEngine.hasCrashRecovery())
    {
        juce::MessageManager::callAsync ([crashReport, offerCrashRecovery]
        {
            if (crashReport == juce::File())
            {
                offerCrashRecovery();
                return;
            }

            CrashReporter::markSeen (crashReport);

            auto reason = CrashReporter::readReason (crashReport);
            Dialogs::confirm ("Aerion DAW Crashed",
                "Aerion DAW crashed last time" + (reason.isNotEmpty() ? " (" + reason + ")" : juce::String())
                    + ".\n\nA crash report with the log of that session was saved in:\n"
                    + crashReport.getFullPathName(),
                "Show Report", "Close",
                [crashReport, offerCrashRecovery] (bool show)
                {
                    if (show)
                        crashReport.getChildFile ("report.txt").revealToUser();

                    offerCrashRecovery();
                });
        });
    }

    // Tab buttons for bottom panel (Mixer / Piano Roll / Console switcher)
    addAndMakeVisible (tabMixer);
    addAndMakeVisible (tabPianoRoll);
    addAndMakeVisible (tabConsole);
    addAndMakeVisible (consolePanel);
    consolePanel.setVisible (false);

    auto setupTab = [](juce::TextButton& btn) {
        btn.setColour (juce::TextButton::buttonColourId,    Theme::surface);
        btn.setColour (juce::TextButton::buttonOnColourId,  Theme::active.withAlpha (0.25f));
        btn.setColour (juce::TextButton::textColourOnId,    Theme::active);
        btn.setColour (juce::TextButton::textColourOffId,   Theme::textMuted);
    };
    setupTab (tabMixer);
    setupTab (tabPianoRoll);
    setupTab (tabConsole);

    tabMixer.onClick = [this] {
        bottomPanel = BottomPanel::Mixer;
        resized();
    };
    tabConsole.onClick = [this] {
        bottomPanel = BottomPanel::Console;
        resized();
    };
    tabPianoRoll.onClick = [this] {
        if (embeddedPianoRoll != nullptr)
        {
            bottomPanel = BottomPanel::PianoRoll;
            resized();
            embeddedPianoRoll->grabKeyboardFocus();
        }
    };

    // MIDI clip double-click callback for embedded editor
    timeline.onMidiClipDoubleClicked = [this] (tracktion::MidiClip& clip)
    {
        // Same clip already embedded — just switch to it
        if (embeddedPianoRoll != nullptr && embeddedClip == &clip)
        {
            bottomPanel = BottomPanel::PianoRoll;
            resized();
            embeddedPianoRoll->grabKeyboardFocus();
            return;
        }

        // New clip — destroy old editor
        closeEmbeddedPianoRoll();

        embeddedClip = &clip;
        attachEmbeddedClipListener (clip);
        embeddedPianoRoll = std::make_unique<PianoRollEditor> (clip, audioEngine.getEdit(), projectData, audioEngine);
        embeddedPianoRoll->setFollowPlayback (timeline.isFollowingPlayback());
        addAndMakeVisible (*embeddedPianoRoll);

        // Set up detach callback.
        // Defer destruction via callAsync: the callback fires from inside the
        // editor's own button-click handler, so we must not destroy the editor
        // synchronously while it is still on the call stack.
        embeddedPianoRoll->onDetachRequested = [this] {
            auto* clipPtr = embeddedClip;
            juce::MessageManager::callAsync ([this, clipPtr]
            {
                closeEmbeddedPianoRoll();
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
        embeddedPianoRoll->grabKeyboardFocus();
    };

    setSize (1400, 860);
    updateTitleBar();

    if (auto* s = audioEngine.getUserSettings())
    {
        graphicsEngine.setChoice (GraphicsEngine::choiceFromInt (s->getIntValue (GraphicsEngine::settingsKey, 0)));
        applyLightweightUi (s->getIntValue (kLightweightUiKey, 0));
        uiSizeChoice = s->getIntValue (UiScale::settingsKey, 0);
        setFollowPlayback (s->getBoolValue (kFollowPlaybackKey, true));
    }

    // Restore the last-used workspace layout (built-in or custom) from settings.
    loadWorkspaceLayouts();
    if (activeLayoutName.isNotEmpty())
        applyWorkspaceLayoutByName (activeLayoutName);

    // Playhead, meters and readouts run on the display clock (onDisplayFrame);
    // this timer only handles slow chores such as the auto-save countdown.
    startTimerHz (5);

    updater.checkAfterStartup();

    juce::Logger::writeToLog ("Startup: MainComponent ctor completed in "
                              + juce::String (juce::Time::getMillisecondCounterHiRes() - ctorStartMs, 1)
                              + " ms");
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    stopTimer();
    closeEmbeddedPianoRoll();
    detachFromObservedEditState();
    projectData.getProjectTree().removeListener (this);
    audioEngine.removeListener (this);
    removeKeyListener (this);
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component* origin)
{
    auto& km = audioEngine.getKeymap();

    if (embeddedPianoRoll != nullptr && embeddedPianoRoll->isVisible())
    {
        const bool keyCameFromPianoRoll =
            origin == embeddedPianoRoll.get()
            || (origin != nullptr && embeddedPianoRoll->isParentOf (origin))
            || embeddedPianoRoll->hasKeyboardFocus (true);

        if (keyCameFromPianoRoll && embeddedPianoRoll->keyPressed (key))
            return true;
    }

    if (km.matches ("view.nextPane", key)) { focusNextPane(); return true; }
    if (km.matches ("app.settings", key))  { showAudioSettings(); return true; }

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

    // Force a crossfade on the selected clip with any overlapping neighbor.
    if (km.matches ("audio.crossfade", key))
    {
        if ((bool) projectData.getProjectTree().getProperty (IDs::autoCrossfadeEnabled, true))
        {
            if (auto* clip = timeline.selectedClip)
            {
                if (isClipTrackFrozenOrFreezing (audioEngine, clip))
                    return true;

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
            if (isClipTrackFrozenOrFreezing (audioEngine, clip))
                return true;

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

    if (km.matches ("transport.follow", key)) { setFollowPlayback (! timeline.isFollowingPlayback()); return true; }

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
            if (isClipTrackFrozenOrFreezing (audioEngine, timeline.selectedClip))
                return true;

            timeline.selectedClip->removeFromParent();
            timeline.clearSelectedClip();
            timeline.repaint();
            return true;
        }
    }

    // With a Mixer strip focused from the keyboard, track keys act on its track.
    if (auto* strip = mixer.getKeyboardTrack())
    {
        if (km.matches ("track.mute", key)) { audioEngine.toggleTrackMute (strip); mixer.repaint(); timeline.repaint(); return true; }
        if (km.matches ("track.solo", key)) { audioEngine.toggleTrackSolo (strip); mixer.repaint(); timeline.repaint(); return true; }
        if (km.matches ("track.arm", key))
        {
            audioEngine.setTrackArmed (strip, ! audioEngine.isTrackArmed (strip));
            mixer.repaint();
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
        timeline.clearSelectedClip();
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
    closeEmbeddedPianoRoll();
    timeline.clearSelectedClip();
    detachFromObservedEditState();
    audioEngine.createNewProject();
    attachToCurrentEditState();
    aiManager.setEdit (audioEngine.getEdit());
    currentProjectFile = juce::File();
    markProjectClean();
    syncInspectorToTrack (nullptr);
    updateTitleBar();
    syncToolbarFromEngine();
    mixer.repaint();
    timeline.repaint();
    syncMenuBarState();
}

void MainComponent::createNewProject()
{
    if (! projectHasUnsavedChanges())
    {
        doCreateNewProject();
        return;
    }

    Dialogs::askToSaveChanges ("New Project", "Save changes to the current project?",
        [this] (Dialogs::SaveChoice choice) {
            if (choice == Dialogs::SaveChoice::cancel) return;
            if (choice == Dialogs::SaveChoice::discard) { doCreateNewProject(); return; }

            if (currentProjectFile.existsAsFile())
            {
                audioEngine.saveProject (currentProjectFile, &projectData);
                markProjectClean();
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

void MainComponent::openTemplate (const juce::File& templateFile)
{
    if (! templateFile.existsAsFile())
        return;

    closeEmbeddedPianoRoll();
    timeline.clearSelectedClip();
    detachFromObservedEditState();
    audioEngine.loadProject (templateFile, &projectData);
    attachToCurrentEditState();
    aiManager.setEdit (audioEngine.getEdit());
    currentProjectFile = juce::File();   // untitled, so Save asks where to put it
    markProjectClean();
    updateTitleBar();
    syncToolbarFromEngine();
    projectData.syncWithEngine (audioEngine.getEdit());
    syncInspectorToTrack (nullptr);
    syncMenuBarState();
    mixer.repaint();
    timeline.repaint();
}

void MainComponent::saveProjectAsTemplate()
{
    auto* alert = new juce::AlertWindow ("Save as Template",
                                         "Name the template. It will appear under File > New from Template.",
                                         juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor ("name", currentProjectFile.existsAsFile() ? currentProjectFile.getFileNameWithoutExtension()
                                                                     : juce::String ("My Template"));
    auto* includeClips = new juce::ToggleButton ("Include clips (otherwise tracks, plugins and routing only)");
    includeClips->setSize (360, 24);
    alert->addCustomComponent (includeClips);
    alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    alert->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MainComponent> (this), alert, includeClips] (int result)
        {
            std::unique_ptr<juce::AlertWindow> ownedAlert (alert);
            std::unique_ptr<juce::ToggleButton> ownedToggle (includeClips);
            if (safe == nullptr || result != 1)
                return;

            const auto name = alert->getTextEditorContents ("name");
            const auto existing = safe->audioEngine.getTemplatesFolder()
                                      .getChildFile (juce::File::createLegalFileName (name.trim()) + ".aerion");
            const bool withClips = includeClips->getToggleState();

            auto save = [safe, name, withClips]
            {
                if (safe == nullptr) return;
                if (safe->audioEngine.saveProjectAsTemplate (name, &safe->projectData, withClips) == juce::File())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save as Template",
                                                            "The template could not be saved. Try another name.");
            };

            if (! existing.existsAsFile()) { save(); return; }

            Dialogs::confirm ("Save as Template", "A template called \"" + existing.getFileNameWithoutExtension()
                                                     + "\" already exists. Replace it?",
                              "Replace", "Cancel", [save] (bool replace) { if (replace) save(); });
        }), false);
}

void MainComponent::updateTitleBar()
{
    juce::String name = currentProjectFile.existsAsFile()
                        ? currentProjectFile.getFileNameWithoutExtension()
                        : "My Song";
    titleShowsUnsavedChanges = projectHasUnsavedChanges();
    if (titleShowsUnsavedChanges)
        name = "*" + name + "*";
    menuBar.projectTitle = name;
    menuBar.repaint();

    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
        dw->setName (Theme::windowTitle (name));
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
    menuBar.followPlayback   = timeline.isFollowingPlayback();
    menuBar.keymap           = &audioEngine.getKeymap();
    menuBar.inspectorVisible = ! inspectorToggle.collapsed;
    menuBar.browserVisible   = ! browserToggle.collapsed;
    menuBar.mixerDetached    = (mixerWindow != nullptr);

    menuBar.builtInWorkspaceNames.clearQuick();
    for (const auto& l : builtInLayouts())
        menuBar.builtInWorkspaceNames.add (l.name);
    menuBar.customWorkspaceNames.clearQuick();
    for (const auto& l : customLayouts)
        menuBar.customWorkspaceNames.add (l.name);
    menuBar.activeWorkspaceName = activeLayoutName;
    menuBar.lightweightUiChoice  = lightweightUiChoice;
    menuBar.lightweightUiActive  = Theme::lightweightUi();
    menuBar.uiSizeChoice         = uiSizeChoice;
    menuBar.uiSizePercent        = UiScale::currentPercent();
    menuBar.autoUpdateCheck      = updater.isAutoCheckEnabled();
    menuBar.graphicsEngineChoice = (int) graphicsEngine.getChoice();
    menuBar.graphicsEngineInUse  = GraphicsEngine::resolvedEngineName (graphicsEngine.getChoice());

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
                                  closeEmbeddedPianoRoll();
                                  timeline.clearSelectedClip();
                                  detachFromObservedEditState();
                                  audioEngine.loadProject (file, &projectData);
                                  attachToCurrentEditState();
                                  aiManager.setEdit (audioEngine.getEdit());
                                  currentProjectFile = file;
                                  markProjectClean();
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
    if (! projectHasUnsavedChanges())
    {
        doOpenProjectChooser();
        return;
    }

    Dialogs::askToSaveChanges ("Open Project", "Save changes to the current project?",
        [this] (Dialogs::SaveChoice choice) {
            if (choice == Dialogs::SaveChoice::cancel) return;
            if (choice == Dialogs::SaveChoice::discard) { doOpenProjectChooser(); return; }

            if (currentProjectFile.existsAsFile())
            {
                audioEngine.saveProject(currentProjectFile, &projectData);
                markProjectClean();
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
    if (! projectHasUnsavedChanges()) { juce::JUCEApplication::getInstance()->quit(); return; }

    Dialogs::askToSaveChanges ("Quit Aerion DAW", "Save changes before quitting?",
        [this] (Dialogs::SaveChoice choice) {
            if (choice == Dialogs::SaveChoice::cancel) return;
            if (choice == Dialogs::SaveChoice::save)
            {
                if (currentProjectFile.existsAsFile())
                {
                    audioEngine.saveProject(currentProjectFile, &projectData);
                    markProjectClean();
                }
                else
                {
                    // Need to save to a new file first
                    pendingQuitAfterSave = true;
                    saveProjectAs();
                    return;
                }
            }
            juce::JUCEApplication::getInstance()->quit();
        },
        "Save & Quit", "Discard & Quit");
}

void MainComponent::saveProject()
{
    if (currentProjectFile.existsAsFile())
    {
        audioEngine.saveProject (currentProjectFile, &projectData);
        markProjectClean();
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
                                  markProjectClean();
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
                                  markProjectClean();
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

void MainComponent::setFollowPlayback (bool shouldFollow)
{
    timeline.setFollowPlayback (shouldFollow);
    if (embeddedPianoRoll != nullptr)
        embeddedPianoRoll->setFollowPlayback (shouldFollow);
    toolbar.followPlayback = shouldFollow;
    toolbar.repaint();

    if (auto* s = audioEngine.getUserSettings())
        s->setValue (kFollowPlaybackKey, shouldFollow);
}

void MainComponent::applyLightweightUi (int choice)
{
    lightweightUiChoice = juce::jlimit (0, 2, choice);

    // Auto: machines with few cores or little memory are the ones where
    // decorative fills and 30 Hz meters cost a noticeable share of the CPU.
    const bool weakMachine = juce::SystemStats::getNumPhysicalCpus() <= 2
                          || juce::SystemStats::getMemorySizeInMegabytes() < 6 * 1024;

    const bool enable = lightweightUiChoice == 1 || (lightweightUiChoice == 0 && weakMachine);
    const bool changed = enable != Theme::lightweightUi();
    Theme::lightweightUi() = enable;

    juce::Logger::writeToLog ("Lightweight UI: "
                              + juce::String (lightweightUiChoice == 0 ? "Auto" : lightweightUiChoice == 1 ? "On" : "Off")
                              + " -> " + (enable ? "on" : "off"));

    if (changed)
    {
        // Cached layers keep their old pixels until invalidated.
        timeline.repaint();
        repaint();
    }

    syncMenuBarState();
}

void MainComponent::applyUiSize (int choice)
{
    uiSizeChoice = choice <= 0 ? 0 : juce::jlimit (UiScale::kMinPercent, UiScale::kMaxPercent, choice);
    const int percent = UiScale::percentFor (uiSizeChoice);

    if (percent != UiScale::currentPercent())
    {
        // Windows keep their size on screen when the scale changes, so the
        // main window would show less of the layout at a larger size. Keep its
        // layout size instead, as far as the display allows.
        auto* window = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent());
        const auto layoutSize = window != nullptr ? window->getBounds() : juce::Rectangle<int>();

        UiScale::apply (percent);

        if (window != nullptr && ! window->isFullScreen() && ! window->isMinimised())
            if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (window->getScreenBounds()))
            {
                const auto area = display->userArea;
                window->setBounds (layoutSize.withPosition (window->getPosition())
                                             .withSize (juce::jmin (layoutSize.getWidth(),  area.getWidth()),
                                                        juce::jmin (layoutSize.getHeight(), area.getHeight()))
                                             .constrainedWithin (area));
            }
    }

    juce::Logger::writeToLog ("UI size: " + (uiSizeChoice == 0 ? juce::String ("Auto") : juce::String (uiSizeChoice) + " %")
                              + " -> " + juce::String (UiScale::currentPercent()) + " %");
    syncMenuBarState();
}

void MainComponent::onDisplayFrame (double nowSec)
{
    const double pos = audioEngine.getTransportPosition();
    const bool playing = audioEngine.isPlaying();
    const bool recording = audioEngine.isRecording();
    const bool lightweight = Theme::lightweightUi();

    // Rates: the playhead every display frame (30 Hz in Lightweight UI);
    // meters, transport readout and live recording rows at most 30 Hz (20 Hz).
    // The 0.9 tolerance keeps a 60 Hz display at exactly every other frame
    // despite timestamp jitter.
    const double playheadIntervalSec = lightweight ? 1.0 / 30.0 : 0.0;
    const double meterIntervalSec    = lightweight ? 1.0 / 20.0 : 1.0 / 30.0;

    // The playhead layer repaints only when it moved to another pixel (playback,
    // locate, scroll or zoom). The Timeline underneath is drawn from its cached
    // layer, so this costs a strip copy.
    if (nowSec - lastPlayheadFrameSec >= playheadIntervalSec * 0.9)
    {
        lastPlayheadFrameSec = nowSec;
        // Follow Playback pages the Timeline first, so the overlay draws the
        // playhead at its new place.
        timeline.followPlayhead (pos, playing);
        playheadOverlay.update();

        if (embeddedPianoRoll != nullptr && embeddedPianoRoll->isVisible())
            embeddedPianoRoll->updatePlayhead (pos, playing);
    }

    if (nowSec - lastMeterFrameSec < meterIntervalSec * 0.9)
        return;

    lastMeterFrameSec = nowSec;

    // Keep meters running briefly after the transport stops so they fall back
    // to silence instead of freezing at their last level.
    if (playing || recording)
        meterTailUntilSec = nowSec + 1.5;

    if (pos != lastTransportPos || playing != lastIsPlaying)
    {
        transport.repaint();
        lastTransportPos = pos;
        lastIsPlaying = playing;
    }

    if (nowSec < meterTailUntilSec)
    {
        // Only the meters, fader caps and readouts; see Mixer::getPlaybackRepaintRegion.
        mixer.repaintStripMetersArea();
        inspector.repaintMeters();
    }

    // Recording changes waveform data continuously; keep invalidation to live rows.
    if (recording)
        timeline.repaintRecordingRows();
}

void MainComponent::timerCallback()
{
    // Slow chores only; everything that animates runs on the display clock.
    AERION_PROFILE_TICK (profileReporter);

    const auto nowMs = juce::Time::getMillisecondCounter();
    const int elapsedMs = lastChoreMs == 0 ? 0 : (int) (nowMs - lastChoreMs);
    lastChoreMs = nowMs;

    // Stopped: refresh the transport CPU / buffer readout so it doesn't look frozen.
    if (! audioEngine.isPlaying() && ! audioEngine.isRecording())
        transport.repaint();

    // Edits made directly on the Edit (clip drags, trims) don't broadcast, so
    // pick up the unsaved-changes marker for the title bar here.
    if (projectHasUnsavedChanges() != titleShowsUnsavedChanges)
        updateTitleBar();

    // Auto-save countdown, in real time rather than ticks.
    if (autoSaveIntervalMs > 0)
    {
        autoSaveElapsedMs += elapsedMs;
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

    // On macOS the menus are in the system menu bar (see the constructor).
    menuBar.setBounds  (bounds.removeFromTop (menuBar.inMacMenuBar ? 0 : 28));
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
        tabConsole.setBounds (tabStrip.removeFromLeft (90));
        tabMixer.setToggleState (bottomPanel == BottomPanel::Mixer, juce::dontSendNotification);
        tabPianoRoll.setToggleState (bottomPanel == BottomPanel::PianoRoll, juce::dontSendNotification);
        tabConsole.setToggleState (bottomPanel == BottomPanel::Console, juce::dontSendNotification);

        // Resizer
        mixerResizer.setBounds (bottomArea.removeFromTop (4));

        // Content: either Mixer, Console, or PianoRoll
        mixer.setVisible (false);
        consolePanel.setVisible (false);
        if (embeddedPianoRoll) { embeddedPianoRoll->setVisible (false); }

        if (bottomPanel == BottomPanel::Console)
        {
            consolePanel.setVisible (true);
            consolePanel.setBounds (bottomArea);
        }
        else if (bottomPanel == BottomPanel::Mixer || embeddedPianoRoll == nullptr)
        {
            mixer.setVisible (true);
            mixer.setBounds (bottomArea);
        }
        else  // PianoRoll
        {
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
        tabConsole.setBounds (tabStrip.removeFromLeft (90));
        tabMixer.setToggleState (bottomPanel == BottomPanel::Mixer, juce::dontSendNotification);
        tabPianoRoll.setToggleState (bottomPanel == BottomPanel::PianoRoll, juce::dontSendNotification);
        tabConsole.setToggleState (bottomPanel == BottomPanel::Console, juce::dontSendNotification);

        consolePanel.setVisible (false);
        if (embeddedPianoRoll) embeddedPianoRoll->setVisible (false);

        if (bottomPanel == BottomPanel::Console)
        {
            auto cArea = centerBounds.removeFromBottom (mixerHeight);
            consolePanel.setVisible (true);
            consolePanel.setBounds (cArea);
        }
        else if (embeddedPianoRoll && bottomPanel == BottomPanel::PianoRoll)
        {
            auto prArea = centerBounds.removeFromBottom (mixerHeight);
            embeddedPianoRoll->setVisible (true);
            embeddedPianoRoll->setBounds (prArea);
        }
    }

    timeline.setBounds (centerBounds);
    playheadOverlay.setBounds (centerBounds);
}

//==============================================================================
class MainComponent::MixerWindow : public juce::DocumentWindow
{
public:
    MixerWindow (MainComponent& mc)
        : DocumentWindow (juce::String ("Mixer") + Theme::productTitleSuffix(), Theme::bgPanel,
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

//==============================================================================
// Workspace layouts

std::vector<MainComponent::WorkspaceLayout> MainComponent::builtInLayouts()
{
    std::vector<WorkspaceLayout> v;

    // Editing — everything visible, compact console under the arrangement.
    v.push_back ({ "Editing",   false, false, false, 0, 240 });
    // Mixing — hide the browser, give the console more room.
    v.push_back ({ "Mixing",    false, true,  false, 0, 480 });
    // Recording — inspector for input/monitor, browser hidden, small console.
    v.push_back ({ "Recording", false, true,  false, 0, 220 });

    return v;
}

MainComponent::WorkspaceLayout MainComponent::captureCurrentLayout (juce::String name) const
{
    WorkspaceLayout l;
    l.name               = name;
    l.inspectorCollapsed = inspectorToggle.collapsed;
    l.browserCollapsed   = browserToggle.collapsed;
    l.mixerDetached      = mixer.detached;
    l.bottomPanel        = (bottomPanel == BottomPanel::Console) ? 2 : ((bottomPanel == BottomPanel::PianoRoll) ? 1 : 0);
    l.mixerHeight        = mixerHeight;
    return l;
}

void MainComponent::applyWorkspaceLayout (const WorkspaceLayout& layout)
{
    inspectorToggle.collapsed = layout.inspectorCollapsed;
    toolbar.inspectorVisible  = ! layout.inspectorCollapsed;
    browserToggle.collapsed   = layout.browserCollapsed;
    toolbar.browserVisible    = ! layout.browserCollapsed;

    if (layout.bottomPanel == 2)
        bottomPanel = BottomPanel::Console;
    else if (layout.bottomPanel == 1)
        bottomPanel = BottomPanel::PianoRoll;
    else
        bottomPanel = BottomPanel::Mixer;

    const int maxMixerH = juce::jmax (140, getHeight() - 400);
    mixerHeight = juce::jlimit (100, maxMixerH, layout.mixerHeight);

    if (layout.mixerDetached && ! mixer.detached)      detachMixer();
    else if (! layout.mixerDetached && mixer.detached) reattachMixer();

    activeLayoutName = layout.name;

    toolbar.repaint();
    resized();
    syncMenuBarState();

    if (auto* s = audioEngine.getUserSettings())
    {
        s->setValue ("activeWorkspaceLayout", activeLayoutName);
        s->saveIfNeeded();
    }
}

void MainComponent::applyWorkspaceLayoutByName (const juce::String& name)
{
    for (const auto& l : builtInLayouts())
        if (l.name == name) { applyWorkspaceLayout (l); return; }

    for (const auto& l : customLayouts)
        if (l.name == name) { applyWorkspaceLayout (l); return; }
}

void MainComponent::saveCurrentWorkspaceLayout()
{
    auto* aw = new juce::AlertWindow ("Save Workspace Layout",
                                      "Enter a name for this layout:",
                                      juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", "My Layout");
    aw->addButton ("Save",   1);
    aw->addButton ("Cancel", 0);

    juce::Component::SafePointer<MainComponent> safe (this);
    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safe, aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> owned (aw);
            if (safe == nullptr || result != 1) return;

            auto name = owned->getTextEditorContents ("name").trim();
            if (name.isEmpty()) return;

            auto& self = *safe;

            // Built-in names are reserved.
            for (const auto& b : MainComponent::builtInLayouts())
                if (b.name.equalsIgnoreCase (name)) return;

            // Replace any existing custom layout with the same name.
            for (size_t i = 0; i < self.customLayouts.size(); ++i)
                if (self.customLayouts[i].name.equalsIgnoreCase (name))
                {
                    self.customLayouts.erase (self.customLayouts.begin() + (long) i);
                    break;
                }

            self.customLayouts.push_back (self.captureCurrentLayout (name));
            self.activeLayoutName = name;
            self.persistWorkspaceLayouts();
            self.syncMenuBarState();
        }), false);
}

void MainComponent::deleteWorkspaceLayout (const juce::String& name)
{
    for (size_t i = 0; i < customLayouts.size(); ++i)
        if (customLayouts[i].name == name)
        {
            customLayouts.erase (customLayouts.begin() + (long) i);
            break;
        }

    if (activeLayoutName == name)
        activeLayoutName = {};

    persistWorkspaceLayouts();
    syncMenuBarState();
}

void MainComponent::persistWorkspaceLayouts()
{
    auto* s = audioEngine.getUserSettings();
    if (s == nullptr) return;

    juce::XmlElement root ("Workspaces");
    for (const auto& l : customLayouts)
    {
        auto* e = root.createNewChildElement ("Layout");
        e->setAttribute ("name",               l.name);
        e->setAttribute ("inspectorCollapsed", l.inspectorCollapsed ? 1 : 0);
        e->setAttribute ("browserCollapsed",   l.browserCollapsed   ? 1 : 0);
        e->setAttribute ("mixerDetached",      l.mixerDetached      ? 1 : 0);
        e->setAttribute ("bottomPanel",        l.bottomPanel);
        e->setAttribute ("mixerHeight",        l.mixerHeight);
    }

    s->setValue ("workspaceLayouts", root.toString());
    s->setValue ("activeWorkspaceLayout", activeLayoutName);
    s->saveIfNeeded();
}

void MainComponent::loadWorkspaceLayouts()
{
    customLayouts.clear();

    auto* s = audioEngine.getUserSettings();
    if (s == nullptr) return;

    const auto xmlStr = s->getValue ("workspaceLayouts");
    if (xmlStr.isNotEmpty())
    {
        if (auto xml = juce::XmlDocument::parse (xmlStr))
        {
            for (auto* e : xml->getChildIterator())
            {
                if (! e->hasTagName ("Layout")) continue;

                WorkspaceLayout l;
                l.name               = e->getStringAttribute ("name");
                l.inspectorCollapsed = e->getIntAttribute ("inspectorCollapsed", 0) != 0;
                l.browserCollapsed   = e->getIntAttribute ("browserCollapsed",   0) != 0;
                l.mixerDetached      = e->getIntAttribute ("mixerDetached",       0) != 0;
                l.bottomPanel        = e->getIntAttribute ("bottomPanel", 0);
                l.mixerHeight        = e->getIntAttribute ("mixerHeight", 320);

                if (l.name.isNotEmpty())
                    customLayouts.push_back (l);
            }
        }
    }

    activeLayoutName = s->getValue ("activeWorkspaceLayout");
}

bool MainComponent::projectHasUnsavedChanges() const
{
    // Aerion's flag covers edits made through AudioEngineManager; Tracktion's
    // covers every undoable Edit change, including direct ones such as clip
    // drags in the Timeline. Either means the project needs saving.
    return hasUnsavedChanges || audioEngine.hasUnsavedEdits();
}

void MainComponent::markProjectClean()
{
    hasUnsavedChanges = false;
    audioEngine.markEditSaved();
}

void MainComponent::editStateChanged()
{
    hasUnsavedChanges = true;
    refreshFromEngine();
}

void MainComponent::engineStatusChanged()
{
    refreshFromEngine();
}

void MainComponent::focusNextPane()
{
    juce::Array<juce::Component*> panes { &toolbar, &timeline, &mixer, &transport };

    for (int i = panes.size(); --i >= 0;)
        if (! panes[i]->isShowing())
            panes.remove (i);

    if (panes.isEmpty())
        return;

    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    int current = -1;

    for (int i = 0; i < panes.size(); ++i)
        if (focused != nullptr && (panes[i] == focused || panes[i]->isParentOf (focused)))
            current = i;

    auto* next = panes[(current + 1) % panes.size()];

    if (next == &mixer)
        mixer.focusFirstStrip();
    else
        next->grabKeyboardFocus(); // a pane that does not take focus itself passes it to its first control
}

void MainComponent::pluginFaulted (const juce::String& pluginName, PluginFaultMonitor::Stage stage,
                                   const juce::String& reason)
{
    const auto what = pluginName + " crashed while " + PluginFaultMonitor::describeStage (stage)
                    + " (" + reason + ").\n\n";

    if (stage == PluginFaultMonitor::Stage::processing)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Plugin Crashed",
            what + "Aerion caught the crash and bypassed the plugin, so the rest of your session keeps playing. "
            "Save your project, then restart Aerion before turning the plugin back on: "
            "after a crash its state may be damaged.");
        return;
    }

    juce::String saved;

    if (stage == PluginFaultMonitor::Stage::savingState)
        saved = "Your project still saves, with the settings this plugin had when it was last saved. ";

    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Plugin Crashed",
        what + "Aerion caught the crash and switched the plugin off for the rest of this session. "
        + saved + "Restart Aerion to use it again.");
}

void MainComponent::refreshFromEngine()
{
    AERION_PROFILE_SCOPE ("MainComponent::editStateChanged");

    const auto syncStartMs = juce::Time::getMillisecondCounterHiRes();

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
            else if (i == tracktion::IDs::name)
                trackTree.setProperty (IDs::name, v.getProperty(i), nullptr);
        }

        // The Mixer draws names straight from the engine; repaint it even for
        // tracks ProjectData does not mirror.
        if (i == tracktion::IDs::name)
            mixer.repaint();
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

void MainComponent::attachEmbeddedClipListener (tracktion::MidiClip& clip)
{
    detachEmbeddedClipListener();
    embeddedClipState = clip.state;
    embeddedClipState.addListener (this);
}

void MainComponent::detachEmbeddedClipListener()
{
    if (embeddedClipState.isValid())
    {
        embeddedClipState.removeListener (this);
        embeddedClipState = {};
    }
}

void MainComponent::closeEmbeddedPianoRoll()
{
    detachEmbeddedClipListener();

    if (embeddedPianoRoll != nullptr)
    {
        removeChildComponent (embeddedPianoRoll.get());
        embeddedPianoRoll.reset();
    }

    embeddedClip = nullptr;
}

void MainComponent::valueTreeParentChanged (juce::ValueTree& tree)
{
    // Embedded PianoRollEditor holds a raw MidiClip&. Close it synchronously while
    // the clip object is still alive — the same pattern as PianoRollWindow.
    if (embeddedPianoRoll == nullptr || tree != embeddedClipState)
        return;

    if (embeddedClipState.getParent().isValid())
        return;

    closeEmbeddedPianoRoll();
    bottomPanel = BottomPanel::Mixer;
    resized();
}

