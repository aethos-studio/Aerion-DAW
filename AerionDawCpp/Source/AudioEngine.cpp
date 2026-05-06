#include "AudioEngine.h"
#include "ProjectData.h"

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
    juce::PropertiesFile::Options options;
    options.applicationName     = "Aerion DAW";
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName          = "AerionDAW";
    appProperties.setStorageParameters (options);

    std::unique_ptr<juce::XmlElement> savedAudioState (appProperties.getUserSettings()->getXmlValue ("audioDeviceState"));
    if (savedAudioState != nullptr)
        engine.getDeviceManager().deviceManager.initialise (2, 2, savedAudioState.get(), true);
    else
        engine.getDeviceManager().initialise (2, 2);
    
    engine.getDeviceManager().enableOutputClipping (true);
    engine.getDeviceManager().deviceManager.addChangeListener (this);
    
    setupInitialEdit();
    startTimerHz (30);
}

AudioEngineManager::~AudioEngineManager()
{
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);
    if (auto state = engine.getDeviceManager().deviceManager.createStateXml())
    {
        appProperties.getUserSettings()->setValue ("audioDeviceState", state.get());
        appProperties.getUserSettings()->saveIfNeeded();
    }

    engine.getDeviceManager().closeDevices();
    edit = nullptr;
}

//==============================================================================
static te::EqualiserPlugin* getOrCreateUtilityEQ (te::Track* track, te::Edit& edit)
{
    if (track == nullptr) return nullptr;
    
    // Look for existing EQ
    for (auto* p : track->pluginList)
        if (auto* eq = dynamic_cast<te::EqualiserPlugin*> (p))
            return eq;
            
    // Add one at the front of the list
    if (auto* audioTrack = dynamic_cast<te::AudioTrack*> (track))
    {
        auto p = edit.getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, {});
        if (p != nullptr)
        {
            track->pluginList.insertPlugin (p, 0, nullptr);
            return dynamic_cast<te::EqualiserPlugin*> (p.get());
        }
    }
    
    return nullptr;
}

float AudioEngineManager::getTrackHPF (te::Track* track)
{
    if (auto* eq = getOrCreateUtilityEQ (track, *edit))
        return eq->loFreqValue.get();
    return 20.0f;
}

void AudioEngineManager::setTrackHPF (te::Track* track, float freq)
{
    if (auto* eq = getOrCreateUtilityEQ (track, *edit))
        eq->setLowFreq (freq);
}

float AudioEngineManager::getTrackLPF (te::Track* track)
{
    if (auto* eq = getOrCreateUtilityEQ (track, *edit))
        return eq->hiFreqValue.get();
    return 20000.0f;
}

void AudioEngineManager::setTrackLPF (te::Track* track, float freq)
{
    if (auto* eq = getOrCreateUtilityEQ (track, *edit))
        eq->setHighFreq (freq);
}

bool AudioEngineManager::getTrackPhase (te::Track* track)
{
    if (auto* eq = getOrCreateUtilityEQ (track, *edit))
        return eq->phaseInvert.get();
    return false;
}

void AudioEngineManager::setTrackPhase (te::Track* track, bool phaseInverted)
{
    if (auto* eq = getOrCreateUtilityEQ (track, *edit))
        eq->phaseInvert = phaseInverted;
}

bool AudioEngineManager::getTrackMono (te::Track* track)
{
    // Use a custom property on the track's state ValueTree for now.
    // In a real implementation, this would toggle a mono-summing plugin.
    return track->state.getProperty ("isMono", false);
}

void AudioEngineManager::setTrackMono (te::Track* track, bool mono)
{
    track->state.setProperty ("isMono", mono, nullptr);
    
    // Logic to insert/remove a mono plugin could go here.
}

