#pragma once
#include <JuceHeader.h>
#include "ProjectData.h"
#include "AudioEngine.h"
#include "GoogleDriveClient.h"
#include "AIManager.h"
#include "AerionTooltipWindow.h"
#include "UIComponents.h"
#include "Views/ConsolePanel.h"
#include "Export/MixdownExportDialog.h"
#include "UI/GraphicsEngine.h"
#include "UI/UiScale.h"
#include "Updates/Updater.h"

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

    // The main window only gets its native peer once it is on the desktop, so
    // the graphics engine choice is applied here rather than in the constructor.
    void parentHierarchyChanged() override { graphicsEngine.apply(); }

    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;

    // AudioEngineManager::Listener
    void editStateChanged() override;
    void engineStatusChanged() override;
    void pluginFaulted (const juce::String& pluginName, PluginFaultMonitor::Stage, const juce::String& reason) override;

    AudioEngineManager& getAudioEngine() { return audioEngine; }
    void requestQuit();

    // juce::ValueTree::Listener
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override;

private:
    /** F6: moves keyboard focus to the next of toolbar, Timeline, Mixer and transport. */
    void focusNextPane();

    void doCreateNewProject();
    void openTemplate (const juce::File& templateFile);
    void saveProjectAsTemplate();
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
    std::unique_ptr<DAWMenuBar::SystemMenuBar> systemMenuBar;   // macOS: the menus at the top of the screen
    DAWToolbar toolbar;
    Inspector  inspector { audioEngine, projectData };
    Browser    browser   { audioEngine };

    Timeline   timeline  { audioEngine, projectData };
    Mixer      mixer     { audioEngine, projectData };
    Transport  transport { audioEngine, projectData };
    TimelinePlayheadOverlay playheadOverlay { timeline, audioEngine };

    int mixerHeight = 320;
    MixerResizer    mixerResizer    { *this };
    PanelCollapseBtn inspectorToggle { true  };
    PanelCollapseBtn browserToggle   { false };

    enum class BottomPanel { Mixer, PianoRoll, Console };
    BottomPanel bottomPanel = BottomPanel::Mixer;
    std::unique_ptr<PianoRollEditor> embeddedPianoRoll;
    tracktion::MidiClip* embeddedClip = nullptr;
    juce::ValueTree embeddedClipState; // listened for parent-change so we can close before UAF
    void attachEmbeddedClipListener (tracktion::MidiClip& clip);
    void detachEmbeddedClipListener();
    void closeEmbeddedPianoRoll();
    juce::TextButton tabMixer     { "MIXER" };
    juce::TextButton tabPianoRoll { "PIANO ROLL" };
    juce::TextButton tabConsole   { "CONSOLE" };
    ConsolePanel consolePanel;

    static constexpr int kInspectorW = 260;
    static constexpr int kBrowserW   = 270;
    static constexpr int kToggleW    = 14;

    MetalLookAndFeel metalLookAndFeel;
    std::unique_ptr<AerionTooltipWindow> tooltipWindow;

    GraphicsEngine::Policy graphicsEngine;

    double lastTransportPos = -1.0;
    bool lastIsPlaying = false;
    double lastMeterFrameSec = 0.0;
    double lastPlayheadFrameSec = 0.0;

    // View -> Lightweight UI: 0 = Auto, 1 = On, 2 = Off (see Theme::lightweightUi).
    static constexpr const char* kLightweightUiKey = "lightweightUi";
    int lightweightUiChoice = 0;
    void applyLightweightUi (int choice);

    // View -> UI Size: 0 = Auto, else percent (see UI/UiScale.h). Main.cpp
    // applies the saved size at startup, before any window opens.
    int uiSizeChoice = 0;
    void applyUiSize (int choice);

    // Transport -> Follow Playback (also the toolbar button and F): the
    // Timeline pages to keep the playhead in view. Off by default; remembered
    // for the user, not the project.
    static constexpr const char* kFollowPlaybackKey = kFollowPlaybackSettingKey;
    void setFollowPlayback (bool shouldFollow);
    double meterTailUntilSec = 0.0;
    juce::uint32 lastChoreMs = 0;

    // Drives periodic profiling reports off the existing chores timer; compiles
    // away entirely unless AERION_ENABLE_PROFILING is on.
    AERION_PROFILE_REPORTER (profileReporter);

    class MixerWindow;
    std::unique_ptr<MixerWindow> mixerWindow;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::File currentProjectFile;
    juce::ValueTree observedEditState;
    bool hasUnsavedChanges = false;          // set by editStateChanged; see projectHasUnsavedChanges()
    bool titleShowsUnsavedChanges = false;

    bool projectHasUnsavedChanges() const;
    void markProjectClean();
    void refreshFromEngine();
    bool pendingNewProjectAfterSave = false;
    bool pendingQuitAfterSave = false;

    int autoSaveElapsedMs = 0;
    int autoSaveIntervalMs = 5 * 60 * 1000;  // 5 min default

    std::vector<WorkspaceLayout> customLayouts;
    juce::String activeLayoutName;

    // Help -> Check for Updates, and the quiet check after startup.
    Updater updater { audioEngine.getUserSettings() };

    // Display-synced clock for everything that animates during playback. Declared
    // last so it is destroyed first, before anything its callback touches.
    void onDisplayFrame (double nowSec);
    juce::VBlankAttachment displayClock { this, [this] (double nowSec) { onDisplayFrame (nowSec); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
