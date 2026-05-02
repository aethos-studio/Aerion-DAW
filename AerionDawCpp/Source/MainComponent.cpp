#include "MainComponent.h"

namespace te = tracktion;

MainComponent::MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (&metalLookAndFeel);

    addAndMakeVisible (menuBar);
    addAndMakeVisible (toolbar);
    addAndMakeVisible (inspector);
    addAndMakeVisible (browser);
    addAndMakeVisible (timeline);
    addAndMakeVisible (mixer);
    addAndMakeVisible (transport);
    addAndMakeVisible (mixerResizer);

    audioEngine.addListener (this);
    audioEngine.getEdit().state.addListener (this);
    addKeyListener (this);
    setWantsKeyboardFocus (true);

    audioEngine.scanPlugins();

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
        audioEngine.importAudioFile (f);
        timeline.repaint();
        mixer.repaint();
    };

    toolbar.onToggleSnap = [this] {
        timeline.snapEnabled = toolbar.snapEnabled;
    };

    toolbar.onToolChanged = [this] (EditTool t) {
        timeline.activeTool = t;
    };

    menuBar.onNew      = [this] { createNewProject(); };
    menuBar.onOpen     = [this] { openProject(); };
    menuBar.onSave     = [this] { saveProject(); };
    menuBar.onSaveAs   = [this] { saveProjectAs(); };
    menuBar.onImport   = [this] { importAudioFile(); };
    menuBar.onSettings = [this] { showAudioSettings(); };

    timeline.onAddTrack = [this]
    {
        audioEngine.addAudioTrack();
        timeline.repaint();
        mixer.repaint();
    };

    timeline.onImportFile = [this] (const juce::File& f)
    {
        audioEngine.importAudioFile (f);
        timeline.repaint();
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
    startTimerHz (60);
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
}

void MainComponent::openProject()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open Project...", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.aerion");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  audioEngine.loadProject (file);
                                  currentProjectFile = file;
                              }
                          });
}

void MainComponent::saveProject()
{
    if (currentProjectFile.existsAsFile())
    {
        audioEngine.saveProject (currentProjectFile);
    }
    else
    {
        saveProjectAs();
    }
}

void MainComponent::saveProjectAs()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Save Project As...", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.aerion");
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
                              }
                          });
}

void MainComponent::importAudioFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Import Audio...", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wav;*.mp3;*.aif;*.flac");
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

    if (playing || audioEngine.isRecording())
    {
        mixer.repaint();
        inspector.repaint();
        timeline.repaint();
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

    inspector.setBounds (bounds.removeFromLeft (260));
    browser.setBounds   (bounds.removeFromRight (270));

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
        : DocumentWindow ("Mixer", Theme::bgBase,
                          juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          owner (mc)
    {
        setUsingNativeTitleBar (false);
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