void AudioEngineManager::addSendToNewBus (te::Track* track)
{
    if (track == nullptr) return;
    
    // 1. Create a new audio track for the bus
    auto busTrack = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (busTrack == nullptr) return;
    
    // Find an unused bus number
    juce::Array<int> usedBusNumbers;
    for (auto* t : te::getAllTracks (*edit))
        for (auto* p : t->pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
                usedBusNumbers.addIfNotAlreadyThere (send->getBusNumber());

    int busNum = 0;
    while (usedBusNumbers.contains (busNum))
        ++busNum;

    busTrack->setName (te::AuxSendPlugin::getDefaultBusName (busNum));

    // 2. Add AuxReturn to the new track
    auto retPlug = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
    if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (retPlug.get()))
    {
        ret->busNumber = busNum;
        busTrack->pluginList.insertPlugin (retPlug, 0, nullptr);
    }

    // 3. Add AuxSend to the source track
    auto sendPlug = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
    if (auto* send = dynamic_cast<te::AuxSendPlugin*> (sendPlug.get()))
    {
        send->busNumber = busNum;
        send->setGainDb (-6.0f);
        track->pluginList.insertPlugin (sendPlug, 0, nullptr);
    }
}

void AudioEngineManager::setAuxSendLevelDb (te::AuxSendPlugin* plugin, float db)
{
    if (plugin != nullptr)
        plugin->setGainDb (db);
}

float AudioEngineManager::getAuxSendLevelDb (te::AuxSendPlugin* plugin)
{
    if (plugin != nullptr)
        return plugin->getGainDb();
    return -100.0f;
}

void AudioEngineManager::saveMixSnapshot (const juce::String& name)
{
    auto snapshots = edit->state.getOrCreateChildWithName (IDs::MixSnapshots, nullptr);
    juce::ValueTree snapshot (IDs::Snapshot);
    snapshots.appendChild (snapshot, nullptr);
    snapshot.setProperty (IDs::name, name, nullptr);

    for (auto* t : te::getAudioTracks (*edit))
    {
        juce::ValueTree trackData (IDs::Data);
        snapshot.appendChild (trackData, nullptr);
        trackData.setProperty (IDs::id, t->itemID.toString(), nullptr);
        trackData.setProperty (IDs::level, getTrackVolumeDb (t), nullptr);
        trackData.setProperty (IDs::pan, getTrackPan (t), nullptr);
        trackData.setProperty (IDs::mute, t->isMuted (false), nullptr);
        trackData.setProperty (IDs::solo, t->isSolo (false), nullptr);
    }
}

void AudioEngineManager::recallMixSnapshot (const juce::String& name)
{
    auto snapshots = edit->state.getChildWithName (IDs::MixSnapshots);
    if (! snapshots.isValid()) return;
    
    auto snapshot = snapshots.getChildWithProperty (IDs::name, name);
    if (! snapshot.isValid()) return;
    
    for (int i = 0; i < snapshot.getNumChildren(); ++i)
    {
        auto data = snapshot.getChild (i);
        if (auto* t = te::findTrackForID (*edit, te::EditItemID::fromString (data.getProperty (IDs::id).toString())))
        {
            setTrackVolumeDb (t, (float) data.getProperty (IDs::level));
            setTrackPan (t, (float) data.getProperty (IDs::pan));
            t->setMute (data.getProperty (IDs::mute));
            t->setSolo (data.getProperty (IDs::solo));
        }
    }
}

juce::StringArray AudioEngineManager::getMixSnapshotNames()
{
    juce::StringArray names;
    auto snapshots = edit->state.getChildWithName (IDs::MixSnapshots);
    for (int i = 0; i < snapshots.getNumChildren(); ++i)
        names.add (snapshots.getChild (i).getProperty (IDs::name).toString());
    return names;
}

int AudioEngineManager::getPluginNumPrograms (te::Plugin* plugin)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
        if (auto* pi = ext->getAudioPluginInstance())
            return pi->getNumPrograms();
    return 0;
}

juce::String AudioEngineManager::getPluginProgramName (te::Plugin* plugin, int index)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
        if (auto* pi = ext->getAudioPluginInstance())
            return pi->getProgramName (index);
    return {};
}

void AudioEngineManager::setPluginProgram (te::Plugin* plugin, int index)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
        if (auto* pi = ext->getAudioPluginInstance())
        {
            pi->setCurrentProgram (index);
            plugin->edit.pluginChanged (*plugin);
        }
}

