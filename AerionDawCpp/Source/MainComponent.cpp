#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible (menuBar);
    addAndMakeVisible (toolbar);
    addAndMakeVisible (inspector);
    addAndMakeVisible (browser);
    addAndMakeVisible (timeline);
    addAndMakeVisible (mixer);
    addAndMakeVisible (transport);

    audioEngine.addListener (this);
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
            inspector.trackName    = juce::String::formatted ("%02d. ", idx + 1) + t->getName();
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
}

void MainComponent::openProject()
{
    auto chooser = std::make_unique<juce::FileChooser> ("Open Project...", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.aerion");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  audioEngine.loadProject (file);
                              }
                          });
}

void MainComponent::saveProject()
{
    auto chooser = std::make_unique<juce::FileChooser> ("Save Project...", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.aerion");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file != juce::File())
                              {
                                  if (file.getFileExtension() != ".aerion")
                                      file = file.withFileExtension (".aerion");
                                  audioEngine.saveProject (file);
                              }
                          });
}

void MainComponent::importAudioFile()
{
    auto chooser = std::make_unique<juce::FileChooser> ("Import Audio...", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wav;*.mp3;*.aif;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
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

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle                   = "Audio Settings";
    options.dialogBackgroundColour        = juce::Colour::fromString ("#ff1e1e24");
    options.escapeKeyTriggersCloseButton  = true;
    options.useNativeTitleBar             = true;
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

    if (playing)
    {
        timeline.repaint();
        mixer.repaint();
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromString ("#ff0e0e11"));
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
        mixer.setBounds (centerBounds.removeFromBottom (260));
    timeline.setBounds (centerBounds);
}

//==============================================================================
class MainComponent::MixerWindow : public juce::DocumentWindow
{
public:
    MixerWindow (MainComponent& mc)
        : DocumentWindow ("Mixer", juce::Colour::fromString ("#ff17171a"),
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
