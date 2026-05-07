#include "MainComponent.h"

namespace te = tracktion;

MainComponent::MainComponent()
{
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
    audioEngine.getEdit().state.addListener (this);
    addKeyListener (this);
    setWantsKeyboardFocus (true);

    // Defer plugin scan so the main window can appear before the cache load
    // and disk scan starts. The scan runs on a background thread and fires
    // onScanFinished / broadcastChange when done.
    juce::MessageManager::callAsync ([this] { audioEngine.scanPlugins(); });

    browser.onPluginPicked = [this] (const juce::PluginDescription& desc)
    {
        auto sel = timeline.getSelectedTracks();
        if (sel.isEmpty()) return;
        if (auto p = audioEngine.addPluginToTrack (sel[0], desc))
            p->showWindowExplicitly();
    };

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
        if (targetTrack != nullptr)
            audioEngine.insertAudioClipOnTrack (targetTrack, f, pos);
        else
            audioEngine.importAudioFileAtPosition (f, pos);

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

    menuBar.onNew      = [this] { createNewProject(); };
    menuBar.onOpen     = [this] { openProject(); };
    menuBar.onSave     = [this] { saveProject(); };
    menuBar.onSaveAs   = [this] { saveProjectAs(); };
    menuBar.onImport   = [this] { importAudioFile(); };
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
    menuBar.onAddMidiTrack   = [this] {
        auto* track = audioEngine.addAudioTrack();
        if (track != nullptr)
        {
            double pos = audioEngine.getTransportPosition();
            tracktion::TimeRange range (tracktion::TimePosition::fromSeconds (pos),
                                        tracktion::TimeDuration::fromSeconds (2.0));
            track->insertMIDIClip (range, nullptr);
        }
        timeline.repaint(); mixer.repaint();
    };
    menuBar.onAddFolderTrack = [this] { audioEngine.addFolderTrack(); timeline.repaint(); mixer.repaint(); };
    menuBar.onDeleteTrack    = [this] {
        for (auto* t : timeline.getSelectedTracks()) audioEngine.deleteTrack (t);
        timeline.onTrackSelected (-1);
        timeline.repaint(); mixer.repaint();
    };
    menuBar.onToggleTrackArm  = [this] {
        auto sel = timeline.getSelectedTracks();
        if (sel.isEmpty()) return;
        auto* t = sel[0];
        audioEngine.setTrackArmed (t, ! audioEngine.isTrackArmed (t));
        timeline.onTrackSelected (timeline.getSelectedIndex());
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
        if (wasPlaying) audioEngine.play();
        timeline.repaint(); transport.repaint();
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

    menuBar.onBeforeMenuOpen = [this] { syncMenuBarState(); };

    timeline.onAddTrack = [this]
    {
        audioEngine.addAudioTrack();
        timeline.repaint();
        mixer.repaint();
    };

    timeline.onAddMidiTrack = [this]
    {
        auto* track = audioEngine.addAudioTrack();
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
        audioEngine.addFolderTrack();
        timeline.repaint();
        mixer.repaint();
    };

    timeline.onImportFile = [this] (const juce::File& f)
    {
        audioEngine.importAudioFileAtPosition (f, 0.0); // legacy menu-import path
        timeline.repaint();
        mixer.repaint();
    };

    // Studio-One-style drop: place at exact position on specified track (or create new)
    timeline.onImportFiles = [this] (const juce::Array<juce::File>& files,
                                      tracktion::AudioTrack* targetTrack,
                                      double insertTime)
    {
        namespace te = tracktion;
        double cursor = insertTime;
        for (auto& f : files)
        {
            if (targetTrack != nullptr)
                audioEngine.insertAudioClipOnTrack (targetTrack, f, cursor);
            else
                targetTrack = audioEngine.importAudioFileAtPosition (f, cursor);

            te::AudioFile af (audioEngine.getEngine(), f);
            double len = af.getLength();
            if (len > 0.0) cursor += len;
        }
        timeline.repaint();
        mixer.repaint();
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

    timeline.onAddFolder = [this]
    {
        auto sel = timeline.getSelectedTracks();
        if (! sel.isEmpty()) audioEngine.groupTracks (sel);
        else                  audioEngine.addFolderTrack();
        timeline.repaint();
        mixer.repaint();
    };

    timeline.onTrackSelected = [this] (int idx)
    {
        auto top = audioEngine.getTopLevelTracks();
        if (idx >= 0 && idx < top.size())
        {
            auto* t = top[idx];
            inspector.trackIndex   = idx;
            inspector.trackName    = t->getName();
            inspector.armed        = audioEngine.isTrackArmed (t);
            inspector.muted        = t->isMuted (false);
            inspector.solo         = t->isSolo  (false);
            inspector.selectedTrack = t;
        }
        else
        {
            inspector.trackIndex    = -1;
            inspector.trackName     = "(no selection)";
            inspector.armed = inspector.muted = inspector.solo = false;
            inspector.selectedTrack = nullptr;
        }
        inspector.repaint();
    };

    setSize (1400, 860);
    // 25 Hz: playhead + meters; avoids piling on top of other ~30 Hz component timers.
    startTimerHz (25);
}

MainComponent::~MainComponent()
{
    stopTimer();
    audioEngine.removeListener (this);
    removeKeyListener (this);
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key.getModifiers().isCommandDown())
    {
        if (key.getKeyCode() == 'Z')
        {
            if (key.getModifiers().isShiftDown()) audioEngine.redo();
            else                                  audioEngine.undo();
            return true;
        }

        if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
        {
            saveProject();
            return true;
        }
    }

    if (key == juce::KeyPress::spaceKey)
    {
        if (audioEngine.isPlaying())
            audioEngine.stop();
        else
            audioEngine.play();
        
        transport.repaint();
        return true;
    }

    // Force a crossfade on the selected clip with any overlapping neighbor (Studio One-style quick action).
    if (key.getTextCharacter() == 'x' || key.getTextCharacter() == 'X')
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

    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        if (auto* clip = timeline.selectedClip)
        {
            double interval = projectData.getProjectTree().getProperty (IDs::snapInterval);
            if (! (bool) projectData.getProjectTree().getProperty (IDs::snapEnabled))
                interval = 0.1; // small nudge if snap is off

            double delta = (key.getKeyCode() == juce::KeyPress::leftKey) ? -interval : interval;
            
            auto& ts = audioEngine.getEdit().tempoSequence;
            if (key.getModifiers().isAltDown())
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

    if (key == juce::KeyPress::homeKey)
    {
        bool wasPlaying = audioEngine.isPlaying();
        audioEngine.setTransportPosition (0.0);
        
        if (wasPlaying)
            audioEngine.play();
            
        timeline.repaint();
        transport.repaint();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
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

    if (key == juce::KeyPress ('m')) { audioEngine.toggleTrackMute (t); timeline.repaint(); return true; }
    if (key == juce::KeyPress ('s')) { audioEngine.toggleTrackSolo (t); timeline.repaint(); return true; }
    if (key == juce::KeyPress ('r')) {
        audioEngine.setTrackArmed (t, ! audioEngine.isTrackArmed (t));
        timeline.onTrackSelected (timeline.getSelectedIndex()); // updates inspector
        timeline.repaint();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        for (auto* track : selected)
            audioEngine.deleteTrack (track);
        timeline.onTrackSelected (-1);
        timeline.repaint();
        mixer.repaint();
        return true;
    }

    return false;
}

void MainComponent::createNewProject()
{
    audioEngine.createNewProject();
    currentProjectFile = juce::File();
    syncToolbarFromEngine();
}

void MainComponent::updateTitleBar()
{
    juce::String name = currentProjectFile.existsAsFile()
                        ? currentProjectFile.getFileNameWithoutExtension()
                        : "My Song";
    menuBar.projectTitle = name;
    menuBar.repaint();

    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
        dw->setName (name + " - Aerion DAW");
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

void MainComponent::openProject()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open Project...", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Aerion Projects"), "*.aerion");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  audioEngine.loadProject (file);
                                  currentProjectFile = file;
                                  updateTitleBar();
                                  syncToolbarFromEngine();
                              }
                          });
}