void AudioEngineManager::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto state = engine.getDeviceManager().deviceManager.createStateXml())
    {
        appProperties.getUserSettings()->setValue ("audioDeviceState", state.get());
        appProperties.getUserSettings()->saveIfNeeded();
    }
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

    // Use Tracktion's built-in click track; start disabled.
    edit->clickTrackEnabled = false;

    // Ensure master track is clean
    if (auto master = edit->getMasterTrack())
    {
        for (int i = master->pluginList.size(); --i >= 0;)
            master->pluginList.getPlugins()[i]->deleteFromParent();

        // Studio One-style: default master bus at 0.0 dB (Tracktion often starts lower).
        setTrackVolumeDb (master, 0.0f);
    }
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
        if (dynamic_cast<te::AudioTrack*>(t) != nullptr || dynamic_cast<te::FolderTrack*>(t) != nullptr)
            result.add (t);
    return result;
}

juce::Array<te::Track*> AudioEngineManager::getMixerTracks()
{
    juce::Array<te::Track*> result;
    auto top = getTopLevelTracks();
    
    std::function<void(te::Track*)> addRecursive = [&](te::Track* t) {
        result.add (t);
        if (auto* f = dynamic_cast<te::FolderTrack*> (t))
            for (auto* child : f->getAllAudioSubTracks (false))
                addRecursive (child);
    };
    
    for (auto* t : top)
        addRecursive (t);
        
    return result;
}

void AudioEngineManager::syncFolderRouting()
{
    // Ensure all tracks inside a folder are routed to that folder if it's a submix.
    for (auto* t : te::getAllTracks (*edit))
    {
        if (auto* f = dynamic_cast<te::FolderTrack*> (t))
        {
            if (f->isSubmixFolder())
            {
                for (auto* child : f->getAllAudioSubTracks (false))
                {
                    (void) child;
                    // Tracktion handles routing automatically for submix folders.
                }
            }
        }
    }
}

float AudioEngineManager::getTrackPeak (te::Track* track)
{
    if (track == nullptr) return -100.0f;

    // Find this track's LevelMeterPlugin, lazily creating one if absent.
    // The plugin's getLevelCache() is unused — Tracktion never writes to it.
    // Live levels arrive via measurer.processBuffer() into registered clients.
    te::LevelMeterPlugin* meterPlugin = nullptr;
    for (auto* p : track->pluginList)
        if (auto* m = dynamic_cast<te::LevelMeterPlugin*> (p))
        {
            meterPlugin = m;
            break;
        }

    if (meterPlugin == nullptr)
    {
        if (auto p = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}))
        {
            track->pluginList.insertPlugin (p, 0, nullptr);
            meterPlugin = dynamic_cast<te::LevelMeterPlugin*> (p.get());
        }
    }

    if (meterPlugin == nullptr) return -100.0f;

    auto key = track->itemID.toString();
    auto it = trackMeters.find (key);
    if (it == trackMeters.end())
        it = trackMeters.emplace (key, std::make_unique<TrackMeter>()).first;

    auto& tm = *it->second;
    if (tm.plugin.get() != meterPlugin)
    {
        // Move the Client onto the new measurer. We deliberately keep the
        // previous plugin alive via the Plugin::Ptr above so that removing
        // the client from the old measurer here is always safe.
        if (tm.plugin != nullptr)
            tm.plugin->measurer.removeClient (tm.client);

        tm.plugin = meterPlugin;
        tm.client.reset();
        meterPlugin->measurer.addClient (tm.client);
    }

    auto l = tm.client.getAndClearAudioLevel (0).dB;
    auto r = tm.client.getAndClearAudioLevel (1).dB;
    float latest = juce::jmax (l, r);

    // Track absolute maximum for peak hold readout
    tm.maxPeakDb = juce::jmax (tm.maxPeakDb, latest);

    // Decay the displayed peak at 48 dB/s with a 50 ms hold — same shape
    // FourOscPlugin uses for its built-in meter, so the visual feels right.
    auto now = juce::Time::getApproximateMillisecondCounter();
    int  elapsedMs = (int) (now - tm.lastUpdateMs);
    float decayed  = tm.lastPeakDb - 48.0f * (juce::jmax (0, elapsedMs - 50) / 1000.0f);

    if (latest > decayed)
    {
        tm.lastPeakDb   = latest;
        tm.lastUpdateMs = now;
    }
    else
    {
        tm.lastPeakDb = decayed;
    }

    return juce::jlimit (-100.0f, 0.0f, tm.lastPeakDb);
}

