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
                       public AudioEngineManager::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;

    // AudioEngineManager::Listener
    void editStateChanged() override
    {
        // JUCE does not cascade repaint() to children, so invalidate the
        // engine-driven views explicitly.
        timeline.repaint();
        mixer.repaint();
        inspector.repaint();
        browser.repaint();
    }

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
    Inspector  inspector { audioEngine };
    Browser    browser   { audioEngine };

    Timeline   timeline  { audioEngine };
    Mixer      mixer     { audioEngine };
    Transport  transport { audioEngine };

    double lastTransportPos = -1.0;
    bool lastIsPlaying = false;

    class MixerWindow;
    std::unique_ptr<MixerWindow> mixerWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
