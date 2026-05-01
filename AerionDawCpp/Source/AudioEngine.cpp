#include "AudioEngine.h"

namespace te = tracktion;

namespace
{
    // Hosts a tracktion plugin's AudioProcessorEditor inside a JUCE DocumentWindow.
    // Without this, te::UIBehaviour::createPluginWindow returns nothing and
    // showWindowExplicitly() is a silent no-op.
    class AerionPluginEditorContent : public te::Plugin::EditorComponent
    {
    public:
        AerionPluginEditorContent (te::ExternalPlugin& p) : plugin (p)
        {
            if (auto* pi = plugin.getAudioPluginInstance())
            {
                editor.reset (pi->createEditorIfNeeded());
                if (editor == nullptr)
                    editor = std::make_unique<juce::GenericAudioProcessorEditor> (*pi);
                addAndMakeVisible (*editor);
            }
            resizeToFitEditor (true);
        }

        bool allowWindowResizing() override { return false; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override
        {
            if (editor == nullptr || allowWindowResizing()) return {};
            return editor->getConstrainer();
        }

        void resized() override { if (editor) editor->setBounds (getLocalBounds()); }

        void childBoundsChanged (juce::Component* c) override
        {
            if (c == editor.get()) { plugin.edit.pluginChanged (plugin); resizeToFitEditor(); }
        }

        void resizeToFitEditor (bool force = false)
        {
            if (force || ! allowWindowResizing())
                setSize (juce::jmax (8, editor ? editor->getWidth() : 0),
                         juce::jmax (8, editor ? editor->getHeight() : 0));
        }

        te::ExternalPlugin& plugin;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
    };

    class AerionPluginWindow : public juce::DocumentWindow
    {
    public:
        AerionPluginWindow (te::Plugin& p)
            : DocumentWindow (p.getName(), juce::Colours::black, DocumentWindow::closeButton, true),
              plugin (p)
        {
            setUsingNativeTitleBar (true);
            getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
            setResizeLimits (100, 50, 4000, 4000);
            recreateEditor();
            centreWithSize (juce::jmax (200, getWidth()), juce::jmax (100, getHeight()));
        }

        ~AerionPluginWindow() override { setEditor (nullptr); }

        void recreateEditor()
        {
            setEditor (nullptr);
            if (auto e = plugin.createEditor())
                setEditor (std::move (e));
        }

        void setEditor (std::unique_ptr<te::Plugin::EditorComponent> newEd)
        {
            setConstrainer (nullptr);
            editor.reset();
            if (newEd != nullptr)
            {
                editor = std::move (newEd);
                setContentNonOwned (editor.get(), true);
            }
            setResizable (editor && editor->allowWindowResizing(), false);
            if (editor && editor->allowWindowResizing())
                setConstrainer (editor->getBoundsConstrainer());
        }

        te::Plugin::EditorComponent* getEditor() const { return editor.get(); }

        void closeButtonPressed() override { plugin.windowState->closeWindowExplicitly(); }

    private:
        te::Plugin& plugin;
        std::unique_ptr<te::Plugin::EditorComponent> editor;
    };

    class AerionUIBehaviour : public te::UIBehaviour
    {
    public:
        std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& pws) override
        {
            if (auto* ws = dynamic_cast<te::Plugin::WindowState*> (&pws))
            {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&ws->plugin))
                    if (ext->getAudioPluginInstance() == nullptr) return {};

                auto w = std::make_unique<AerionPluginWindow> (ws->plugin);
                if (w->getEditor() == nullptr) return {};
                w->setVisible (true);
                w->toFront (false);
                return w;
            }
            return {};
        }
    };
}

std::unique_ptr<te::UIBehaviour> AudioEngineManager::makeUIBehaviour()
{
    return std::make_unique<AerionUIBehaviour>();
}

AudioEngineManager::AudioEngineManager()
{
    engine.getDeviceManager().initialise (0, 2);
    setupInitialEdit();
}

AudioEngineManager::~AudioEngineManager()
{
    engine.getDeviceManager().closeDevices();
    edit = nullptr;
}

void AudioEngineManager::setupInitialEdit()
{
    edit = te::Edit::createSingleTrackEdit (engine);

    if (editListener == nullptr)
        editListener = std::make_unique<EditListener> (*this);
    
    edit->addListener (editListener.get());

    // Set a default recording directory
    auto recDir = engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Recordings");
    recDir.createDirectory();
    engine.getPropertyStorage().setDefaultLoadSaveDirectory ("recordings", recDir);

    edit->getTransport().setLoopRange (te::TimeRange (te::TimePosition::fromSeconds (0.0),
                                                       te::TimeDuration::fromSeconds (4.0)));
    edit->getTransport().looping = false;

    // Start with an empty session — remove the track that createSingleTrackEdit added.
    for (auto* t : te::getAudioTracks (*edit))
        edit->deleteTrack (t);
}

juce::Array<te::AudioTrack*> AudioEngineManager::getAudioTracks()
{
    return te::getAudioTracks (*edit);
}