te::AudioTrack* AudioEngineManager::addAudioTrack()
{
    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (auto* at = t.get())
    {
        // Ensure every new track has a level meter plugin for UI feedback.
        if (at->getLevelMeterPlugin() == nullptr)
        {
            auto p = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {});
            at->pluginList.insertPlugin (p, 0, nullptr);
        }
    }
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

void AudioEngineManager::moveTrackAfter (te::Track* t, te::Track* preceding, te::FolderTrack* folder)
{
    if (t == nullptr) return;
    edit->moveTrack (t, te::TrackInsertPoint (folder, preceding));
}

void AudioEngineManager::setTrackArmed (te::Track* t, bool enabled)
{
    if (t == nullptr) return;
    armedTracks.set (t->itemID.toString(), enabled);

    if (enabled)
    {
        // Enable all physical wave inputs so they appear as InputDeviceInstances.
        auto& dm = engine.getDeviceManager();
        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto* wip = dm.getWaveInDevice (i))
                wip->setEnabled (true);

        // Ensure the playback context exists — this creates InputDeviceInstances.
        edit->getTransport().ensureContextAllocated();

        // Target each wave input to this track and enable recording.
        // If the track has a preferred device index, route only that device; otherwise route all.
        int preferredIdx = inputDeviceMap.contains (t->itemID.toString())
                               ? inputDeviceMap[t->itemID.toString()] : -1;

        int devIdx = 0;
        for (auto* in : edit->getAllInputDevices())
        {
            if (in->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
            {
                if (preferredIdx < 0 || devIdx == preferredIdx)
                {
                    [[maybe_unused]] auto res = in->setTarget (t->itemID, true, &edit->getUndoManager(), 0);
                    in->setRecordingEnabled (t->itemID, true);
                }
                ++devIdx;
            }
        }

        // Enable input monitoring through the FX chain while armed.
        for (auto* in : edit->getAllInputDevices())
            if (in->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                in->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::automatic);
    }
    else
    {
        for (auto* in : edit->getAllInputDevices())
        {
            in->setRecordingEnabled (t->itemID, false);
            if (in->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                in->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::off);
        }
    }
}

bool AudioEngineManager::isTrackArmed (te::Track* t) const
{
    if (t == nullptr) return false;
    return armedTracks[t->itemID.toString()];
}

float AudioEngineManager::getTrackMaxPeak (te::Track* track)
{
    if (track == nullptr) return -100.0f;
    auto it = trackMeters.find (track->itemID.toString());
    if (it != trackMeters.end())
        return it->second->maxPeakDb;
    return -100.0f;
}

void AudioEngineManager::clearTrackMaxPeak (te::Track* track)
{
    if (track == nullptr) return;
    auto it = trackMeters.find (track->itemID.toString());
    if (it != trackMeters.end())
        it->second->maxPeakDb = -100.0f;
}

te::Plugin* AudioEngineManager::getPluginFor (juce::ValueTree& v)
{
    if (edit)
    {
        for (auto* t : te::getAllTracks (*edit))
        {
            for (auto* p : t->pluginList.getPlugins())
                if (p->state == v)
                    return p;
        }
        // Also check master track plugins.
        if (auto master = edit->getMasterTrack())
        {
            for (auto* p : master->pluginList.getPlugins())
                if (p->state == v)
                    return p;
        }
    }
    return nullptr;
}

void AudioEngineManager::toggleTrackMute (te::Track* t)
{
    if (t == nullptr) return;
    bool newState = ! t->isMuted (false);
    t->setMute (newState);
    
    if (auto* f = dynamic_cast<te::FolderTrack*> (t))
        for (auto* child : f->getAllAudioSubTracks (true))
            child->setMute (newState);
}

void AudioEngineManager::removePlugin (te::Plugin* plugin)
{
    if (plugin != nullptr) plugin->deleteFromParent();
}

void AudioEngineManager::setTrackPan (te::Track* track, float pan)
{
    if (auto* p = getAutomationParam (track, AutomationParamKind::Pan))
        p->setParameter (juce::jlimit (-1.0f, 1.0f, pan), juce::sendNotification);
}

float AudioEngineManager::getTrackPan (te::Track* track)
{
    if (auto* p = getAutomationParam (track, AutomationParamKind::Pan))
        return p->getCurrentValue();
    return 0.0f;
}

