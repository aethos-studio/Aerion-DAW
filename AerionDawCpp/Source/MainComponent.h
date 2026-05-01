#pragma once
#include <JuceHeader.h>
#include "ProjectData.h"
#include "AudioEngine.h"
#include "GoogleDriveClient.h"
#include "AIManager.h"
#include "UIComponents.h"

class MainComponent  : public juce::Component,
                       public juce::Timer,
                       public juce::KeyListener,
                       public AudioEngineManager::Listener,
                       public juce::ValueTree::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;

    // AudioEngineManager::Listener
    void editStateChanged() override;

    // juce::ValueTree::Listener
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}

private:
    void createNewProject();
    void openProject();
    void saveProject();
    void importAudioFile();
    void showAudioSettings();

    void detachMixer();
    void reattachMixer();

    AudioEngineManager audioEngine;
    GoogleDriveClient driveClient;
    AIManager aiManager { audioEngine.getEdit() };
    ProjectData projectData;

    DAWMenuBar menuBar;
    DAWToolbar toolbar;
    Inspector  inspector { audioEngine, projectData };
    Browser    browser   { audioEngine };

    Timeline   timeline  { audioEngine, projectData };
    Mixer      mixer     { audioEngine, projectData };
    Transport  transport { audioEngine, projectData };

    MetalLookAndFeel metalLookAndFeel;

    double lastTransportPos = -1.0;
    bool lastIsPlaying = false;

    class MixerWindow;
    std::unique_ptr<MixerWindow> mixerWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
