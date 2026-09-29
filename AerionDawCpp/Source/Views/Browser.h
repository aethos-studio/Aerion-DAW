#pragma once

// Right-hand Browser panel (files, plugins, cloud).

#include "ViewShared.h"

//==============================================================================
// Right-hand placeholder browser.
class Browser : public juce::Component,
                public juce::ChangeListener
{
public:
    Browser (AudioEngineManager& ae) : audioEngine (ae),
                                       thumb (512, formatManager, thumbCache)
    {
        formatManager.registerBasicFormats();
        thumb.addChangeListener (this);

        currentDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Aerion Projects");
        if (! currentDir.isDirectory())
            currentDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! currentDir.isDirectory())
            currentDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        refreshFileCache();
    }

    ~Browser() override { thumb.removeChangeListener (this); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    enum class Tab { plugins, files, cloud };

    std::function<void (const juce::PluginDescription&)> onPluginPicked;
    std::function<void (const juce::File&)>              onFilePicked;
    std::function<void (const juce::File&)>              onFileDoubleClicked;
    std::function<void()>                                onRescanRequested;

    void setDriveClient (GoogleDriveClient* client)
    {
        driveClient = client;
        if (driveClient != nullptr)
        {
            driveClient->onLoginStateChanged = [this] (bool) { repaint(); };
            driveClient->onFilesListed = [this] (juce::Array<GoogleDriveClient::DriveFile> files)
            {
                driveFiles    = std::move (files);
                driveLoading  = false;
                repaint();
            };
        }
    }

    void setDriveFiles (juce::Array<GoogleDriveClient::DriveFile> files)
    {
        driveFiles = std::move (files);
        driveLoading = false;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        Theme::fillBackgroundGradient (g, getLocalBounds());
        g.setColour (Theme::border.withAlpha (0.7f));
        g.drawRect (getLocalBounds(), 1);

        auto header = getLocalBounds().removeFromTop(32);
        Theme::fillVerticalGradient (g, header, Theme::surface.brighter (0.08f), Theme::surface.darker (0.06f));
        g.setColour(Theme::border);
        g.drawLine(0.0f, 32.0f, (float)getWidth(), 32.0f);

        // Rescan button (Plugins tab only).
        if (tab == Tab::plugins)
        {
            rescanBtn = header.removeFromRight (60).reduced (4, 6);
            bool scanning = audioEngine.isScanningPlugins();
            g.setColour (scanning ? Theme::active.withAlpha (0.2f) : Theme::surface);
            g.fillRoundedRectangle (rescanBtn.toFloat(), 3.0f);
            g.setColour (scanning ? Theme::active : Theme::border);
            g.drawRoundedRectangle (rescanBtn.toFloat(), 3.0f, 1.0f);
            g.setColour (scanning ? Theme::active : Theme::textMuted);
            g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
            g.drawText (scanning ? "SCANNING" : "RESCAN", rescanBtn, juce::Justification::centred);
        }
        else
        {
            rescanBtn = {};
        }

        int third = header.getWidth() / 3;
        pluginsTabBounds = header.removeFromLeft (third);
        filesTabBounds   = header.removeFromLeft (third);
        cloudTabBounds   = header;

        g.setFont (Theme::uiSize (11.0f).withStyle (juce::Font::bold));
        for (auto [b, label, t] : { std::tuple { pluginsTabBounds, "Plugins", Tab::plugins },
                                    std::tuple { filesTabBounds,   "Files",   Tab::files   },
                                    std::tuple { cloudTabBounds,   "Cloud",   Tab::cloud   } })
        {
            bool active = (tab == t);
            g.setColour (active ? Theme::active : Theme::textMuted);
            g.drawText (label, b, juce::Justification::centred);
            if (active)
                g.fillRect ((float)b.getX(), (float)(b.getBottom() - 2), (float)b.getWidth(), 2.0f);
        }

        rowBounds.clearQuick();
        rowDescs.clearQuick();
        rowFiles.clearQuick();

        if (tab == Tab::plugins) paintPluginList (g);
        else if (tab == Tab::files) paintFileList (g);
        else paintCloudTab (g);

        // Waveform preview strip at the bottom (files tab only)
        if (tab == Tab::files)
        {
            auto pa = getLocalBounds().removeFromBottom (kPreviewH);
            g.setColour (Theme::bgPanel.darker (0.08f));
            g.fillRect (pa);
            g.setColour (Theme::border);
            g.drawLine (0.0f, (float) pa.getY(), (float) getWidth(), (float) pa.getY(), 1.0f);

            if (selectedFile.existsAsFile() && thumb.isFullyLoaded() && thumb.getTotalLength() > 0.0)
            {
                auto wa = pa.reduced (8, 10);
                auto wf = wa.withTrimmedBottom (14);
                g.setColour (juce::Colours::black.withAlpha (0.65f));
                thumb.drawChannels (g, wf, 0.0, thumb.getTotalLength(), 1.0f);

                g.setColour (Theme::textMain.withAlpha (1.0f));
                thumb.drawChannels (g, wf, 0.0, thumb.getTotalLength(), 1.0f);
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    g.addTransform (juce::AffineTransform::translation (0.0f, -1.0f));
                    thumb.drawChannels (g, wf, 0.0, thumb.getTotalLength(), 1.0f);
                }
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    g.addTransform (juce::AffineTransform::translation (0.0f,  1.0f));
                    thumb.drawChannels (g, wf, 0.0, thumb.getTotalLength(), 1.0f);
                }
                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (9.0f));
                g.drawText (selectedFile.getFileName(), wa.withTop (wa.getBottom() - 13),
                            juce::Justification::centred);
            }
            else if (selectedFile.existsAsFile())
            {
                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (11.0f));
                g.drawText ("Loading preview...", pa, juce::Justification::centred);
            }
            else
            {
                g.setColour (Theme::textMuted.withAlpha (0.5f));
                g.setFont (Theme::uiSize (11.0f));
                g.drawText ("Click an audio file to preview", pa, juce::Justification::centred);
            }
        }
    }

    void paintPluginList (juce::Graphics& g)
    {
        auto& fm    = audioEngine.getEngine().getPluginManager().pluginFormatManager;
        auto& known = audioEngine.getEngine().getPluginManager().knownPluginList;
        auto types  = known.getTypes();

        int y = 40;
        g.setFont (Theme::uiSize (11.0f));

        if (types.isEmpty())
        {
            g.setColour (Theme::textMuted);
            g.drawText (audioEngine.isScanningPlugins() ? "Scanning plugins..." : "No plugins found.",
                        12, y, getWidth() - 16, 22, juce::Justification::centredLeft);
            y += 22;
            g.setFont (Theme::uiSize (9.0f));
            g.drawText (juce::String::formatted ("Formats registered: %d", fm.getNumFormats()),
                        12, y, getWidth() - 16, 18, juce::Justification::centredLeft);
            y += 18;
            for (int i = 0; i < fm.getNumFormats(); ++i)
                if (auto* fmt = fm.getFormat (i))
                {
                    g.drawText ("  " + fmt->getName(), 12, y, getWidth() - 16, 16, juce::Justification::centredLeft);
                    y += 16;
                }
            return;
        }

        juce::StringArray seenManufacturers;
        for (auto& d : types)
        {
            if (seenManufacturers.indexOf (d.manufacturerName) < 0)
            {
                seenManufacturers.add (d.manufacturerName);
                g.setColour (Theme::textMuted);
                g.setFont (Theme::uiSize (10.0f).withStyle (juce::Font::bold));
                g.drawText (d.manufacturerName.toUpperCase(), 12, y, getWidth() - 16, 18,
                            juce::Justification::centredLeft);
                y += 18;
            }

            juce::Rectangle<int> r (8, y, getWidth() - 16, 22);
            rowBounds.add (r);
            rowDescs.add (d);

            g.setColour (Theme::textMain);
            g.setFont (Theme::uiSize (11.0f));
            g.drawText (d.name, r.withTrimmedLeft (12), juce::Justification::centredLeft);
            g.setColour (Theme::textMuted);
            g.setFont (Theme::uiSize (9.0f));
            g.drawText (d.pluginFormatName, r.withTrimmedRight (8), juce::Justification::centredRight);

            y += 22;
            if (y > getHeight()) break;
        }
    }

    void paintFileList (juce::Graphics& g)
    {
        // Path strip.
        juce::Rectangle<int> path (8, 38, getWidth() - 16, 20);
        g.setColour (Theme::textMuted);
        g.setFont (Theme::uiSize (10.0f));
        g.drawText (currentDir.getFullPathName(), path, juce::Justification::centredLeft);

        int y = 64;
        g.setFont (Theme::uiSize (11.0f));

        // Up directory entry.
        if (currentDir.getParentDirectory() != currentDir)
        {
            juce::Rectangle<int> r (8, y, getWidth() - 16, 22);
            rowBounds.add (r);
            rowFiles.add (currentDir.getParentDirectory());
            rowDescs.add ({});

            g.setColour (Theme::active);
            g.drawText ("..", r.withTrimmedLeft (12), juce::Justification::centredLeft);
            y += 22;
        }

        for (auto& f : cachedFileChildren)
        {
            juce::Rectangle<int> r (8, y, getWidth() - 16, 22);
            rowBounds.add (r);
            rowFiles.add (f);
            rowDescs.add ({});

            g.setColour (f.isDirectory() ? Theme::active : Theme::textMain);
            g.drawText ((f.isDirectory() ? juce::String ("[D] ") : juce::String ("    ")) + f.getFileName(),
                        r.withTrimmedLeft (12), juce::Justification::centredLeft);

            y += 22;
            if (y > getHeight() - kPreviewH) break;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 8) return;

        for (int i = 0; i < rowBounds.size(); ++i)
        {
            if (! rowBounds[i].contains (e.getMouseDownPosition()))
                continue;

            if (tab == Tab::plugins && i < rowDescs.size())
            {
                // Internal JUCE drag so Timeline/Mixer can receive it with position data
                auto* ddc = juce::DragAndDropContainer::findParentDragContainerFor (this);
                if (ddc != nullptr)
                {
                    juce::String payload = "PLUGIN:" + rowDescs[i].createIdentifierString();
                    ddc->startDragging (payload, this, juce::ScaledImage{}, true);
                }
                return;
            }

            if (tab == Tab::files && i < rowFiles.size())
            {
                auto& f = rowFiles.getReference (i);
                if (f.existsAsFile() && ! f.isDirectory())
                {
                    // Start internal drag so Timeline can show ghost preview
                    auto* ddc = juce::DragAndDropContainer::findParentDragContainerFor (this);
                    if (ddc != nullptr)
                        ddc->startDragging ("AUDIOFILE:" + f.getFullPathName(), this);

                    // Also start external drag for dropping into other apps/OS
                    juce::DragAndDropContainer::performExternalDragDropOfFiles (
                        { f.getFullPathName() }, false, this);
                }
                return;
            }
            break;
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < rowBounds.size(); ++i)
        {
            if (rowBounds[i].contains (e.getPosition()))
            {
                if (tab == Tab::files && i < rowFiles.size())
                {
                    auto& f = rowFiles.getReference (i);
                    if (f.existsAsFile() && ! f.isDirectory())
                        if (onFileDoubleClicked) onFileDoubleClicked (f);
                }
                return;
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (pluginsTabBounds.contains (e.getPosition())) { tab = Tab::plugins; repaint(); return; }
        if (filesTabBounds.contains   (e.getPosition())) { tab = Tab::files;   repaint(); return; }
        if (cloudTabBounds.contains   (e.getPosition())) { tab = Tab::cloud;   repaint(); return; }

        if (tab == Tab::cloud)
        {
            return;
        }

        if (! rescanBtn.isEmpty() && rescanBtn.contains (e.getPosition()))
        {
            if (onRescanRequested) onRescanRequested();
            repaint();
            return;
        }

        if (tab == Tab::plugins && e.mods.isPopupMenu())
        {
            for (int i = 0; i < rowBounds.size(); ++i)
            {
                if (! rowBounds[i].contains (e.getPosition()))
                    continue;

                if (i >= rowDescs.size())
                    return;

                juce::PluginDescription desc = rowDescs.getReference (i);

                juce::PopupMenu m;
                const bool scanning = audioEngine.isScanningPlugins();
                m.addItem (1, "Delete from Plugin Browser", ! scanning);

                auto screenPos = juce::Desktop::getMousePosition();
                m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                                 [this, desc] (int r)
                                 {
                                     if (r != 1)
                                         return;

                                     auto msg = "This removes the plugin from the browser list only.\n"
                                                "It will appear again after a manual RESCAN if it still exists on disk.";

                                     juce::Component::SafePointer<Browser> safe (this);
                                     juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon,
                                                                        "Delete plugin",
                                                                        msg + juce::String ("\n\n") + desc.name,
                                                                        "Delete", "Cancel",
                                                                        this,
                                                                        juce::ModalCallbackFunction::create ([safe, desc] (int result)
                                                                        {
                                                                            if (safe == nullptr || result == 0)
                                                                                return;

                                                                            safe->audioEngine.deletePluginFromBrowserList (desc);
                                                                            safe->repaint();
                                                                        }));
                                 });
                return;
            }
        }

        for (int i = 0; i < rowBounds.size(); ++i)
            if (rowBounds[i].contains (e.getPosition()))
            {
                if (tab == Tab::plugins)
                {
                    // Do not insert on single-click — use drag-and-drop to a track or mixer strip.
                    repaint();
                }
                else {
                    auto& f = rowFiles.getReference (i);
                    if (f.isDirectory()) {
                        currentDir = f;
                        selectedFile = juce::File();
                        refreshFileCache();
                        repaint();
                    } else {
                        if (f != selectedFile) {
                            selectedFile = f;
                            thumb.setSource (new juce::FileInputSource (f));
                        }
                        if (onFilePicked) onFilePicked (f);
                        repaint();
                    }
                }
                return;
            }
    }

    void paintCloudTab (juce::Graphics& g)
    {
        int y = 60;
        int W = getWidth();

        g.setColour (Theme::textMuted);
        g.setFont (Theme::uiSize (14.0f).withStyle (juce::Font::bold));
        g.drawText ("CLOUD SYNC", 0, y, W, 22, juce::Justification::centred);
        y += 30;
        
        g.setFont (Theme::uiSize (12.0f));
        g.drawText ("Coming Soon", 0, y, W, 20, juce::Justification::centred);
        
        y += 40;
        g.setFont (Theme::uiSize (10.0f));
        g.drawText ("Google Drive integration", 0, y, W, 18, juce::Justification::centred);
        y += 14;
        g.drawText ("is currently inactive for", 0, y, W, 18, juce::Justification::centred);
        y += 14;
        g.drawText ("further tuning.", 0, y, W, 18, juce::Justification::centred);
    }