te::AutomatableParameter* AudioEngineManager::getAutomationParam (te::Track* track, AutomationParamKind kind)
{
    if (track == nullptr) return nullptr;

    te::VolumeAndPanPlugin::Ptr vp;
    if (auto* a = dynamic_cast<te::AudioTrack*> (track))
        vp = a->getVolumePlugin();
    else if (track->isMasterTrack())
        vp = edit->getMasterVolumePlugin();

    if (vp != nullptr)
        return (kind == AutomationParamKind::Volume) ? vp->volParam.get() : vp->panParam.get();

    return nullptr;
}

void AudioEngineManager::setTrackVolumeDb (te::Track* track, float db)
{
    if (track == nullptr) return;
    db = juce::jlimit (kMinVolumeDb, kMaxVolumeDb, db);

    if (auto* vp = getAutomationParam (track, AutomationParamKind::Volume))
    {
        ensureVolumeRange (track);
        // Tracktion's native fader-position formula: pos = exp((dB - 6) / 20)
        float nativeVal = std::exp ((db - 6.0f) / 20.0f);
        vp->setParameter (nativeVal, juce::sendNotification);
    }
}

float AudioEngineManager::getTrackVolumeDb (te::Track* track)
{
    if (track == nullptr) return 0.0f;

    if (auto* vp = getAutomationParam (track, AutomationParamKind::Volume))
    {
        // Tracktion's native inverse: dB = 20 * ln(pos) + 6
        float nativeVal = vp->getCurrentValue();
        return (nativeVal > 0.0f) ? (20.0f * std::log (nativeVal)) + 6.0f : -100.0f;
    }

    return 0.0f;
}

void AudioEngineManager::ensureVolumeRange (te::Track* track)
{
    if (track == nullptr) return;

    if (auto* vp = getAutomationParam (track, AutomationParamKind::Volume))
    {
        // Tracktion defaults to 0..1 (= 0..+6 dB). Extend upper bound to kMaxVolumeDb.
        const float nativeMax = std::exp ((kMaxVolumeDb - 6.0f) / 20.0f);
        if (vp->valueRange.end < nativeMax)
        {
            float currentVal = vp->getCurrentValue();
            const_cast<juce::NormalisableRange<float>&> (vp->valueRange).end = nativeMax;
            vp->setParameter (currentVal, juce::sendNotification);
        }
    }
}

void AudioEngineManager::toggleTrackSolo (te::Track* t)
{
    if (t == nullptr) return;
    bool newState = ! t->isSolo (false);
    t->setSolo (newState);
    
    if (auto* f = dynamic_cast<te::FolderTrack*> (t))
        for (auto* child : f->getAllAudioSubTracks (true))
            child->setSolo (newState);
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

void AudioEngineManager::setTempo (double bpm)
{
    if (edit == nullptr) return;
    auto& ts = edit->tempoSequence;
    ts.getTempoAt (te::TimePosition()).setBpm (bpm);
    broadcastChange();
}

void AudioEngineManager::setTimeSig (int numerator, int denominator)
{
    if (edit == nullptr) return;
    auto& ts = edit->tempoSequence;
    ts.getTimeSigAt (te::TimePosition()).setStringTimeSig (juce::String::formatted ("%d/%d", numerator, denominator));
    broadcastChange();
}

bool AudioEngineManager::isMetronomeEnabled() const
{
    if (edit == nullptr) return false;
    return edit->clickTrackEnabled.get();
}

void AudioEngineManager::setMetronomeEnabled (bool enabled)
{
    if (edit == nullptr) return;
    edit->clickTrackEnabled = enabled;
}

void AudioEngineManager::toggleMetronome()
{
    setMetronomeEnabled (! isMetronomeEnabled());
}

float AudioEngineManager::getMetronomeVolumeDb() const
{
    if (edit == nullptr) return 0.0f;
    // Read clickTrackGain directly — getClickTrackVolume() clamps to 1.0 (0 dB).
    float gain = edit->clickTrackGain.get();
    return (gain > 0.0f) ? 20.0f * std::log10 (gain) : -60.0f;
}

void AudioEngineManager::setMetronomeVolumeDb (float db)
{
    if (edit == nullptr) return;
    // Write clickTrackGain directly — setClickTrackVolume() clamps to 1.0 (0 dB).
    static constexpr float kMaxGain = 31.623f; // +30 dB
    float gain = juce::jlimit (0.0f, kMaxGain, std::pow (10.0f, db / 20.0f));
    edit->clickTrackGain = gain;
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
        track->setName (file.getFileNameWithoutExtension());
        te::AudioFile af (engine, file);
        auto len = af.getLength();
        
        track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                               { { te::TimePosition::fromSeconds (0.0), te::TimeDuration::fromSeconds (len) }, te::TimeDuration::fromSeconds (0.0) },
                               false);
    }
}

