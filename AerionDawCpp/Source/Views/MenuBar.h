#pragma once

// Custom-drawn application menu bar.

#include "ViewShared.h"
#include "AboutDialog.h"

//==============================================================================
class DAWMenuBar : public juce::Component,
                   private juce::Timer
{
public:
    // === Sync state  -  populated by onBeforeMenuOpen ===
    bool   snapEnabled      = false;
    double snapInterval     = 0.25;
    bool   autoCrossfadeOn  = true;
    int    autoCrossfadeMaxMs = 120;
    bool   metronomeOn      = false;
    int    countInBars      = 0;
    bool   punchEnabled     = false;
    bool   pdcEnabled       = false;
    bool   loopEnabled      = false;
    bool   inspectorVisible = true;
    bool   browserVisible   = true;
    bool   mixerDetached    = false;
    juce::StringArray builtInWorkspaceNames;
    juce::StringArray customWorkspaceNames;
    juce::String      activeWorkspaceName;
    int          graphicsEngineChoice = 0;   // GraphicsEngine::Choice
    juce::String graphicsEngineInUse;        // e.g. "Software Renderer"
    int          lightweightUiChoice  = 0;   // 0 = Auto, 1 = On, 2 = Off
    bool         lightweightUiActive  = false;
    bool   hasSelectedTrack = false;
    bool   hasSelectedClip  = false;
    bool   trackArmed       = false;
    bool   trackMuted       = false;
    bool   trackSolo        = false;
    juce::String projectTitle = "My Song";

    // === Recent projects ===
    juce::RecentlyOpenedFilesList*    recentProjects     = nullptr;

    // === Callbacks ===
    std::function<void()> onNew, onOpen, onSave, onSaveAs, onImport, onExportMixdown, onSettings;
    std::function<void()> onUndo, onRedo;
    std::function<void()> onToggleMetronome, onShowMetronomeSettings;
    std::function<void()> onToggleSnap;
    std::function<void(double)> onSnapIntervalChanged;
    std::function<void(int)>    onCountInChanged;
    std::function<void()> onAddAudioTrack, onAddMidiTrack, onAddFolderTrack;
    std::function<void()> onDeleteTrack;
    std::function<void()> onToggleTrackArm, onToggleTrackMute, onToggleTrackSolo;
    std::function<void()> onNudgeLeft, onNudgeRight, onTrimLeft, onTrimRight, onDeleteEvent;
    std::function<void()> onRescanPlugins, onTogglePdc;
    std::function<void()> onToggleAutoCrossfade;
    std::function<void(int)> onAutoCrossfadeMaxChanged;
    std::function<void()> onPlay, onStop, onRecord, onGoToStart;
    std::function<void()> onToggleLoop, onTogglePunch;
    std::function<void()> onToggleInspector, onToggleBrowser, onToggleMixerDetach;
    std::function<void(juce::String)> onApplyWorkspace;
    std::function<void()>             onSaveWorkspace;
    std::function<void(juce::String)> onDeleteWorkspace;
    std::function<void(int)>          onGraphicsEngineChanged;
    std::function<void(int)>          onLightweightUiChanged;
    std::function<void(juce::File)>   onOpenRecent;
    std::function<void()>             onClearRecent;
    std::function<void()>             onCollectSaveAs;
    std::function<void()>             onShowKeyboardShortcuts;
    std::function<void()> onBeforeMenuOpen;

    DAWMenuBar()
    {
        if (auto x = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::aerion_logo_ui_svg, BinaryData::aerion_logo_ui_svgSize)))
            logoDrawable = juce::Drawable::createFromSVG (*x);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bgBase);

        auto b = getLocalBounds();
        if (logoDrawable)
        {
            auto logoArea = b.removeFromLeft (40).reduced (8);
            logoDrawable->drawWithin (g, logoArea.toFloat(),
                juce::RectanglePlacement (juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid), 1.0f);
        }

        static const juce::StringArray kItems { "File", "Edit", "Song", "Track", "Event", "Audio", "Transport", "View", "Help" };
        g.setFont (Theme::uiSize (12.0f));
        int x = 50;
        for (int i = 0; i < kItems.size(); ++i)
        {
            bool hov = (i == hoveredMenu);
            if (hov)
            {
                g.setColour (Theme::surface.brighter (0.1f));
                g.fillRoundedRectangle (juce::Rectangle<float> ((float)(x - 2), 4.f, 58.f, (float)(getHeight() - 8)), 3.0f);
            }
            g.setColour (hov ? Theme::accent : Theme::textMuted);
            g.drawText (kItems[i], x, 0, 60, getHeight(), juce::Justification::centred);
            x += 60;
        }

        g.setColour (Theme::textMuted.withAlpha (0.5f));
        g.setFont (Theme::uiSize (11.0f));
        g.drawText (Theme::windowTitle (projectTitle), 0, 0, getWidth(), getHeight(), juce::Justification::centred);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int idx = menuIndexAt (e.x);
        if (idx != hoveredMenu) { hoveredMenu = idx; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (openMenuIndex < 0 && hoveredMenu != -1) { hoveredMenu = -1; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int idx = menuIndexAt (e.x);

        // Clicking the open menu's title closes it, as in other menu bars.
        if (idx >= 0 && idx == openMenuIndex)
        {
            closeOpenMenu();
            return;
        }

        openMenu (idx);
    }

private:
    std::unique_ptr<juce::Drawable> logoDrawable;
    int hoveredMenu = -1;
    int lastPopupMenuIndex = -1;
    int openMenuIndex = -1;    // the menu showing now, or -1
    int menuGeneration = 0;    // tells a dismissed menu's callback from the open one's

    void openMenu (int idx)
    {
        if (idx < 0)
            return;

        if (onBeforeMenuOpen) onBeforeMenuOpen();

        ++menuGeneration;
        openMenuIndex = idx;
        hoveredMenu = idx;
        lastPopupMenuIndex = idx;
        repaint();

        // An open menu takes the mouse, so the bar polls the pointer to switch
        // menus when it moves onto another title.
        startTimerHz (30);

        switch (idx)
        {
            case 0: showFileMenu();      break;
            case 1: showEditMenu();      break;
            case 2: showSongMenu();      break;
            case 3: showTrackMenu();     break;
            case 4: showEventMenu();     break;
            case 5: showAudioMenu();     break;
            case 6: showTransportMenu(); break;
            case 7: showViewMenu();      break;
            case 8: showHelpMenu();      break;
            default: break;
        }
    }

    void closeOpenMenu()
    {
        ++menuGeneration;
        openMenuIndex = -1;
        stopTimer();
        juce::PopupMenu::dismissAllActiveMenus();
        repaint();
    }

    void timerCallback() override
    {
        if (openMenuIndex < 0)
        {
            stopTimer();
            return;
        }

        const auto pos = getLocalPoint (nullptr, juce::Desktop::getMousePosition());
        if (! getLocalBounds().contains (pos))
            return;

        const int idx = menuIndexAt (pos.x);
        if (idx >= 0 && idx != openMenuIndex)
        {
            ++menuGeneration;   // the dismissed menu's callback must not reset the new one
            juce::PopupMenu::dismissAllActiveMenus();
            openMenu (idx);
        }
    }

    /** Shows a title's menu below it; onResult gets the chosen item id, or 0. */
    void showMenu (juce::PopupMenu& m, std::function<void (int)> onResult)
    {
        const int generation = menuGeneration;
        m.showMenuAsync (anchoredMenuOptions(), [this, generation, onResult = std::move (onResult)] (int r)
        {
            if (generation == menuGeneration)
            {
                openMenuIndex = -1;
                stopTimer();
                const auto pos = getMouseXYRelative();
                hoveredMenu = getLocalBounds().contains (pos) ? menuIndexAt (pos.x) : -1;
                repaint();
            }

            onResult (r);
        });
    }

    juce::Rectangle<int> menuItemBounds (int idx) const
    {
        if (idx < 0) return {};
        constexpr int menuX0 = 50;
        constexpr int menuW  = 60;
        return { menuX0 + idx * menuW, 0, menuW, getHeight() };
    }

    juce::PopupMenu::Options anchoredMenuOptions() const
    {
        auto b = menuItemBounds (lastPopupMenuIndex);
        if (b.isEmpty())
            b = { juce::jmax (0, getMouseXYRelative().x), 0, 1, getHeight() };

        return juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (b));
    }

    int menuIndexAt (int x) const
    {
        if (x < 50) return -1;
        int i = (x - 50) / 60;
        return (i >= 0 && i < 9) ? i : -1;
    }

    void showFileMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "New Project");
        m.addItem (2, "Open Project...");

        // Open Recent submenu
        juce::PopupMenu recentSub;
        if (recentProjects != nullptr && recentProjects->getNumFiles() > 0)
        {
            for (int i = 0; i < recentProjects->getNumFiles(); ++i)
            {
                auto f = recentProjects->getFile (i);
                bool exists = f.existsAsFile();
                recentSub.addItem (200 + i, f.getFileName(), exists, false);
            }
            recentSub.addSeparator();
        }
        recentSub.addItem (299, "Clear Recent");
        m.addSubMenu ("Open Recent", recentSub);

        m.addItem (3, "Save Project");
        m.addItem (6, "Save Project As...");
        m.addItem (8, "Collect & Save As...");
        m.addSeparator();
        m.addItem (4, "Import Audio File...");
        m.addItem (7, "Export Mixdown...");
        m.addSeparator();
        m.addItem (5, "Audio Settings...");
        showMenu (m, [this] (int r) {
            if (r == 1 && onNew)      onNew();
            if (r == 2 && onOpen)     onOpen();
            if (r == 3 && onSave)     onSave();
            if (r == 6 && onSaveAs)   onSaveAs();
            if (r == 8 && onCollectSaveAs) onCollectSaveAs();
            if (r == 4 && onImport)   onImport();
            if (r == 7 && onExportMixdown) onExportMixdown();
            if (r == 5 && onSettings) onSettings();
            if (r >= 200 && r < 299 && onOpenRecent && recentProjects != nullptr)
            {
                auto f = recentProjects->getFile (r - 200);
                if (f.existsAsFile()) onOpenRecent (f);
            }
            if (r == 299 && onClearRecent) onClearRecent();
        });
    }

    void showEditMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Undo\tCtrl+Z");
        m.addItem (2, "Redo\tCtrl+Shift+Z");
        showMenu (m, [this] (int r) {
            if (r == 1 && onUndo) onUndo();
            if (r == 2 && onRedo) onRedo();
        });
    }

    void showSongMenu()
    {
        juce::PopupMenu snapSub;
        const std::pair<const char*, double> snaps[] = {
            {"1 Bar", 1.0}, {"1/2", 0.5}, {"1/4", 0.25}, {"1/8", 0.125}, {"1/16", 0.0625}
        };
        for (int i = 0; i < 5; ++i)
            snapSub.addItem (10 + i, snaps[i].first, true, std::abs (snapInterval - snaps[i].second) < 0.001);

        juce::PopupMenu countInSub;
        countInSub.addItem (20, "Off",    true, countInBars == 0);
        countInSub.addItem (21, "1 Bar",  true, countInBars == 1);
        countInSub.addItem (22, "2 Bars", true, countInBars == 2);

        juce::PopupMenu m;
        m.addItem (1, "Metronome",             true, metronomeOn);
        m.addItem (2, "Metronome Settings...");
        m.addSeparator();
        m.addItem (3, "Snap to Grid",          true, snapEnabled);
        m.addSubMenu ("Snap Interval", snapSub);
        m.addSeparator();
        m.addSubMenu ("Count-In", countInSub);
        showMenu (m, [this] (int r) {
            if (r == 1  && onToggleMetronome)       onToggleMetronome();
            if (r == 2  && onShowMetronomeSettings)  onShowMetronomeSettings();
            if (r == 3  && onToggleSnap)             onToggleSnap();
            const double snapVals[] = { 1.0, 0.5, 0.25, 0.125, 0.0625 };
            if (r >= 10 && r <= 14 && onSnapIntervalChanged) onSnapIntervalChanged (snapVals[r - 10]);
            if (r == 20 && onCountInChanged) onCountInChanged (0);
            if (r == 21 && onCountInChanged) onCountInChanged (1);
            if (r == 22 && onCountInChanged) onCountInChanged (2);
        });
    }

    void showTrackMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Add Audio Track");
        m.addItem (2, "Add MIDI Track");
        m.addItem (3, "Add Folder Track");
        m.addSeparator();
        m.addItem (4, "Delete Track", hasSelectedTrack, false);
        m.addSeparator();
        m.addItem (5, "Arm",  hasSelectedTrack, trackArmed);
        m.addItem (6, "Mute", hasSelectedTrack, trackMuted);
        m.addItem (7, "Solo", hasSelectedTrack, trackSolo);
        showMenu (m, [this] (int r) {
            if (r == 1 && onAddAudioTrack)   onAddAudioTrack();
            if (r == 2 && onAddMidiTrack)    onAddMidiTrack();
            if (r == 3 && onAddFolderTrack)  onAddFolderTrack();
            if (r == 4 && onDeleteTrack)     onDeleteTrack();
            if (r == 5 && onToggleTrackArm)  onToggleTrackArm();
            if (r == 6 && onToggleTrackMute) onToggleTrackMute();
            if (r == 7 && onToggleTrackSolo) onToggleTrackSolo();
        });
    }

    void showEventMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Nudge Left",  hasSelectedClip, false);
        m.addItem (2, "Nudge Right", hasSelectedClip, false);
        m.addSeparator();
        m.addItem (3, "Trim Left",   hasSelectedClip, false);
        m.addItem (4, "Trim Right",  hasSelectedClip, false);
        m.addSeparator();
        m.addItem (5, "Delete",      hasSelectedClip, false);
        showMenu (m, [this] (int r) {
            if (r == 1 && onNudgeLeft)   onNudgeLeft();
            if (r == 2 && onNudgeRight)  onNudgeRight();
            if (r == 3 && onTrimLeft)    onTrimLeft();
            if (r == 4 && onTrimRight)   onTrimRight();
            if (r == 5 && onDeleteEvent) onDeleteEvent();
        });
    }

    void showAudioMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Audio Settings...");
        m.addSeparator();
        m.addItem (2, "Rescan Plugins");
        m.addSeparator();
        m.addItem (3, "Plugin Delay Compensation", true, pdcEnabled);
        m.addItem (4, "Auto Crossfade",            true, autoCrossfadeOn);

        juce::PopupMenu xfadeLen;
        for (auto ms : { 10, 25, 50, 80, 120, 200, 500 })
            xfadeLen.addItem (1000 + ms, juce::String (ms) + " ms", true, autoCrossfadeMaxMs == ms);
        m.addSubMenu ("Auto Crossfade Length", xfadeLen, autoCrossfadeOn);

        showMenu (m, [this] (int r) {
            if (r == 1 && onSettings)      onSettings();
            if (r == 2 && onRescanPlugins) onRescanPlugins();
            if (r == 3 && onTogglePdc)     onTogglePdc();
            if (r == 4 && onToggleAutoCrossfade) onToggleAutoCrossfade();
            if (r >= 1000 && onAutoCrossfadeMaxChanged) onAutoCrossfadeMaxChanged (r - 1000);
        });
    }

    void showTransportMenu()
    {
        juce::PopupMenu countInSub;
        countInSub.addItem (10, "Off",    true, countInBars == 0);
        countInSub.addItem (11, "1 Bar",  true, countInBars == 1);
        countInSub.addItem (12, "2 Bars", true, countInBars == 2);

        juce::PopupMenu m;
        m.addItem (1, "Play / Pause\tSpace");
        m.addItem (2, "Stop");
        m.addItem (3, "Record");
        m.addSeparator();
        m.addItem (4, "Go to Start\tHome");
        m.addSeparator();
        m.addItem (5, "Loop",         true, loopEnabled);
        m.addItem (6, "Punch In/Out", true, punchEnabled);
        m.addSubMenu ("Count-In", countInSub);
        showMenu (m, [this] (int r) {
            if (r == 1  && onPlay)           onPlay();
            if (r == 2  && onStop)           onStop();
            if (r == 3  && onRecord)         onRecord();
            if (r == 4  && onGoToStart)      onGoToStart();
            if (r == 5  && onToggleLoop)     onToggleLoop();
            if (r == 6  && onTogglePunch)    onTogglePunch();
            if (r == 10 && onCountInChanged) onCountInChanged (0);
            if (r == 11 && onCountInChanged) onCountInChanged (1);
            if (r == 12 && onCountInChanged) onCountInChanged (2);
        });
    }

    void showViewMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Inspector", true, inspectorVisible);
        m.addItem (2, "Browser",   true, browserVisible);
        m.addSeparator();
        m.addItem (3, mixerDetached ? "Dock Mixer" : "Detach Mixer");
        m.addSeparator();

        juce::PopupMenu wsSub;
        for (int i = 0; i < builtInWorkspaceNames.size(); ++i)
            wsSub.addItem (100 + i, builtInWorkspaceNames[i], true,
                           activeWorkspaceName == builtInWorkspaceNames[i]);

        if (customWorkspaceNames.size() > 0)
        {
            wsSub.addSeparator();
            for (int i = 0; i < customWorkspaceNames.size(); ++i)
                wsSub.addItem (200 + i, customWorkspaceNames[i], true,
                               activeWorkspaceName == customWorkspaceNames[i]);
        }

        wsSub.addSeparator();
        wsSub.addItem (300, "Save Current Layout...");

        if (customWorkspaceNames.size() > 0)
        {
            juce::PopupMenu delSub;
            for (int i = 0; i < customWorkspaceNames.size(); ++i)
                delSub.addItem (400 + i, customWorkspaceNames[i]);
            wsSub.addSubMenu ("Delete Layout", delSub);
        }

        m.addSubMenu ("Workspace", wsSub);

        // Renderer for all windows; Auto is resolved per machine (UI/GraphicsEngine.h).
        juce::PopupMenu gfxSub;
        gfxSub.addItem (500, "Auto" + (graphicsEngineChoice == 0 && graphicsEngineInUse.isNotEmpty()
                                          ? " (" + graphicsEngineInUse + ")" : juce::String()),
                        true, graphicsEngineChoice == 0);
        gfxSub.addItem (501, "Hardware Accelerated", true, graphicsEngineChoice == 1);
        gfxSub.addItem (502, "Software",             true, graphicsEngineChoice == 2);
        m.addSubMenu ("Graphics Engine", gfxSub);

        // Flat fills and lower animation rates for weak machines.
        juce::PopupMenu lightSub;
        lightSub.addItem (600, juce::String ("Auto (") + (lightweightUiActive ? "On" : "Off") + ")",
                          true, lightweightUiChoice == 0);
        lightSub.addItem (601, "On",  true, lightweightUiChoice == 1);
        lightSub.addItem (602, "Off", true, lightweightUiChoice == 2);
        m.addSubMenu ("Lightweight UI", lightSub);

        showMenu (m, [this] (int r) {
            if (r >= 500 && r <= 502 && onGraphicsEngineChanged)
                onGraphicsEngineChanged (r - 500);
            if (r >= 600 && r <= 602 && onLightweightUiChanged)
                onLightweightUiChanged (r - 600);
            if (r == 1 && onToggleInspector)   onToggleInspector();
            if (r == 2 && onToggleBrowser)     onToggleBrowser();
            if (r == 3 && onToggleMixerDetach) onToggleMixerDetach();
            if (r >= 100 && r < 200 && onApplyWorkspace && (r - 100) < builtInWorkspaceNames.size())
                onApplyWorkspace (builtInWorkspaceNames[r - 100]);
            if (r >= 200 && r < 300 && onApplyWorkspace && (r - 200) < customWorkspaceNames.size())
                onApplyWorkspace (customWorkspaceNames[r - 200]);
            if (r == 300 && onSaveWorkspace)
                onSaveWorkspace();
            if (r >= 400 && r < 500 && onDeleteWorkspace && (r - 400) < customWorkspaceNames.size())
                onDeleteWorkspace (customWorkspaceNames[r - 400]);
        });
    }

    void showHelpMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Keyboard Shortcuts...");
        m.addSeparator();
        m.addItem (2, "About Aerion DAW");
        showMenu (m, [this] (int r) {
            if (r == 1 && onShowKeyboardShortcuts)
                onShowKeyboardShortcuts();
            if (r == 2)
                AboutDialog::launch();
        });
    }
};
