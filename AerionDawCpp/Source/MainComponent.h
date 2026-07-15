#pragma once
#include <JuceHeader.h>
#include "ProjectData.h"
#include "AudioEngine.h"
#include "GoogleDriveClient.h"
#include "AIManager.h"
#include "AerionTooltipWindow.h"
#include "UIComponents.h"
#include "Export/MixdownExportDialog.h"

class MainComponent  : public juce::Component,
                       public juce::DragAndDropContainer,
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

    AudioEngineManager& getAudioEngine() { return audioEngine; }
    void requestQuit();

    // juce::ValueTree::Listener
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}

private:
    void doCreateNewProject();
    void createNewProject();
    void doOpenProjectChooser();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void collectAndSaveAs();
    void importAudioFile();
    void exportMixdown();
    void showAudioSettings();

    void detachMixer();
    void reattachMixer();

    // Workspace layouts — a named snapshot of the arrangement of panels/windows.
    // Persisted app-wide (not per-project) via appProperties, alongside the keymap.
    struct WorkspaceLayout
    {
        juce::String name;
        bool inspectorCollapsed = false;
        bool browserCollapsed   = false;
        bool mixerDetached      = false;
        int  bottomPanel        = 0;   // 0 = Mixer, 1 = Piano Roll
        int  mixerHeight        = 320;
    };

    static std::vector<WorkspaceLayout> builtInLayouts();
    WorkspaceLayout captureCurrentLayout (juce::String name) const;
    void applyWorkspaceLayout (const WorkspaceLayout& layout);
    void applyWorkspaceLayoutByName (const juce::String& name);
    void saveCurrentWorkspaceLayout();
    void deleteWorkspaceLayout (const juce::String& name);
    void loadWorkspaceLayouts();
    void persistWorkspaceLayouts();

    void updateTitleBar();
    void syncToolbarFromEngine();
    void syncMenuBarState();
    void attachToCurrentEditState();
    void detachFromObservedEditState();
    void syncInspectorToTrack (tracktion::Track* track);

    struct MixerResizer : public juce::Component
    {
        MixerResizer (MainComponent& mc) : owner (mc) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
        void mouseDown (const juce::MouseEvent&) override { startHeight = owner.mixerHeight; }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            owner.mixerHeight = juce::jlimit (100, owner.getHeight() - 400, startHeight - e.getDistanceFromDragStartY());
            owner.resized();
        }
        MainComponent& owner;
        int startHeight = 0;
    };

    // Thin clickable strip that collapses/expands the adjacent panel
    struct PanelCollapseBtn : public juce::Component
    {
        bool collapsed = false;
        bool isLeft;   // true = Inspector side, false = Browser side
        std::function<void()> onClick;

        PanelCollapseBtn (bool left) : isLeft (left)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff1a1f2b));
            g.fillRoundedRectangle (b, 3.0f);

            // Arrow chevron
            const float cx = b.getCentreX(), cy = b.getCentreY();
            const float aw = 5.0f, ah = 8.0f;
            bool pointRight = isLeft ? collapsed : !collapsed;
            juce::Path arrow;
            if (pointRight) {
                arrow.addTriangle (cx - aw * 0.5f, cy - ah * 0.5f,
                                   cx + aw * 0.5f, cy,
                                   cx - aw * 0.5f, cy + ah * 0.5f);
            } else {
                arrow.addTriangle (cx + aw * 0.5f, cy - ah * 0.5f,
                                   cx - aw * 0.5f, cy,
                                   cx + aw * 0.5f, cy + ah * 0.5f);
            }
            g.setColour (isMouseOver() ? juce::Colour (0xff63b3ed) : juce::Colour (0xff4a5568));
            g.fillPath (arrow);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { repaint(); }
        void mouseUp    (const juce::MouseEvent&) override
        {
            collapsed = !collapsed;
            repaint();
            if (onClick) onClick();
        }
    };

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

    int mixerHeight = 320;
    MixerResizer    mixerResizer    { *this };
    PanelCollapseBtn inspectorToggle { true  };
    PanelCollapseBtn browserToggle   { false };

    enum class BottomPanel { Mixer, PianoRoll };
    BottomPanel bottomPanel = BottomPanel::Mixer;
    std::unique_ptr<PianoRollEditor> embeddedPianoRoll;
    tracktion::MidiClip* embeddedClip = nullptr;
    juce::TextButton tabMixer     { "MIXER" };
    juce::TextButton tabPianoRoll { "PIANO ROLL" };

    static constexpr int kInspectorW = 260;
    static constexpr int kBrowserW   = 270;
    static constexpr int kToggleW    = 14;

    MetalLookAndFeel metalLookAndFeel;
    std::unique_ptr<AerionTooltipWindow> tooltipWindow;

    double lastTransportPos = -1.0;
    bool lastIsPlaying = false;
    float lastPlayheadX = -10000.0f;
    uint32_t idleCpuRefreshTick = 0;

    class MixerWindow;
    std::unique_ptr<MixerWindow> mixerWindow;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::File currentProjectFile;
    juce::ValueTree observedEditState;
    bool hasUnsavedChanges = false;
    bool pendingNewProjectAfterSave = false;
    bool pendingQuitAfterSave = false;

    int autoSaveElapsedMs = 0;
    int autoSaveIntervalMs = 5 * 60 * 1000;  // 5 min default

    std::vector<WorkspaceLayout> customLayouts;
    juce::String activeLayoutName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