juce::Array<te::Track*> AudioEngineManager::getTopLevelTracks()
{
    // Only return user-visible tracks. Tracktion's meta-tracks
    // (Tempo / Chord / Marker / Arranger / Master) are filtered out.
    juce::Array<te::Track*> result;
    for (auto* t : te::getTopLevelTracks (*edit))
        if (dynamic_cast<te::AudioTrack*>(t) != nullptr
         || dynamic_cast<te::FolderTrack*>(t) != nullptr)
            result.add (t);
    return result;
}

te::AudioTrack* AudioEngineManager::addAudioTrack()
{
    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    return t.get();
}

te::FolderTrack* AudioEngineManager::addFolderTrack()
{
    auto f = edit->insertNewFolderTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, false);
    return f.get();
}

te::FolderTrack* AudioEngineManager::groupTracks (const juce::Array<te::Track*>& tracks)
{
    if (tracks.isEmpty())
        return nullptr;

    auto folder = edit->insertNewFolderTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, false);
    if (folder == nullptr)
        return nullptr;

    for (auto* t : tracks)
        if (t != nullptr && t != folder.get())
            edit->moveTrack (t, te::TrackInsertPoint (folder.get(), nullptr));

    return folder.get();
}

void AudioEngineManager::deleteTrack (te::Track* t)
{
    if (t != nullptr)
        edit->deleteTrack (t);
}

void AudioEngineManager::moveTrackToIndex (te::Track* t, int newIndex)
{
    if (t == nullptr)
        return;

    auto top = te::getTopLevelTracks (*edit);
    juce::Array<te::Track*> without;
    without.ensureStorageAllocated (top.size());
    for (auto* x : top) if (x != t) without.add (x);

    newIndex = juce::jlimit (0, without.size(), newIndex);
    te::Track* preceding = (newIndex <= 0) ? nullptr : without[newIndex - 1];

    edit->moveTrack (t, te::TrackInsertPoint (nullptr, preceding));
}

void AudioEngineManager::setTrackArmed (te::Track* t, bool enabled)
{
    if (t == nullptr) return;
    armedTracks.set (t->itemID.toString(), enabled);

    for (auto* in : edit->getAllInputDevices())
        in->setRecordingEnabled (t->itemID, enabled);
}

bool AudioEngineManager::isTrackArmed (te::Track* t) const
{
    if (t == nullptr) return false;
    return armedTracks[t->itemID.toString()];
}

void AudioEngineManager::toggleTrackMute (te::Track* t)
{
    if (t != nullptr) t->setMute (! t->isMuted (false));
}

void AudioEngineManager::removePlugin (te::Plugin* plugin)
{
    if (plugin != nullptr) plugin->deleteFromParent();
}

void AudioEngineManager::setTrackPan (te::Track* track, float pan)
{
    if (track == nullptr) return;
    if (auto* a = dynamic_cast<te::AudioTrack*> (track))
        if (auto* vp = a->getVolumePlugin())
            vp->setPan (juce::jlimit (-1.0f, 1.0f, pan));
}

float AudioEngineManager::getTrackPan (te::Track* track)
{
    if (auto* a = dynamic_cast<te::AudioTrack*> (track))
        if (auto* vp = a->getVolumePlugin())
            return vp->getPan();
    return 0.0f;
}

te::AutomatableParameter* AudioEngineManager::getAutomationParam (te::Track* track, AutomationParamKind kind)
{
    if (track == nullptr) return nullptr;
    if (auto* a = dynamic_cast<te::AudioTrack*> (track))
        if (auto* vp = a->getVolumePlugin())
            return (kind == AutomationParamKind::Volume) ? vp->volParam.get() : vp->panParam.get();
    return nullptr;
}

void AudioEngineManager::toggleTrackSolo (te::Track* t)
{
    if (t != nullptr) t->setSolo (! t->isSolo (false));
}

double AudioEngineManager::getTempoAtPosition (double seconds)
{
    if (edit == nullptr) return 120.0;
    return edit->tempoSequence.getBpmAt (te::TimePosition::fromSeconds (seconds));
}

juce::String AudioEngineManager::getTimeSigAtPosition (double seconds)
{
    if (edit == nullptr) return "4/4";
    auto& ts = edit->tempoSequence.getTimeSigAt (te::TimePosition::fromSeconds (seconds));
    return juce::String::formatted ("%d/%d", (int) ts.numerator, (int) ts.denominator);
}

juce::String AudioEngineManager::getBarsBeatsString (double seconds)
{
    if (edit == nullptr) return "0001.01.01.00";
    auto bb = edit->tempoSequence.toBarsAndBeats (te::TimePosition::fromSeconds (seconds));
    
    // Bars and beats are 0-indexed in the struct, but traditionally displayed 1-indexed.
    int bar  = bb.bars + 1;
    int beat = (int)bb.beats.inBeats() + 1;
    
    // Calculate sub-beats (16th notes) and ticks. 
    // Assuming 480 ticks per quarter note (Tracktion default).
    double fractionalBeats = bb.beats.inBeats() - (int)bb.beats.inBeats();
    int sub  = (int)(fractionalBeats * 4.0) + 1;
    int tick = (int)(std::fmod (fractionalBeats * 4.0, 1.0) * 120.0); // 120 ticks per 16th

    return juce::String::formatted ("%04d.%02d.%02d.%02d", bar, beat, sub, tick);
}