private:
    static constexpr int kPreviewH = 80;

    void refreshFileCache()
    {
        cachedFileChildren.clearQuick();

        juce::Array<juce::File> children;
        currentDir.findChildFiles (children, juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);

        std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b) {
            if (a.isDirectory() != b.isDirectory()) return a.isDirectory();
            return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
        });

        for (auto& f : children)
            if (f.isDirectory() || f.hasFileExtension ("wav;mp3;aif;aiff;flac;ogg"))
                cachedFileChildren.add (f);
    }

    AudioEngineManager& audioEngine;
    Tab                  tab { Tab::plugins };
    juce::File           currentDir;
    juce::Array<juce::File> cachedFileChildren;
    juce::File           selectedFile;

    juce::AudioFormatManager  formatManager;
    juce::AudioThumbnailCache thumbCache { 10 };
    juce::AudioThumbnail      thumb;

    GoogleDriveClient*                        driveClient         = nullptr;
    juce::Array<GoogleDriveClient::DriveFile> driveFiles;
    bool                                      driveLoading        = false;
    juce::Rectangle<int>                      loginBtnBounds;
    juce::Rectangle<int>                      driveRefreshBtnBounds;
    juce::Array<juce::Rectangle<int>>         driveRowBounds;

    juce::Rectangle<int> rescanBtn, pluginsTabBounds, filesTabBounds, cloudTabBounds;
    juce::Array<juce::Rectangle<int>>    rowBounds;
    juce::Array<juce::PluginDescription> rowDescs;
    juce::Array<juce::File>              rowFiles;
};