tracktion::WaveAudioClip* AudioEngineManager::insertAudioClipOnTrack (
    tracktion::AudioTrack* track, const juce::File& file, double startTimeSecs)
{
    if (track == nullptr || ! file.existsAsFile()) return nullptr;

    te::AudioFile af (engine, file);
    double len = af.getLength();
    if (len <= 0.0) len = 1.0;

    auto clip = track->insertWaveClip (
        file.getFileNameWithoutExtension(), file,
        { { te::TimePosition::fromSeconds (startTimeSecs),
            te::TimeDuration::fromSeconds (len) },
          te::TimeDuration::fromSeconds (0.0) },
        false);

    return dynamic_cast<tracktion::WaveAudioClip*> (clip.get());
}

tracktion::AudioTrack* AudioEngineManager::importAudioFileAtPosition (
    const juce::File& file, double startTimeSecs)
{
    if (! file.existsAsFile()) return nullptr;

    auto* track = addAudioTrack();
    if (track != nullptr)
        track->setName (file.getFileNameWithoutExtension());

    insertAudioClipOnTrack (track, file, startTimeSecs);
    return track;
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

//==============================================================================
// Milestone 3 — Recording & Monitoring
//==============================================================================

void AudioEngineManager::timerCallback()
{
    if (!punchEnabled || !isRecording()) return;

    auto& t = edit->getTransport();
    if (!t.looping) return;

    double pos     = getTransportPosition();
    double loopEnd = t.getLoopRange().getEnd().inSeconds();
    if (pos >= loopEnd)
        stop();
}

void AudioEngineManager::setMetronomeAccentEnabled (bool on)
{
    edit->clickTrackEmphasiseBars = on;
}

bool AudioEngineManager::isMetronomeAccentEnabled() const
{
    return edit->clickTrackEmphasiseBars;
}

void AudioEngineManager::setCountInMode (int bars)
{
    using CI = te::Edit::CountIn;
    edit->setCountInMode (bars == 1 ? CI::oneBar : bars == 2 ? CI::twoBar : CI::none);
}

int AudioEngineManager::getCountInBars() const
{
    using CI = te::Edit::CountIn;
    switch (edit->getCountInMode())
    {
        case CI::oneBar: return 1;
        case CI::twoBar: return 2;
        default:         return 0;
    }
}

void AudioEngineManager::setPunchEnabled (bool on)
{
    punchEnabled = on;
}

void AudioEngineManager::setLatencyCompensationEnabled (bool on)
{
    edit->setLatencyCompensationEnabled (on);
}

bool AudioEngineManager::isLatencyCompensationEnabled() const
{
    return edit->isLatencyCompensationEnabled();
}

juce::StringArray AudioEngineManager::getInputDeviceNames() const
{
    juce::StringArray names;
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* d = dm.getWaveInDevice (i))
            names.add (d->getName());
    return names;
}

void AudioEngineManager::setTrackInputDevice (te::Track* track, int waveDeviceIdx)
{
    if (track == nullptr) return;
    inputDeviceMap.set (track->itemID.toString(), waveDeviceIdx);

    // If currently armed, re-arm with the new device selection.
    if (isTrackArmed (track))
        setTrackArmed (track, true);
}

int AudioEngineManager::getTrackInputDeviceIdx (te::Track* track) const
{
    if (track == nullptr) return -1;
    auto key = track->itemID.toString();
    return inputDeviceMap.contains (key) ? inputDeviceMap[key] : -1;
}

AudioEngineManager::BufferInfo AudioEngineManager::getBufferInfo() const
{
    auto& dm = engine.getDeviceManager();
    return { dm.getSampleRate(), dm.getBlockSize(), dm.getCpuUsage() };
}