void AudioEngineManager::importAudioFile (const juce::File& file)
{
    if (! file.existsAsFile()) return;

    auto* track = addAudioTrack();
    if (track != nullptr)
    {
        te::AudioFile af (engine, file);
        auto len = af.getLength();
        
        track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                               { { te::TimePosition::fromSeconds (0.0), te::TimeDuration::fromSeconds (len) }, te::TimeDuration::fromSeconds (0.0) },
                               false);
    }
}

void AudioEngineManager::saveProject (const juce::File& file)
{
    if (edit != nullptr)
    {
        if (auto xml = edit->state.createXml())
            xml->writeTo (file);
    }
}

void AudioEngineManager::loadProject (const juce::File& file)
{
    if (file.existsAsFile())
    {
        if (auto xml = juce::XmlDocument::parse (file))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            edit = te::loadEditFromState (engine, vt, te::Edit::forEditing);
            edit->addListener (editListener.get());

            armedTracks.clear();
            thumbnails.clear();
            broadcastChange();
        }
    }
}

te::SmartThumbnail& AudioEngineManager::getThumbnailForClip (te::WaveAudioClip& clip, juce::Component& comp)
{
    auto it = thumbnails.find (clip.itemID.getRawID());
    if (it != thumbnails.end())
        return *it->second;

    auto thumb = std::make_unique<te::SmartThumbnail> (engine, clip.getAudioFile(), comp, edit.get());
    auto& ref = *thumb;
    thumbnails[clip.itemID.getRawID()] = std::move (thumb);
    return ref;
}

void AudioEngineManager::createNewProject()
{
    setupInitialEdit();
    armedTracks.clear();
    broadcastChange();
}

void AudioEngineManager::broadcastChange()
{
    listeners.call ([] (Listener& l) { l.editStateChanged(); });
}

void AudioEngineManager::scanPlugins()
{
    if (scanInFlight.exchange (true))
        return; // Already scanning.

    auto& list = engine.getPluginManager().knownPluginList;
    auto cacheFile = engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Plugins.xml");

    if (cacheFile.existsAsFile())
        if (auto xml = juce::XmlDocument::parse (cacheFile))
            list.recreateFromXml (*xml);

    auto& fm = engine.getPluginManager().pluginFormatManager;
    if (fm.getNumFormats() == 0)
        fm.addDefaultFormats();

    if (fm.getNumFormats() == 0) { scanInFlight.store (false); return; }

    // Run every format sequentially on one background thread so writes to
    // KnownPluginList and the cache file don't race.
    struct ScanThread : public juce::Thread
    {
        ScanThread (AudioEngineManager& m, juce::File f) : Thread ("PluginScanner"), owner (m), cache (f) {}

        void run() override
        {
            auto& fm   = owner.engine.getPluginManager().pluginFormatManager;
            auto& list = owner.engine.getPluginManager().knownPluginList;

            for (int i = 0; i < fm.getNumFormats(); ++i)
            {
                auto* format = fm.getFormat (i);
                if (format == nullptr) continue;

                juce::FileSearchPath path = format->getDefaultLocationsToSearch();

                // Extra paths for spots installers commonly use that JUCE doesn't return.
               #if JUCE_WINDOWS
                if (format->getName() == "VST3")
                {
                    auto userVst3 = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                          .getChildFile ("Programs/Common/VST3");
                    if (userVst3.isDirectory()) path.add (userVst3);

                    path.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
                }
                if (format->getName() == "VST")
                {
                    path.add (juce::File ("C:\\Program Files\\VSTPlugins"));
                    path.add (juce::File ("C:\\Program Files\\Steinberg\\VstPlugins"));
                }
               #endif

                juce::PluginDirectoryScanner scanner (list, *format, path, /*recursive*/ true, cache);
                juce::String pluginName;
                while (! threadShouldExit() && scanner.scanNextFile (true, pluginName)) {}
            }

            if (auto xml = list.createXml())
                xml->writeTo (cache);

            owner.scanInFlight.store (false);
            juce::MessageManager::callAsync ([&owner = this->owner] { owner.broadcastChange(); });
        }

        AudioEngineManager& owner;
        juce::File cache;
    };

    if (scanThread != nullptr)
    {
        scanThread->stopThread (2000);
        scanThread.reset();
    }
    scanThread = std::make_unique<ScanThread> (*this, cacheFile);
    scanThread->startThread();
}

tracktion::Plugin::Ptr AudioEngineManager::addPluginToTrack (te::Track* track, const juce::PluginDescription& desc)
{
    if (track == nullptr) return {};

    // pluginList lives on the base Track, so this works for audio, folder and master tracks.
    auto p = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);
    if (p != nullptr)
    {
        track->pluginList.insertPlugin (p, track->pluginList.size(), nullptr);
        p->setEnabled (true);
        p->setProcessingEnabled (true);
        return p;
    }
    return {};
}