void MainComponent::saveProject()
{
    if (currentProjectFile.existsAsFile())
    {
        audioEngine.saveProject (currentProjectFile);
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
                                  audioEngine.saveProject (file);
                                  currentProjectFile = file;
                                  updateTitleBar();
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
    auto* selector = new juce::AudioDeviceSelectorComponent (audioEngine.getEngine().getDeviceManager().deviceManager,
                                                             0, 2, 0, 2, true, true, true, false);
    selector->setSize (500, 450);
    selector->setLookAndFeel (&metalLookAndFeel);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle                   = "Audio Settings";
    options.dialogBackgroundColour        = Theme::bgPanel;
    options.escapeKeyTriggersCloseButton  = true;
    options.useNativeTitleBar             = false;
    options.resizable                     = false;

    options.launchAsync();
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
        // Recording can change waveforms and clip lengths; repaint fully.
        mixer.repaint();
        inspector.repaint();
        timeline.repaint();
        return;
    }

    // Stopped: refresh transport CPU / buffer line occasionally (otherwise it looks "frozen").
    if (! playing && ++idleCpuRefreshTick >= 5) // ~5 Hz at 25 Hz main timer
    {
        idleCpuRefreshTick = 0;
        transport.repaint();
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
    if (! mixer.detached)
    {
        mixer.setBounds (centerBounds.removeFromBottom (mixerHeight));
        mixerResizer.setBounds (centerBounds.removeFromBottom (4));
    }
    else
    {
        mixerResizer.setBounds (0, 0, 0, 0);
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
    projectData.syncWithEngine (audioEngine.getEdit());
    browser.repaint(); // Browser doesn't listen to ProjectData yet
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
