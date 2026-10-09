#include "AudioEngine.h"
#include "ScannerProcesses.h"
#include "ProjectData.h"
#include "Export/MixdownExportJob.h"
#include "Audio/OutputTap.h"

namespace te = tracktion;

namespace
{
    /** Picks the buffer size in `sizes` closest to `targetSamples`, preferring
        an equal or smaller value when there's a tie. Used by the safe-default
        path; never forces a buffer the device can't actually support. */
    static int chooseBufferNear (const juce::Array<int>& sizes, int targetSamples)
    {
        if (sizes.isEmpty()) return targetSamples;
        if (sizes.contains (targetSamples)) return targetSamples;

        int best = sizes[0];
        for (int s : sizes)
        {
            const auto distBest = std::abs (best - targetSamples);
            const auto distS    = std::abs (s    - targetSamples);
            if (distS < distBest || (distS == distBest && s <= targetSamples && best > targetSamples))
                best = s;
        }
        return best;
    }

    /** Apply *safe* newcomer-friendly defaults: pick a robust, shared backend
        that won't conflict with other apps holding the device, and only nudge
        the buffer size if the current value is unusable. We deliberately do
        NOT touch the sample rate (WASAPI Exclusive locks the rate when set,
        so leaving it alone keeps the full list selectable in the dialog) and
        do NOT auto-pick Exclusive Mode (it collides with browser/system audio
        and causes distortion when another app already owns the device). */
    static void applyBeginnerFriendlyDefaults (te::Engine& engine)
    {
        auto& adm = engine.getDeviceManager().deviceManager;

       #if JUCE_WINDOWS
        // Order: ASIO (best, opt-in via Steinberg SDK) → Windows Audio shared
        // (always works, ~10 ms floor — still much better than DirectSound /
        // Low Latency Mode and never collides with other apps).
        const juce::StringArray prefs { "ASIO", "Windows Audio" };
       #elif JUCE_MAC
        const juce::StringArray prefs { "CoreAudio" };
       #elif JUCE_LINUX
        const juce::StringArray prefs { "JACK", "ALSA" };
       #else
        const juce::StringArray prefs;
       #endif

        juce::String pick;
        for (const auto& pref : prefs)
            for (auto* type : adm.getAvailableDeviceTypes())
                if (type != nullptr && type->getTypeName() == pref)
                {
                    type->scanForDevices();
                    if (! type->getDeviceNames (false).isEmpty()
                        || ! type->getDeviceNames (true).isEmpty())
                    { pick = pref; goto found; }
                }
        found:;

        if (pick.isNotEmpty() && pick != adm.getCurrentAudioDeviceType())
            adm.setCurrentAudioDeviceType (pick, true);

        // Only nudge the buffer when the open device clearly can't deliver a
        // usable interactive value. Don't touch sample rate — let the user / OS
        // pick from the full list the device exposes.
        if (auto* dev = adm.getCurrentAudioDevice())
        {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            adm.getAudioDeviceSetup (setup);

            const auto sizes = dev->getAvailableBufferSizes();
            const int  cur   = setup.bufferSize;
            if (! sizes.isEmpty() && (cur <= 0 || cur > 1024))
            {
                setup.bufferSize = chooseBufferNear (sizes, 256);
                adm.setAudioDeviceSetup (setup, false);
            }
        }
    }

    /** If the OS / saved settings opened a multi‑thousand‑sample buffer (common with
        WASAPI defaults), interactive MIDI monitoring feels like hundreds of ms of lag.
        Nudge toward a sane size when clearly excessive. */
    static void clampExcessiveAudioBufferSize (te::Engine& engine)
    {
        auto& adm = engine.getDeviceManager().deviceManager;
        auto* dev = adm.getCurrentAudioDevice();
        if (dev == nullptr) return;

        const int current = dev->getCurrentBufferSizeSamples();
        constexpr int kMaxInteractive = 1024;
        constexpr int kClampIfLargerThan = 2048; // above this, monitoring / MIDI feels broken

        if (current <= kClampIfLargerThan) return;

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        adm.getAudioDeviceSetup (setup);

        const auto sizes = dev->getAvailableBufferSizes();
        int chosen = 0;

        if (sizes.isEmpty())
        {
            chosen = juce::jmin (current, kMaxInteractive);
        }
        else
        {
            for (int s : sizes)
                if (s <= kMaxInteractive && (chosen == 0 || s > chosen))
                    chosen = s;

            if (chosen == 0)
                for (int s : sizes)
                    if (s < current && (chosen == 0 || s < chosen))
                        chosen = s;
        }

        if (chosen > 0 && chosen < current)
        {
            setup.bufferSize = chosen;
            adm.setAudioDeviceSetup (setup, true);
        }
    }

    // Runs every call into a hosted plugin through the fault monitor, so a
    // plugin crash is caught instead of taking the app down: processBlock
    // through Patches/tracktion/0001-external-plugin-process-hook.patch,
    // loading and saving or restoring its settings through
    // Patches/tracktion/0002-external-plugin-call-hook.patch.
    class AerionEngineBehaviour : public te::EngineBehaviour
    {
    public:
        PluginFaultMonitor pluginFaults;

        void processExternalPluginBlock (te::ExternalPlugin& plugin, juce::AudioPluginInstance& instance,
                                         juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                                         bool bypassed) override
        {
            pluginFaults.process (&plugin, plugin.isEnabled(), instance, buffer, midi, bypassed);
        }

        bool callExternalPlugin (te::ExternalPlugin& plugin, ExternalPluginCall kind,
                                 const std::function<void()>& call) override
        {
            using Stage = PluginFaultMonitor::Stage;
            const auto stage = kind == ExternalPluginCall::createInstance ? Stage::loading
                             : kind == ExternalPluginCall::saveState      ? Stage::savingState
                                                                          : Stage::restoringState;
            return pluginFaults.call (&plugin, stage, call);
        }

        // A plugin that crashes while being scanned takes down a child process
        // (started in Main.cpp), not the app, and is listed as failed.
        bool canScanPluginsOutOfProcess() override { return true; }

        // Tracktion asks for high priority while an Edit plays (2, realtime,
        // only when its useRealtime setting is on), so other programs cannot
        // starve the audio, and for normal priority again when playback stops.
        void setProcessPriority (int level) override
        {
            juce::Process::setPriority (level >= 2 ? juce::Process::RealtimePriority
                                      : level == 1 ? juce::Process::HighPriority
                                                   : juce::Process::NormalPriority);
        }
    };

    PluginFaultMonitor& faultMonitorFor (te::Engine& e)
    {
        return static_cast<AerionEngineBehaviour&> (e.getEngineBehaviour()).pluginFaults;
    }

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
                juce::AudioProcessorEditor* created = nullptr;

                if (faultMonitorFor (plugin.engine).call (&plugin, PluginFaultMonitor::Stage::openingEditor,
                                                          [&] { created = pi->createEditorIfNeeded(); }))
                {
                    editor.reset (created);

                    if (editor == nullptr)
                        editor = std::make_unique<juce::GenericAudioProcessorEditor> (*pi);

                    addAndMakeVisible (*editor);
                }
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
        explicit AerionUIBehaviour (AudioEngineManager& m) : manager (m) {}

        // Aerion has one Edit open at a time. Tracktion needs it to route MIDI
        // controller input to learnt mappings and to drive control surfaces.
        te::Edit* getCurrentlyFocusedEdit() override            { return manager.getEditIfAny(); }
        te::Edit* getLastFocusedEdit() override                 { return manager.getEditIfAny(); }

        juce::Array<te::Edit*> getAllOpenEdits() override
        {
            if (auto* e = manager.getEditIfAny())
                return { e };

            return {};
        }

        te::SelectionManager* getCurrentlyFocusedSelectionManager() override
        {
            return manager.getEditIfAny() != nullptr ? &manager.getSelectionManager() : nullptr;
        }

        std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& pws) override
        {
            if (auto* ws = dynamic_cast<te::Plugin::WindowState*> (&pws))
            {
                auto* ext = dynamic_cast<te::ExternalPlugin*> (&ws->plugin);

                if (ext != nullptr && ext->getAudioPluginInstance() == nullptr)
                    return {};

                auto w = std::make_unique<AerionPluginWindow> (ws->plugin);
                if (w->getEditor() == nullptr) return {};

                // Its editor crashed while opening; the fault dialog says so.
                if (ext != nullptr && faultMonitorFor (ext->engine).isBroken (ext)) return {};
                w->setVisible (true);
                w->toFront (false);
                return w;
            }
            return {};
        }

    private:
        AudioEngineManager& manager;
    };

}

std::unique_ptr<te::UIBehaviour> AudioEngineManager::makeUIBehaviour (AudioEngineManager& manager)
{
    return std::make_unique<AerionUIBehaviour> (manager);
}

void AudioEngineManager::setSelectedTracks (const juce::Array<te::Track*>& tracks)
{
    te::SelectableList list;

    for (auto* t : tracks)
        if (t != nullptr)
            list.add (t);

    if (list.isEmpty())
        selectionManager.deselectAll();
    else
        selectionManager.select (list);
}

std::unique_ptr<te::EngineBehaviour> AudioEngineManager::makeEngineBehaviour()
{
    return std::make_unique<AerionEngineBehaviour>();
}

bool AudioEngineManager::handlePluginFault (const void* key, PluginFaultMonitor::Stage stage, const juce::String& reason)
{
    if (edit == nullptr)
        return false;

    for (auto* plugin : te::getAllPlugins (*edit, false))
    {
        auto* external = dynamic_cast<te::ExternalPlugin*> (plugin);

        if (external == nullptr || static_cast<const void*> (external) != key)
            continue;

        const auto name = external->getName();
        juce::Logger::writeToLog ("Plugin fault: " + name + " crashed while "
                                  + PluginFaultMonitor::describeStage (stage)
                                  + " (" + reason + "); bypassing it.");

        external->setEnabled (false);
        edit->pluginChanged (*external);
        broadcastChange();
        listeners.call ([&] (Listener& l) { l.pluginFaulted (name, stage, reason); });
        return true;
    }

    // Not in the current Edit (e.g. a render). It stays skipped.
    return false;
}

bool AudioEngineManager::hasPluginFaulted (te::Plugin* plugin) const
{
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin);
    return external != nullptr && pluginFaults->hasFaulted (external);
}

juce::PropertiesFile::Options AudioEngineManager::userSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "Aerion DAW";
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName          = "AerionDAW";
    return options;
}

AudioEngineManager::AudioEngineManager()
{
    const auto ctorStartMs = juce::Time::getMillisecondCounterHiRes();

    pluginFaults = &faultMonitorFor (engine);
    pluginFaults->onFault = [this] (const void* key, PluginFaultMonitor::Stage stage, const juce::String& reason)
    {
        return handlePluginFault (key, stage, reason);
    };

    appProperties.setStorageParameters (userSettingsOptions());

    // Crash detection: check if previous session terminated cleanly
    auto* s = appProperties.getUserSettings();
    bool wasRunning = s ? s->getBoolValue("sessionRunning", false) : false;
    auto recoveryFile = getRecoveryFile();
    hadCrash = wasRunning && recoveryFile.existsAsFile();
    // Mark this session as running
    if (s)
    {
        s->setValue("sessionRunning", true);
        s->saveIfNeeded();
    }

    recentProjects.setMaxNumberOfItems (10);
    recentProjects.restoreFromString (
        appProperties.getUserSettings()->getValue ("recentProjects"));

    keymap.loadFrom (appProperties.getUserSettings());
    midiLearnWatcher = std::make_unique<MidiLearnWatcher> (*this);

    // Session/edit first (cheap vs driver init). Opening devices is deferred to the next
    // message so the main window can show and pump events while the OS loads ASIO/WASAPI.
    setupInitialEdit();
    applyDefaultMidiMappings (nullptr);
    markEditSaved();   // the user's default mappings are not an edit
    juce::Logger::writeToLog ("Startup: AudioEngineManager setupInitialEdit completed in "
                              + juce::String (juce::Time::getMillisecondCounterHiRes() - ctorStartMs, 1)
                              + " ms");
    startTimerHz (30);

    juce::MessageManager::callAsync ([this]
    {
        if (closing.load()) return;

        const auto deviceStartMs = juce::Time::getMillisecondCounterHiRes();

        // Opening devices can make Tracktion update the Edit's input device state.
        // That is not a user edit, so it must not mark a clean project as changed.
        const bool editWasClean = ! hasUnsavedEdits();

        std::unique_ptr<juce::XmlElement> savedAudioState (appProperties.getUserSettings()->getXmlValue ("audioDeviceState"));
        const bool firstRun = (savedAudioState == nullptr);

        if (savedAudioState != nullptr)
            engine.getDeviceManager().deviceManager.initialise (2, 2, savedAudioState.get(), true);
        else
            engine.getDeviceManager().initialise (2, 2);

        if (closing.load())
        {
            engine.getDeviceManager().closeDevices();
            return;
        }

        if (firstRun)
            applyBeginnerFriendlyDefaults (engine);
        else
            clampExcessiveAudioBufferSize (engine);

        engine.getDeviceManager().enableOutputClipping (true);
        engine.getDeviceManager().setGlobalOutputAudioProcessor (std::make_unique<Aerion::OutputTap> (loudnessAnalyser, spectrumAnalyser, correlationMeter));
        engine.getDeviceManager().deviceManager.addChangeListener (this);
        audioDevicesConnected = true;

        if (editWasClean)
            markEditSaved();
        juce::Logger::writeToLog ("Startup: audio device init completed in "
                                  + juce::String (juce::Time::getMillisecondCounterHiRes() - deviceStartMs, 1)
                                  + " ms");
        broadcastStatusChange();
    });
}

AudioEngineManager::~AudioEngineManager()
{
    closing.store (true);
    cancelActiveFreezeJobs();

    // Clear crash detection sentinel: this session is terminating cleanly
    if (auto* s = appProperties.getUserSettings())
    {
        s->setValue("sessionRunning", false);
        s->saveIfNeeded();
    }

    // Any in-flight callAsync from the scan thread that wins the WeakReference
    // race must find null callbacks instead of a half-destructed engine.
    onScanProgress = nullptr;
    onScanFinished = nullptr;

    stopTimer();

    if (audioDevicesConnected)
    {
        // The tap refers to the analysers, members destroyed before the engine.
        engine.getDeviceManager().setGlobalOutputAudioProcessor (nullptr);
        engine.getDeviceManager().removeChangeListener (this);
        if (auto state = engine.getDeviceManager().deviceManager.createStateXml())
        {
            appProperties.getUserSettings()->setValue ("audioDeviceState", state.get());
            appProperties.getUserSettings()->saveIfNeeded();
        }
    }

    if (auto* s = appProperties.getUserSettings())
    {
        s->setValue ("recentProjects", recentProjects.toString());
        s->saveIfNeeded();
    }

    // Everything that must be saved is saved. A scan that cannot be stopped in
    // time (a plugin that is slow to load, scanned inside Aerion itself) must not
    // keep Aerion open: the engine cannot be freed while it still runs, and
    // waiting for it would hold up whatever follows, such as an installer.
    if (! stopScanThread (kScanStopWaitMs))
    {
        juce::Logger::writeToLog ("Quit: a plugin scan is still running; ending Aerion without waiting for it");
        if (onForcedQuit)
            onForcedQuit();
        std::_Exit (0);
    }

    engine.getDeviceManager().closeDevices();
    releaseEditResources();
    pluginFaults->onFault = nullptr;
    edit = nullptr;
}

void AudioEngineManager::releaseEditResources()
{
    // Meters hold a reference to each track's LevelMeterPlugin. If that is the
    // last reference, the plugin's destructor calls back into its Edit, so
    // these must go while the Edit is still alive, never after it is replaced.
    trackMeters.clear();
    thumbnails.clear();
    selectionManager.deselectAll();

    // Faults are keyed by plugin address, which a new Edit may reuse.
    pluginFaults->clear();
}

//==============================================================================
static te::EqualiserPlugin* getOrCreateUtilityEQ (te::Track* track, te::Edit& edit)
{
    if (track == nullptr) return nullptr;
    
    for (auto* p : track->pluginList)
        if (auto* eq = dynamic_cast<te::EqualiserPlugin*> (p))
            return eq;

    // Front of the list, after an instrument (it would otherwise filter silence).
    if (auto* audioTrack = dynamic_cast<te::AudioTrack*> (track))
    {
        auto p = edit.getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, {});
        if (p != nullptr)
        {
            int index = 0;
            while (index < track->pluginList.size() && track->pluginList[index]->isSynth())
                ++index;

            track->pluginList.insertPlugin (p, index, nullptr);
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
    if (track == nullptr) return false;
    // Use a custom property on the track's state ValueTree for now.
    // In a real implementation, this would toggle a mono-summing plugin.
    return track->state.getProperty ("isMono", false);
}

void AudioEngineManager::setTrackMono (te::Track* track, bool mono)
{
    if (track == nullptr) return;
    track->state.setProperty ("isMono", mono, nullptr);
    broadcastChange();
    
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
    if (edit != nullptr && editListener != nullptr)
        edit->removeListener (editListener.get());

    edit = te::Edit::createSingleTrackEdit (engine);

    attachEditListenerToCurrentEdit();

    // Set a default recording directory
    auto recDir = engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Recordings");
    recDir.createDirectory();
    engine.getPropertyStorage().setDefaultLoadSaveDirectory ("recordings", recDir);

    edit->getTransport().setLoopRange (te::TimeRange (te::TimePosition::fromSeconds (0.0),
                                                       te::TimeDuration::fromSeconds (4.0)));
    edit->getTransport().looping = false;

    // Start with an empty session  -  remove the track that createSingleTrackEdit added.
    for (auto* t : te::getAudioTracks (*edit))
        edit->deleteTrack (t);

    // Use Tracktion's built-in click track; start disabled.
    edit->clickTrackEnabled = false;

    // Ensure master track is clean
    if (auto master = edit->getMasterTrack())
    {
        for (int i = master->pluginList.size(); --i >= 0;)
            master->pluginList.getPlugins()[i]->deleteFromParent();

        // Default master bus at 0.0 dB (Tracktion often starts lower).
        setTrackVolumeDb (master, 0.0f);
    }
}

void AudioEngineManager::attachEditListenerToCurrentEdit()
{
    if (edit == nullptr)
        return;

    if (editListener == nullptr)
        editListener = std::make_unique<EditListener> (*this);

    edit->addListener (editListener.get());
}

te::Track* AudioEngineManager::findTrackById (const juce::String& id) const
{
    if (edit == nullptr || id.isEmpty())
        return nullptr;

    for (auto* track : te::getAllTracks (*edit))
        if (track != nullptr && track->itemID.toString() == id)
            return track;

    return nullptr;
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

bool AudioEngineManager::isFolderSubmix (te::FolderTrack* folder) const
{
    return folder != nullptr && folder->isSubmixFolder();
}

void AudioEngineManager::setFolderSubmix (te::FolderTrack* folder, bool asSubmix)
{
    if (folder == nullptr || edit == nullptr)
        return;

    if (asSubmix)
    {
        if (! folder->isSubmixFolder())
        {
            // Default organisational folder uses VCA-only plugins; submix needs Volume+Meter.
            for (int i = folder->pluginList.size(); --i >= 0;)
            {
                if (auto* p = folder->pluginList[i])
                    if (dynamic_cast<te::VCAPlugin*> (p) != nullptr)
                        p->deleteFromParent();
            }

            if (folder->getVolumePlugin() == nullptr)
                if (auto p = edit->getPluginCache().createNewPlugin (te::VolumeAndPanPlugin::xmlTypeName, {}))
                    folder->pluginList.insertPlugin (p, 0, nullptr);

            if (folder->pluginList.findFirstPluginOfType<te::LevelMeterPlugin>() == nullptr)
                if (auto p = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}))
                    folder->pluginList.insertPlugin (p, -1, nullptr);
        }
    }
    else
    {
        if (folder->isSubmixFolder())
        {
            juce::Array<te::Plugin*> toRemove;
            for (auto* p : folder->pluginList)
            {
                if (dynamic_cast<te::VCAPlugin*> (p) != nullptr) continue;
                if (dynamic_cast<te::TextPlugin*> (p) != nullptr) continue;
                toRemove.add (p);
            }

            for (auto* p : toRemove)
                p->deleteFromParent();

            if (folder->pluginList.findFirstPluginOfType<te::VCAPlugin>() == nullptr)
                if (auto p = edit->getPluginCache().createNewPlugin (te::VCAPlugin::xmlTypeName, {}))
                    folder->pluginList.insertPlugin (p, -1, nullptr);
        }
    }

    syncFolderRouting();
    broadcastChange();
}

float AudioEngineManager::getTrackPeak (te::Track* track)
{
    if (track == nullptr) return -100.0f;

    // Find this track's LevelMeterPlugin, lazily creating one if absent.
    // The plugin's getLevelCache() is unused  -  Tracktion never writes to it.
    // Live levels arrive via measurer.processBuffer() into registered clients.
    te::LevelMeterPlugin* meterPlugin = nullptr;
    for (auto* p : track->pluginList)
        if (auto* m = dynamic_cast<te::LevelMeterPlugin*> (p))
        {
            meterPlugin = m;
            break;
        }

    if (meterPlugin == nullptr) return -100.0f;

    const auto key = track->itemID;
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

    // Decay the displayed peak at 48 dB/s with a 50 ms hold  -  same shape
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
        applyDefaultMidiMappings (at);
    }
    broadcastChange();
    return t.get();
}

te::AudioTrack* AudioEngineManager::addMidiTrack()
{
    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (auto* at = t.get())
    {
        // Mark as MIDI track for recording purposes
        at->state.setProperty(IDs::isMidiTrack, true, nullptr);

        // No instrument: the user adds one (built-in or third-party). The
        // meter is there so the track's level shows once they do.
        if (at->getLevelMeterPlugin() == nullptr)
            at->pluginList.insertPlugin (edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}),
                                         at->pluginList.size(), nullptr);
        applyDefaultMidiMappings (at);
    }
    broadcastChange();
    return t.get();
}

te::FolderTrack* AudioEngineManager::addFolderTrack()
{
    auto f = edit->insertNewFolderTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr, false);
    if (f != nullptr)
        // Tracktion may insert Volume/Meter by default, which makes isSubmixFolder() true immediately.
        // Force organisational (VCA-style) folder until the user chooses Convert to Submix.
        setFolderSubmix (f.get(), false);
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

    setFolderSubmix (folder.get(), false);
    return folder.get();
}

void AudioEngineManager::deleteTrack (te::Track* t)
{
    if (t == nullptr)
        return;

    const auto itemID = t->itemID;
    const auto id = itemID.toString();
    edit->deleteTrack (t);
    armedTracks.remove (id);
    inputDeviceMap.remove (id);
    midiInputDeviceMap.remove (id);
    monitorModeMap.remove (id);
    freezingTracks.remove (id);
    trackMeters.erase (itemID);
    broadcastChange();
}

void AudioEngineManager::moveTrackAfter (te::Track* t, te::Track* preceding, te::FolderTrack* folder)
{
    if (t == nullptr) return;
    edit->moveTrack (t, te::TrackInsertPoint (folder, preceding));
    broadcastChange();
}

namespace
{
    static te::InputDevice::MonitorMode toTeMonitorMode (AudioEngineManager::MonitorMode m)
    {
        switch (m)
        {
            case AudioEngineManager::MonitorMode::On:  return te::InputDevice::MonitorMode::on;
            case AudioEngineManager::MonitorMode::Off: return te::InputDevice::MonitorMode::off;
            case AudioEngineManager::MonitorMode::Auto:
            default:                                   return te::InputDevice::MonitorMode::automatic;
        }
    }

    static bool isMidiInputType (te::InputDevice::DeviceType type)
    {
        return type == te::InputDevice::physicalMidiDevice
            || type == te::InputDevice::virtualMidiDevice;
    }
}

void AudioEngineManager::setTrackArmed (te::Track* t, bool enabled)
{
    if (t == nullptr) return;
    armedTracks.set (t->itemID.toString(), enabled);

    if (enabled)
    {
        // Check if this is a MIDI-only track
        bool isMidiTrack = (bool) t->state.getProperty(IDs::isMidiTrack, false);

        // For MIDI tracks, enable MIDI input devices but not wave devices
        if (isMidiTrack)
        {
            // Ensure the playback context exists  -  this creates InputDeviceInstances.
            edit->getTransport().ensureContextAllocated();

            const int preferredMidiIdx = getTrackMidiInputDeviceIdx (t);

            int midiIdx = 0;
            for (auto* in : edit->getAllInputDevices())
            {
                const auto devType = in->getInputDevice().getDeviceType();
                if (isMidiInputType (devType))
                {
                    if (preferredMidiIdx < 0 || midiIdx == preferredMidiIdx)
                    {
                        [[maybe_unused]] auto res = in->setTarget (t->itemID, true, &edit->getUndoManager(), 0);
                        in->setRecordingEnabled (t->itemID, true);
                    }
                    ++midiIdx;
                }
            }
        }
        else
        {
            // For audio tracks, enable wave input devices
            // Enable all physical wave inputs so they appear as InputDeviceInstances.
            auto& dm = engine.getDeviceManager();
            for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
                if (auto* wip = dm.getWaveInDevice (i))
                    wip->setEnabled (true);

            // Ensure the playback context exists  -  this creates InputDeviceInstances.
            edit->getTransport().ensureContextAllocated();

            // Target each wave input to this track and enable recording.
            // If the track has a preferred device index, route only that device; otherwise route all.
            const int preferredWaveIdx = getTrackInputDeviceIdx (t);
            const int preferredMidiIdx = getTrackMidiInputDeviceIdx (t);

            int waveIdx = 0, midiIdx = 0;
            for (auto* in : edit->getAllInputDevices())
            {
                const auto devType = in->getInputDevice().getDeviceType();

                if (devType == te::InputDevice::waveDevice)
                {
                    if (preferredWaveIdx < 0 || waveIdx == preferredWaveIdx)
                    {
                        [[maybe_unused]] auto res = in->setTarget (t->itemID, true, &edit->getUndoManager(), 0);
                        in->setRecordingEnabled (t->itemID, true);
                    }
                    ++waveIdx;
                }
                else if (isMidiInputType (devType))
                {
                    if (preferredMidiIdx < 0 || midiIdx == preferredMidiIdx)
                    {
                        [[maybe_unused]] auto res = in->setTarget (t->itemID, true, &edit->getUndoManager(), 0);
                        in->setRecordingEnabled (t->itemID, true);
                    }
                    ++midiIdx;
                }
            }
        }

        // Apply the per-track monitoring override (default = Auto). Note that
        // monitor mode lives on the InputDevice itself, so a "last-armed-track
        // wins" rule applies when several armed tracks share an input - which
        // matches every other Tracktion-based DAW.
        const auto teMode = toTeMonitorMode (getTrackMonitorMode (t));
        const int preferredMidiIdx2 = getTrackMidiInputDeviceIdx (t);
        const int preferredWaveIdx2 = getTrackInputDeviceIdx (t);
        int waveIdx2 = 0, midiIdx2 = 0;
        for (auto* in : edit->getAllInputDevices())
        {
            const auto devType = in->getInputDevice().getDeviceType();

            if (devType == te::InputDevice::waveDevice)
            {
                if (!isMidiTrack && (preferredWaveIdx2 < 0 || waveIdx2 == preferredWaveIdx2))
                    in->getInputDevice().setMonitorMode (teMode);
                ++waveIdx2;
            }
            else if (isMidiInputType (devType))
            {
                if (preferredMidiIdx2 < 0 || midiIdx2 == preferredMidiIdx2)
                    in->getInputDevice().setMonitorMode (teMode);
                ++midiIdx2;
            }
        }
    }
    else
    {
        for (auto* in : edit->getAllInputDevices())
        {
            in->setRecordingEnabled (t->itemID, false);
            const auto devType = in->getInputDevice().getDeviceType();
            if (devType == te::InputDevice::waveDevice || isMidiInputType (devType))
                in->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::off);
        }
    }

    broadcastChange();
}

bool AudioEngineManager::isTrackArmed (te::Track* t) const
{
    if (t == nullptr) return false;
    return armedTracks[t->itemID.toString()];
}

float AudioEngineManager::getTrackMaxPeak (te::Track* track)
{
    if (track == nullptr) return -100.0f;
    auto it = trackMeters.find (track->itemID);
    if (it != trackMeters.end())
        return it->second->maxPeakDb;
    return -100.0f;
}

void AudioEngineManager::clearTrackMaxPeak (te::Track* track)
{
    if (track == nullptr) return;
    auto it = trackMeters.find (track->itemID);
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

    broadcastChange();
}

void AudioEngineManager::removePlugin (te::Plugin* plugin)
{
    if (plugin != nullptr)
    {
        if (auto* ownerTrack = plugin->getOwnerTrack())
            if (auto* at = dynamic_cast<te::AudioTrack*> (ownerTrack))
                if (isTrackFrozen (at) || isTrackFreezing (at))
                    return;
    }

    if (plugin != nullptr)
    {
        plugin->deleteFromParent();
        broadcastChange();
    }
}

bool AudioEngineManager::isExternalPluginBypassed (te::Plugin* plugin) const
{
    return plugin != nullptr && ! plugin->isEnabled();
}

juce::Array<te::AudioTrack*> AudioEngineManager::getSidechainSourceCandidates (te::Plugin& plugin)
{
    juce::Array<te::AudioTrack*> out;
    for (auto* at : te::getAudioTracks (plugin.edit))
        if (at != plugin.getOwnerTrack())
            out.add (at);
    return out;
}

void AudioEngineManager::setPluginSidechainSource (te::Plugin& plugin, te::AudioTrack* source)
{
    if (source == nullptr)
    {
        plugin.setSidechainSourceID ({});
        return;
    }

    plugin.setSidechainSourceID (source->itemID);

    // Without wires the plugin gets its default routing and ignores the source:
    // main input to channels 1-2, sidechain to the plugin's extra inputs.
    if (plugin.getNumWires() == 0)
        plugin.guessSidechainRouting();
}

void AudioEngineManager::setPluginBypassed (te::Plugin* plugin, bool bypassed)
{
    if (plugin == nullptr)
        return;

    if (auto* ownerTrack = plugin->getOwnerTrack())
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (ownerTrack))
        {
            if (isTrackFrozen (at) || isTrackFreezing (at))
                return;
        }
    }

    plugin->setEnabled (! bypassed);

    if (! bypassed)
        plugin->setProcessingEnabled (true);

    edit->pluginChanged (*plugin);
    broadcastChange();
}

void AudioEngineManager::moveInsertDevice (te::Track* track, te::Plugin* plugin, int newExternalIndex)
{
    if (track == nullptr || plugin == nullptr)
        return;

    if (auto* at = dynamic_cast<te::AudioTrack*> (track))
    {
        if (isTrackFrozen (at) || isTrackFreezing (at))
            return;
    }

    auto externals = getInsertDevices (track);

    const int currentExternalIndex = externals.indexOf (plugin);
    if (currentExternalIndex < 0)
        return;

    newExternalIndex = juce::jlimit (0, externals.size() - 1, newExternalIndex);
    if (newExternalIndex == currentExternalIndex)
        return;

    externals.remove (currentExternalIndex);
    externals.insert (newExternalIndex, plugin);

    int targetListIndex = track->pluginList.size();
    const int followingExternalIndex = newExternalIndex + 1;

    if (followingExternalIndex < externals.size())
        targetListIndex = track->pluginList.indexOf (externals[followingExternalIndex]);

    te::Plugin::Ptr pluginPtr (plugin);
    track->pluginList.insertPlugin (pluginPtr, targetListIndex, nullptr);
    broadcastChange();
}

// A parameter with an automation curve follows that curve on every audio block,
// stopped or playing, so a fader or pan move would snap straight back. The move
// overrides the curve instead (it is bypassed, and stays in the project) until
// the user re-enables automation. With a single point Tracktion moves that point.
static void overrideAutomationForManualMove (te::AutomatableParameter& p)
{
    auto& curve = p.getCurve();

    if (curve.getNumPoints() > 1 && ! curve.bypass.get())
        curve.bypass = true;
}

static bool isAutomationOverridden (te::AutomatableParameter* p)
{
    return p != nullptr && p->getCurve().getNumPoints() > 1 && p->getCurve().bypass.get();
}

void AudioEngineManager::setTrackPan (te::Track* track, float pan)
{
    if (auto* p = getAutomationParam (track, AutomationParamKind::Pan))
    {
        overrideAutomationForManualMove (*p);
        p->setParameter (juce::jlimit (-1.0f, 1.0f, pan), juce::sendNotification);
    }
}

bool AudioEngineManager::isTrackAutomationOverridden (te::Track* track)
{
    return isAutomationOverridden (getAutomationParam (track, AutomationParamKind::Volume))
        || isAutomationOverridden (getAutomationParam (track, AutomationParamKind::Pan));
}

void AudioEngineManager::reenableTrackAutomation (te::Track* track)
{
    if (edit == nullptr)
        return;

    const auto position = edit->getTransport().getPosition();

    for (auto kind : { AutomationParamKind::Volume, AutomationParamKind::Pan })
    {
        if (auto* p = getAutomationParam (track, kind); isAutomationOverridden (p))
        {
            p->getCurve().bypass = false;
            p->updateToFollowCurve (position);
        }
    }

    broadcastChange();
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
    else if (auto* f = dynamic_cast<te::FolderTrack*> (track))
        vp = f->getVolumePlugin();
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
        overrideAutomationForManualMove (*vp);
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

//==============================================================================
AudioEngineManager::MidiLearnWatcher::MidiLearnWatcher (AudioEngineManager& m)
    : te::MidiLearnState::Listener (m.engine.getMidiLearnState()), owner (m)
{
}

void AudioEngineManager::MidiLearnWatcher::midiLearnStatusChanged (bool)
{
    if (owner.onMidiLearnChanged)
        owner.onMidiLearnChanged();
}

void AudioEngineManager::MidiLearnWatcher::midiLearnAssignmentChanged (te::MidiLearnState::ChangeType type)
{
    // One controller per learn: stop once it is mapped. Deferred, because this
    // runs inside the mapping code's own notification.
    owner.storeMidiMappings();

    if (type == te::MidiLearnState::added)
        juce::MessageManager::callAsync ([safe = juce::WeakReference<AudioEngineManager> (&owner)]
        {
            if (safe != nullptr)
                safe->setMidiLearnActive (false);
        });

    owner.broadcastChange();
    if (owner.onMidiLearnChanged)
        owner.onMidiLearnChanged();
}

void AudioEngineManager::setMidiLearnActive (bool shouldBeActive)
{
    if (! shouldBeActive && edit != nullptr)
        edit->getParameterChangeHandler().getPendingParam (true);

    engine.getMidiLearnState().setActive (shouldBeActive);
}

bool AudioEngineManager::isMidiLearnActive()
{
    return engine.getMidiLearnState().isActive();
}

void AudioEngineManager::learnParameter (te::AutomatableParameter& param)
{
    setMidiLearnActive (true);
    // Marks the parameter as the one waiting for a controller, as a touch would.
    param.getEdit().getParameterChangeHandler().parameterChanged (param, false);
}

juce::String AudioEngineManager::getMidiMappingText (te::AutomatableParameter* param)
{
    int channel = -1, controllerID = -1;
    if (param == nullptr || edit == nullptr
         || ! edit->getParameterControlMappings().getParameterMapping (*param, channel, controllerID))
        return {};

    return midiControllerText (controllerID, channel);
}

void AudioEngineManager::storeMidiMappings()
{
    // The mappings object keeps its own list; the Edit (and so the saved
    // project) only has them once written back. Done on each change rather
    // than on save, because writing them changes the Edit.
    if (edit != nullptr)
        edit->getParameterControlMappings().saveToEdit();
}

juce::Array<AudioEngineManager::MidiMappingRow> AudioEngineManager::getMidiMappings()
{
    juce::Array<MidiMappingRow> rows;
    if (edit == nullptr)
        return rows;

    auto& mappings = edit->getParameterControlMappings();
    for (int i = 0; i < mappings.getNumControllerIDs(); ++i)
    {
        const auto m = mappings.getMappingForRow (i);
        rows.add ({ midiControllerText (m.controllerID, m.channelID),
                    m.parameter != nullptr ? m.parameter->getFullName() : juce::String ("(no parameter)") });
    }
    return rows;
}

void AudioEngineManager::removeMidiMapping (int row)
{
    if (edit == nullptr || ! juce::isPositiveAndBelow (row, edit->getParameterControlMappings().getNumControllerIDs()))
        return;

    edit->getParameterControlMappings().removeMapping (row);
    storeMidiMappings();
    broadcastChange();
    if (onMidiLearnChanged)
        onMidiLearnChanged();
}

juce::String AudioEngineManager::defaultMappingTargetFor (te::AutomatableParameter* param)
{
    using Kind = AutomationParamKind;
    if (param == nullptr || edit == nullptr)
        return {};

    if (auto* master = getMasterTrack())
    {
        if (param == getAutomationParam (master, Kind::Volume)) return "master:volume";
        if (param == getAutomationParam (master, Kind::Pan))    return "master:pan";
    }

    auto tracks = te::getAudioTracks (*edit);
    for (int i = 0; i < tracks.size(); ++i)
    {
        if (param == getAutomationParam (tracks[i], Kind::Volume)) return "track:" + juce::String (i + 1) + ":volume";
        if (param == getAutomationParam (tracks[i], Kind::Pan))    return "track:" + juce::String (i + 1) + ":pan";
    }

    return {};
}

te::AutomatableParameter* AudioEngineManager::parameterForDefaultTarget (const juce::String& target)
{
    using Kind = AutomationParamKind;
    if (edit == nullptr)
        return nullptr;

    const auto parts = juce::StringArray::fromTokens (target, ":", "");
    const auto kind = parts[parts.size() - 1] == "pan" ? Kind::Pan : Kind::Volume;

    if (parts.size() == 2 && parts[0] == "master")
        return getAutomationParam (getMasterTrack(), kind);

    if (parts.size() == 3 && parts[0] == "track")
    {
        auto tracks = te::getAudioTracks (*edit);
        const int index = parts[1].getIntValue() - 1;
        if (juce::isPositiveAndBelow (index, tracks.size()))
            return getAutomationParam (tracks[index], kind);
    }

    return nullptr;
}

AudioEngineManager::DefaultMappingResult AudioEngineManager::saveMidiMappingsAsDefault()
{
    DefaultMappingResult result;
    if (edit == nullptr)
        return result;

    juce::XmlElement defaults ("MidiDefaults");
    auto& mappings = edit->getParameterControlMappings();

    for (int i = 0; i < mappings.getNumControllerIDs(); ++i)
    {
        const auto m = mappings.getMappingForRow (i);
        const auto target = defaultMappingTargetFor (m.parameter);

        if (target.isEmpty() || m.controllerID <= 0)
        {
            ++result.skipped;
            continue;
        }

        auto* e = defaults.createNewChildElement ("MAP");
        e->setAttribute ("controller", m.controllerID);
        e->setAttribute ("channel", m.channelID);
        e->setAttribute ("target", target);
        ++result.saved;
    }

    if (auto* s = getUserSettings())
    {
        s->setValue (kDefaultMidiMappingsKey, &defaults);
        s->saveIfNeeded();
    }

    return result;
}

void AudioEngineManager::clearDefaultMidiMappings()
{
    if (auto* s = getUserSettings())
    {
        s->removeValue (kDefaultMidiMappingsKey);
        s->saveIfNeeded();
    }
}

int AudioEngineManager::getNumDefaultMidiMappings()
{
    if (auto* s = getUserSettings())
        if (auto xml = s->getXmlValue (kDefaultMidiMappingsKey))
            return xml->getNumChildElements();

    return 0;
}

void AudioEngineManager::applyDefaultMidiMappings (te::Track* track)
{
    auto* s = getUserSettings();
    if (edit == nullptr || s == nullptr)
        return;

    auto defaults = s->getXmlValue (kDefaultMidiMappingsKey);
    if (defaults == nullptr || defaults->getNumChildElements() == 0)
        return;

    // Which targets belong to this track: "master:" or "track:<n>:".
    juce::String prefix = "master:";
    if (track != nullptr && ! track->isMasterTrack())
    {
        const int index = te::getAudioTracks (*edit).indexOf (dynamic_cast<te::AudioTrack*> (track));
        if (index < 0)
            return;
        prefix = "track:" + juce::String (index + 1) + ":";
    }

    // Mappings are added through the Edit's state, the form Tracktion loads
    // them from; write back the current ones first so none are lost.
    auto& mappings = edit->getParameterControlMappings();
    mappings.saveToEdit();
    auto state = edit->state.getOrCreateChildWithName (te::IDs::CONTROLLERMAPPINGS, nullptr);
    bool added = false;

    for (auto* e : defaults->getChildIterator())
    {
        const auto target = e->getStringAttribute ("target");
        if (! target.startsWith (prefix))
            continue;

        auto* param = parameterForDefaultTarget (target);
        const int controller = e->getIntAttribute ("controller");
        const int channel    = e->getIntAttribute ("channel");
        if (param == nullptr || controller <= 0 || mappings.isParameterMapped (*param))
            continue;

        bool controllerInUse = false;
        for (const auto& map : state)
            controllerInUse = controllerInUse || ((int) map[te::IDs::id] == controller && (int) map[te::IDs::channel] == channel);
        if (controllerInUse)
            continue;

        state.appendChild (te::createValueTree (te::IDs::MAP,
                                                te::IDs::id, controller,
                                                te::IDs::channel, channel,
                                                te::IDs::param, param->getFullName(),
                                                te::IDs::pluginID, param->getOwnerID().toString()), nullptr);
        added = true;
    }

    if (added)
    {
        mappings.loadFromEdit();
        if (onMidiLearnChanged)
            onMidiLearnChanged();
    }
}

juce::String AudioEngineManager::midiControllerText (int controllerID, int channel)
{
    // Tracktion's controller IDs: 0x10000 + CC number, 0x20000 NRPN, 0x30000 RPN, 0x40000 pressure.
    juce::String what;
    if (controllerID >= 0x40000)      what = "Pressure";
    else if (controllerID >= 0x30000) what = "RPN " + juce::String (controllerID & 0x7fff);
    else if (controllerID >= 0x20000) what = "NRPN " + juce::String (controllerID & 0x7fff);
    else if (controllerID >= 0x10000) what = "CC " + juce::String (controllerID & 0x7f);
    else                              return {};

    return what + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 Ch ")) + juce::String (channel);
}

void AudioEngineManager::clearMidiMapping (te::AutomatableParameter* param)
{
    if (param == nullptr || edit == nullptr)
        return;

    if (edit->getParameterControlMappings().removeParameterMapping (*param))
    {
        storeMidiMappings();
        broadcastChange();
        if (onMidiLearnChanged)
            onMidiLearnChanged();
    }
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

    broadcastChange();
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
    ts.getTimeSigAt (te::TimePosition()).setStringTimeSig (juce::String::formatted ("%d/%d",
                                                                                    juce::jlimit (1, 32, numerator),
                                                                                    juce::jlimit (1, 32, denominator)));
    broadcastChange();
}

int AudioEngineManager::getNumTimeSigs() const
{
    return edit != nullptr ? edit->tempoSequence.getNumTimeSigs() : 0;
}

double AudioEngineManager::getTimeSigStartBeat (int index) const
{
    if (edit == nullptr || index < 0 || index >= edit->tempoSequence.getNumTimeSigs())
        return 0.0;

    if (auto* timeSig = edit->tempoSequence.getTimeSig (index))
        return timeSig->getStartBeat().inBeats();

    return 0.0;
}

juce::String AudioEngineManager::getTimeSigAtIndex (int index) const
{
    if (edit == nullptr || index < 0 || index >= edit->tempoSequence.getNumTimeSigs())
        return "4/4";

    if (auto* timeSig = edit->tempoSequence.getTimeSig (index))
        return timeSig->getStringTimeSig();

    return "4/4";
}

void AudioEngineManager::setTimeSigAtBeat (double beat, int numerator, int denominator)
{
    if (edit == nullptr)
        return;

    numerator = juce::jlimit (1, 32, numerator);
    denominator = juce::jlimit (1, 32, denominator);

    auto& ts = edit->tempoSequence;
    const auto targetBeat = tracktion::BeatPosition::fromBeats (juce::jmax (0.0, beat));
    auto& existing = ts.getTimeSigAt (targetBeat);

    tracktion::TimeSigSetting* target = &existing;
    if (std::abs (existing.getStartBeat().inBeats() - targetBeat.inBeats()) > 0.0001)
        target = ts.insertTimeSig (targetBeat).get();

    if (target != nullptr)
    {
        target->setStringTimeSig (juce::String::formatted ("%d/%d", numerator, denominator));
        broadcastChange();
    }
}

void AudioEngineManager::setTimeSigAtPosition (double seconds, int numerator, int denominator)
{
    if (edit == nullptr)
        return;

    auto& ts = edit->tempoSequence;
    auto barsAndBeats = ts.toBarsAndBeats (tracktion::TimePosition::fromSeconds (juce::jmax (0.0, seconds)));
    barsAndBeats.beats = tracktion::BeatDuration();
    const auto beat = ts.toBeats (ts.toTime (barsAndBeats));
    setTimeSigAtBeat (beat.inBeats(), numerator, denominator);
}

void AudioEngineManager::removeTimeSigAtIndex (int index)
{
    if (edit == nullptr || index <= 0 || index >= edit->tempoSequence.getNumTimeSigs())
        return;

    edit->tempoSequence.removeTimeSig (index);
    broadcastChange();
}

void AudioEngineManager::moveTimeSigAtIndexToBeat (int index, double beat)
{
    if (edit == nullptr || index <= 0 || index >= edit->tempoSequence.getNumTimeSigs())
        return;

    if (auto* timeSig = edit->tempoSequence.getTimeSig (index))
    {
        const auto delta = tracktion::BeatDuration::fromBeats (juce::jmax (0.0, beat) - timeSig->getStartBeat().inBeats());
        edit->tempoSequence.moveTimeSigStart (index, delta, true);
        broadcastChange();
    }
}

int AudioEngineManager::getNumTempos() const
{
    return edit != nullptr ? edit->tempoSequence.getNumTempos() : 0;
}

double AudioEngineManager::getTempoStartBeat (int index) const
{
    if (edit == nullptr || index < 0 || index >= edit->tempoSequence.getNumTempos())
        return 0.0;

    if (auto* tempo = edit->tempoSequence.getTempo (index))
        return tempo->getStartBeat().inBeats();

    return 0.0;
}

double AudioEngineManager::getTempoBpm (int index) const
{
    if (edit == nullptr || index < 0 || index >= edit->tempoSequence.getNumTempos())
        return 120.0;

    if (auto* tempo = edit->tempoSequence.getTempo (index))
        return tempo->getBpm();

    return 120.0;
}

void AudioEngineManager::insertTempoAtBeat (double beat, double bpm)
{
    if (edit == nullptr)
        return;

    auto& ts = edit->tempoSequence;
    bpm = juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm);
    ts.insertTempo (te::BeatPosition::fromBeats (juce::jmax (0.0, beat)), bpm, 0.0f);
    broadcastChange();
}

void AudioEngineManager::removeTempoAtIndex (int index)
{
    if (edit == nullptr || index <= 0 || index >= edit->tempoSequence.getNumTempos())
        return;

    edit->tempoSequence.removeTempo (index, true);
    broadcastChange();
}

void AudioEngineManager::setTempoBpmAtIndex (int index, double bpm)
{
    if (edit == nullptr || index < 0 || index >= edit->tempoSequence.getNumTempos())
        return;

    if (auto* tempo = edit->tempoSequence.getTempo (index))
    {
        tempo->setBpm (juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm));
        broadcastChange();
    }
}

void AudioEngineManager::moveTempoAtIndexToBeat (int index, double beat)
{
    if (edit == nullptr || index <= 0 || index >= edit->tempoSequence.getNumTempos())
        return;

    if (auto* tempo = edit->tempoSequence.getTempo (index))
    {
        tempo->setStartBeat (te::BeatPosition::fromBeats (juce::jmax (0.0, beat)));
        broadcastChange();
    }
}

bool AudioEngineManager::isMetronomeEnabled() const
{
    if (edit == nullptr) return false;
    return edit->clickTrackEnabled.get();
}

void AudioEngineManager::setMetronomeEnabled (bool enabled)
{
    if (edit == nullptr) return;
    if ((bool) edit->clickTrackEnabled == enabled) return;
    edit->clickTrackEnabled = enabled;
    broadcastChange();
}

void AudioEngineManager::toggleMetronome()
{
    setMetronomeEnabled (! isMetronomeEnabled());
}

float AudioEngineManager::getMetronomeVolumeDb() const
{
    if (edit == nullptr) return 0.0f;
    // Read clickTrackGain directly  -  getClickTrackVolume() clamps to 1.0 (0 dB).
    float gain = edit->clickTrackGain.get();
    return (gain > 0.0f) ? 20.0f * std::log10 (gain) : -60.0f;
}

void AudioEngineManager::setMetronomeVolumeDb (float db)
{
    if (edit == nullptr) return;
    // Write clickTrackGain directly  -  setClickTrackVolume() clamps to 1.0 (0 dB).
    static constexpr float kMaxGain = 31.623f; // +30 dB
    float gain = juce::jlimit (0.0f, kMaxGain, std::pow (10.0f, db / 20.0f));
    edit->clickTrackGain = gain;
    broadcastChange();
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
        broadcastChange();
    }
}

tracktion::WaveAudioClip* AudioEngineManager::insertAudioClipOnTrack (
    tracktion::AudioTrack* track, const juce::File& file, double startTimeSecs)
{
    if (track == nullptr || ! file.existsAsFile()) return nullptr;
    if (isTrackFrozen (track) || isTrackFreezing (track)) return nullptr;

    te::AudioFile af (engine, file);
    double len = af.getLength();
    if (len <= 0.0) len = 1.0;

    auto clip = track->insertWaveClip (
        file.getFileNameWithoutExtension(), file,
        { { te::TimePosition::fromSeconds (startTimeSecs),
            te::TimeDuration::fromSeconds (len) },
          te::TimeDuration::fromSeconds (0.0) },
        false);

    auto* waveClip = dynamic_cast<tracktion::WaveAudioClip*> (clip.get());
    if (waveClip != nullptr)
        broadcastChange();
    return waveClip;
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

void AudioEngineManager::saveProject (const juce::File& file, class ProjectData* projectData)
{
    if (edit == nullptr) return;

    // Plugins only write their current settings into the Edit on a flush.
    edit->flushState();

    auto root = std::make_unique<juce::XmlElement> ("AerionProject");
    root->setAttribute ("version", 1);

    // Serialize runtime state
    auto runtimeState = std::make_unique<juce::XmlElement> ("RuntimeState");

    auto armedTracksElem = std::make_unique<juce::XmlElement> ("ArmedTracks");
    for (auto it = armedTracks.begin(); it != armedTracks.end(); ++it)
    {
        if (*it)
        {
            auto trackElem = std::make_unique<juce::XmlElement> ("Track");
            trackElem->setAttribute ("id", it.getKey());
            armedTracksElem->addChildElement (trackElem.release());
        }
    }
    runtimeState->addChildElement (armedTracksElem.release());

    auto inputDevicesElem = std::make_unique<juce::XmlElement> ("InputDevices");
    for (auto it = inputDeviceMap.begin(); it != inputDeviceMap.end(); ++it)
    {
        auto trackElem = std::make_unique<juce::XmlElement> ("Track");
        trackElem->setAttribute ("id", it.getKey());
        trackElem->setAttribute ("waveDeviceIdx", *it);
        inputDevicesElem->addChildElement (trackElem.release());
    }
    runtimeState->addChildElement (inputDevicesElem.release());

    auto midiInputDevicesElem = std::make_unique<juce::XmlElement> ("MidiInputDevices");
    for (auto it = midiInputDeviceMap.begin(); it != midiInputDeviceMap.end(); ++it)
    {
        auto trackElem = std::make_unique<juce::XmlElement> ("Track");
        trackElem->setAttribute ("id", it.getKey());
        trackElem->setAttribute ("midiDeviceIdx", *it);
        midiInputDevicesElem->addChildElement (trackElem.release());
    }
    runtimeState->addChildElement (midiInputDevicesElem.release());

    auto monitorModesElem = std::make_unique<juce::XmlElement> ("MonitorModes");
    for (auto it = monitorModeMap.begin(); it != monitorModeMap.end(); ++it)
    {
        auto trackElem = std::make_unique<juce::XmlElement> ("Track");
        trackElem->setAttribute ("id", it.getKey());
        trackElem->setAttribute ("mode", *it);
        monitorModesElem->addChildElement (trackElem.release());
    }
    runtimeState->addChildElement (monitorModesElem.release());

    auto punchElem = std::make_unique<juce::XmlElement> ("PunchEnabled");
    punchElem->setAttribute ("value", punchEnabled ? 1 : 0);
    runtimeState->addChildElement (punchElem.release());

    root->addChildElement (runtimeState.release());

    // Serialize project settings
    if (projectData != nullptr)
    {
        auto settings = std::make_unique<juce::XmlElement>("ProjectSettings");
        settings->setAttribute("snapEnabled",  (int) projectData->getProjectTree()
            .getProperty(IDs::snapEnabled,  true));
        settings->setAttribute("snapInterval", (double) projectData->getProjectTree()
            .getProperty(IDs::snapInterval, 1.0));
        root->addChildElement(settings.release());
    }

    // Append the EDIT block
    if (auto editXml = edit->state.createXml())
        root->addChildElement (editXml.release());

    root->writeTo (file);
}

juce::File AudioEngineManager::getTemplatesFolder()
{
    return engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Templates");
}

juce::Array<juce::File> AudioEngineManager::getProjectTemplates()
{
    auto files = getTemplatesFolder().findChildFiles (juce::File::findFiles, false, "*.aerion");
    files.sort();
    return files;
}

juce::File AudioEngineManager::saveProjectAsTemplate (const juce::String& name, ProjectData* projectData, bool includeClips)
{
    const auto safeName = juce::File::createLegalFileName (name.trim());
    if (safeName.isEmpty() || ! getTemplatesFolder().createDirectory())
        return {};

    const auto file = getTemplatesFolder().getChildFile (safeName + ".aerion");
    saveProject (file, projectData);

    if (! includeClips)
    {
        if (auto xml = juce::XmlDocument::parse (file))
        {
            stripClipsFromXml (*xml);
            xml->writeTo (file);
        }
    }

    return file;
}

void AudioEngineManager::stripClipsFromXml (juce::XmlElement& e)
{
    static const juce::StringArray clipTags { "AUDIOCLIP", "MIDICLIP", "STEPCLIP", "EDITCLIP",
                                              "CHORDCLIP", "ARRANGERCLIP", "CONTAINERCLIP" };

    for (int i = e.getNumChildElements(); --i >= 0;)
    {
        auto* child = e.getChildElement (i);
        if (clipTags.contains (child->getTagName()))
            e.removeChildElement (child, true);
        else
            stripClipsFromXml (*child);
    }
}

juce::File AudioEngineManager::getTrackTemplatesFolder()
{
    return getTemplatesFolder().getChildFile ("Tracks");
}

juce::Array<juce::File> AudioEngineManager::getTrackTemplates()
{
    auto files = getTrackTemplatesFolder().findChildFiles (juce::File::findFiles, false, "*" + juce::String (kTrackTemplateExtension));
    files.sort();
    return files;
}

juce::File AudioEngineManager::saveTrackAsTemplate (te::Track* track, const juce::String& name, bool includeClips)
{
    const auto safeName = juce::File::createLegalFileName (name.trim());
    if (track == nullptr || track->isMasterTrack() || safeName.isEmpty()
         || ! getTrackTemplatesFolder().createDirectory())
        return {};

    edit->flushState();

    auto trackXml = track->state.createXml();
    if (trackXml == nullptr)
        return {};

    if (! includeClips)
        stripClipsFromXml (*trackXml);

    // A sidechain source outside the template would point at a track the
    // project it is inserted into does not have.
    juce::StringArray ownIDs;
    std::function<void (const juce::XmlElement&)> collectIDs = [&] (const juce::XmlElement& e)
    {
        if (e.hasTagName (te::IDs::TRACK.toString()) || e.hasTagName (te::IDs::FOLDERTRACK.toString()))
            ownIDs.add (e.getStringAttribute (te::IDs::id));
        for (auto* child : e.getChildIterator())
            collectIDs (*child);
    };
    collectIDs (*trackXml);

    std::function<void (juce::XmlElement&)> dropOutsideSidechains = [&] (juce::XmlElement& e)
    {
        const auto source = e.getStringAttribute (te::IDs::sidechainSourceID);
        if (source.isNotEmpty() && ! ownIDs.contains (source))
            e.removeAttribute (te::IDs::sidechainSourceID);
        for (auto* child : e.getChildIterator())
            dropOutsideSidechains (*child);
    };
    dropOutsideSidechains (*trackXml);

    juce::XmlElement root ("AerionTrackTemplate");
    root.setAttribute ("version", 1);
    root.addChildElement (trackXml.release());

    const auto file = getTrackTemplatesFolder().getChildFile (safeName + kTrackTemplateExtension);
    return root.writeTo (file) ? file : juce::File();
}

te::Track* AudioEngineManager::insertTrackTemplate (const juce::File& file, te::Track* after)
{
    auto xml = juce::XmlDocument::parse (file);
    if (edit == nullptr || xml == nullptr || ! xml->hasTagName ("AerionTrackTemplate"))
        return nullptr;

    auto* trackXml = xml->getFirstChildElement();
    if (trackXml == nullptr)
        return nullptr;

    // New IDs for the track, its sub-tracks, plugins and clips; references
    // between them (sidechain sources) follow.
    te::EditItemID::remapIDs (*trackXml, *edit, nullptr);

    auto state = juce::ValueTree::fromXml (*trackXml);
    if (! state.isValid())
        return nullptr;

    const auto insertPoint = (after == nullptr || after->isMasterTrack())
                               ? te::TrackInsertPoint::getEndOfTracks (*edit)
                               : te::TrackInsertPoint (after->getParentTrack(), after);
    auto newTrack = edit->insertTrack (insertPoint, state, nullptr);

    syncFolderRouting();
    broadcastChange();
    return newTrack.get();
}

void AudioEngineManager::loadProject (const juce::File& file, class ProjectData* projectData)
{
    if (!file.existsAsFile()) return;

    const auto loadStartMs = juce::Time::getMillisecondCounterHiRes();

    if (auto xml = juce::XmlDocument::parse (file))
    {
        juce::XmlElement* editXml = nullptr;

        // Detect format: new wrapper vs legacy
        if (xml->getTagName() == "AerionProject")
        {
            // New format with RuntimeState
            if (auto* runtimeElem = xml->getChildByName ("RuntimeState"))
                restoreRuntimeStateFromXml (*runtimeElem);
            else
            {
                armedTracks.clear();
                inputDeviceMap.clear();
                midiInputDeviceMap.clear();
                monitorModeMap.clear();
                punchEnabled = false;
            }

            editXml = xml->getChildByName ("EDIT");
        }
        else
        {
            // Legacy format: root is the EDIT
            armedTracks.clear();
            inputDeviceMap.clear();
            midiInputDeviceMap.clear();
            monitorModeMap.clear();
            punchEnabled = false;
            editXml = xml.get();
        }

        if (editXml == nullptr) return;

        cancelActiveFreezeJobs();

        if (edit != nullptr && editListener != nullptr)
            edit->removeListener (editListener.get());

        auto vt = juce::ValueTree::fromXml (*editXml);
        releaseEditResources();
        edit = te::loadEditFromState (engine, vt, te::Edit::forEditing);
        ++editGeneration;
        attachEditListenerToCurrentEdit();

        freezingTracks.clear();
        syncFolderRouting();

        for (auto* track : te::getAudioTracks (*edit))
        {
            const bool isMidiTrack = (bool) track->state.getProperty(IDs::isMidiTrack, false);
            if (! isMidiTrack
                && dynamic_cast<te::FolderTrack*> (track) == nullptr
                && track->getLevelMeterPlugin() == nullptr)
            {
                if (auto p = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}))
                    track->pluginList.insertPlugin (p, 0, nullptr);
            }
        }

        applyRestoredRuntimeStateToEdit();

        // Collect missing audio files
        juce::StringArray missingFiles;
        for (auto* track : te::getAudioTracks (*edit))
        {
            for (auto* clip : track->getClips())
            {
                if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip))
                {
                    auto srcFile = waveClip->getSourceFileReference().getFile();
                    if (!srcFile.existsAsFile() && srcFile.getFullPathName().isNotEmpty())
                    {
                        if (!missingFiles.contains (srcFile.getFullPathName()))
                            missingFiles.add (srcFile.getFullPathName());
                    }
                }
            }
        }

        broadcastStatusChange();

        // Restore snap settings from <ProjectSettings> if present
        if (projectData != nullptr && xml->getTagName() == "AerionProject")
        {
            if (auto* settingsElem = xml->getChildByName("ProjectSettings"))
            {
                projectData->getProjectTree().setProperty(
                    IDs::snapEnabled,  settingsElem->getBoolAttribute("snapEnabled",  true),  nullptr);
                projectData->getProjectTree().setProperty(
                    IDs::snapInterval, settingsElem->getDoubleAttribute("snapInterval", 1.0), nullptr);
            }
        }

        // Show non-blocking alert for missing files
        if (!missingFiles.isEmpty())
        {
            juce::MessageManager::callAsync ([missingFiles]
            {
                juce::String message = "The following audio files could not be found:\n\n";
                for (const auto& filePath : missingFiles)
                    message += filePath + "\n";
                message += "\nProject loaded, but these clips are silent.";

                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                       "Missing Audio Files", message);
            });
        }

        juce::Logger::writeToLog ("Project: loaded " + file.getFileName()
                                  + " in " + juce::String (juce::Time::getMillisecondCounterHiRes() - loadStartMs, 1)
                                  + " ms");
    }
}

void AudioEngineManager::restoreRuntimeStateFromXml (const juce::XmlElement& runtimeXml)
{
    armedTracks.clear();
    inputDeviceMap.clear();
    midiInputDeviceMap.clear();
    monitorModeMap.clear();
    punchEnabled = false;

    // Restore armed tracks
    if (auto* armedElem = runtimeXml.getChildByName ("ArmedTracks"))
    {
        forEachXmlChildElement (*armedElem, trackElem)
        {
            if (trackElem->hasTagName ("Track"))
            {
                if (auto id = trackElem->getStringAttribute ("id"); id.isNotEmpty())
                    armedTracks.set (id, true);
            }
        }
    }

    // Restore input devices
    if (auto* inputElem = runtimeXml.getChildByName ("InputDevices"))
    {
        forEachXmlChildElement (*inputElem, trackElem)
        {
            if (trackElem->hasTagName ("Track"))
            {
                if (auto id = trackElem->getStringAttribute ("id"); id.isNotEmpty())
                {
                    int waveDeviceIdx = trackElem->getIntAttribute ("waveDeviceIdx", -1);
                    if (waveDeviceIdx >= 0)
                        inputDeviceMap.set (id, waveDeviceIdx);
                }
            }
        }
    }

    // Restore MIDI input devices
    if (auto* midiElem = runtimeXml.getChildByName ("MidiInputDevices"))
    {
        forEachXmlChildElement (*midiElem, trackElem)
        {
            if (trackElem->hasTagName ("Track"))
            {
                if (auto id = trackElem->getStringAttribute ("id"); id.isNotEmpty())
                {
                    int midiDeviceIdx = trackElem->getIntAttribute ("midiDeviceIdx", -1);
                    if (midiDeviceIdx >= -1)  // -1 means "all"
                        midiInputDeviceMap.set (id, midiDeviceIdx);
                }
            }
        }
    }

    // Restore monitor modes
    if (auto* monitorElem = runtimeXml.getChildByName ("MonitorModes"))
    {
        forEachXmlChildElement (*monitorElem, trackElem)
        {
            if (trackElem->hasTagName ("Track"))
            {
                if (auto id = trackElem->getStringAttribute ("id"); id.isNotEmpty())
                {
                    int mode = trackElem->getIntAttribute ("mode", (int) MonitorMode::Auto);
                    monitorModeMap.set (id, mode);
                }
            }
        }
    }

    // Restore punch enabled
    if (auto* punchElem = runtimeXml.getChildByName ("PunchEnabled"))
        punchEnabled = punchElem->getIntAttribute ("value", 0) != 0;
}

void AudioEngineManager::applyRestoredRuntimeStateToEdit()
{
    if (edit == nullptr)
        return;

    for (auto* track : te::getAllTracks (*edit))
    {
        if (track == nullptr)
            continue;

        const auto id = track->itemID.toString();

        if (inputDeviceMap.contains (id))
            track->state.setProperty (IDs::trackInputDeviceIdx, inputDeviceMap[id], nullptr);
        else if (track->state.hasProperty (IDs::trackInputDeviceIdx))
            inputDeviceMap.set (id, (int) track->state.getProperty (IDs::trackInputDeviceIdx, -1));

        if (midiInputDeviceMap.contains (id))
            track->state.setProperty (IDs::midiInputDevice, midiInputDeviceMap[id], nullptr);
        else if (track->state.hasProperty (IDs::midiInputDevice))
            midiInputDeviceMap.set (id, (int) track->state.getProperty (IDs::midiInputDevice, -1));

        if (monitorModeMap.contains (id))
        {
            auto mode = static_cast<MonitorMode> (monitorModeMap[id]);
            if (mode != MonitorMode::Auto && mode != MonitorMode::On && mode != MonitorMode::Off)
            {
                mode = MonitorMode::Auto;
                monitorModeMap.set (id, static_cast<int> (mode));
            }

            track->state.setProperty (IDs::monitorMode, static_cast<int> (mode), nullptr);
        }
        else if (track->state.hasProperty (IDs::monitorMode))
        {
            auto mode = static_cast<MonitorMode> ((int) track->state.getProperty (IDs::monitorMode,
                                                                                  static_cast<int> (MonitorMode::Auto)));
            if (mode != MonitorMode::Auto && mode != MonitorMode::On && mode != MonitorMode::Off)
                mode = MonitorMode::Auto;

            monitorModeMap.set (id, static_cast<int> (mode));
            track->state.setProperty (IDs::monitorMode, static_cast<int> (mode), nullptr);
        }

        if (armedTracks.contains (id) && armedTracks[id])
            setTrackArmed (track, true);
    }
}

float AudioEngineManager::measureClipPeak (te::WaveAudioClip& clip)
{
    const auto file = clip.getOriginalFile();
    std::unique_ptr<juce::AudioFormatReader> reader (te::AudioFileUtils::createReaderFor (engine, file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return -1.0f;

    // The used source range, in original-file seconds. A reversed clip's offset
    // counts from the end of the source.
    // A looping clip may play any part of the source, so it uses all of it.
    const double sourceLen = (double) reader->lengthInSamples / reader->sampleRate;
    double span  = clip.getPosition().getLength().inSeconds() * clip.getSpeedRatio();
    double start = clip.getPosition().getOffset().inSeconds() * clip.getSpeedRatio();
    if (clip.getIsReversed())
        start = sourceLen - start - span;
    if (clip.isLooping())
    {
        start = 0.0;
        span  = sourceLen;
    }

    const auto startSample = (juce::int64) (juce::jlimit (0.0, sourceLen, start) * reader->sampleRate);
    const auto numSamples  = juce::jmin (reader->lengthInSamples - startSample, (juce::int64) (span * reader->sampleRate));

    if (numSamples <= 0)
        return -1.0f;

    const int numChannels = (int) reader->numChannels;
    std::vector<juce::Range<float>> levels ((size_t) numChannels);
    reader->readMaxLevels (startSample, numSamples, levels.data(), numChannels);

    float peak = 0.0f;
    for (auto& r : levels)
        peak = juce::jmax (peak, std::abs (r.getStart()), std::abs (r.getEnd()));
    return peak;
}

bool AudioEngineManager::normaliseClip (te::WaveAudioClip& clip)
{
    const float peak = measureClipPeak (clip);
    if (peak <= 0.0f)
        return false;

    clip.setGainDB (kNormaliseTargetDb - juce::Decibels::gainToDecibels (peak));
    return true;
}

te::SmartThumbnail& AudioEngineManager::getThumbnailForClip (te::WaveAudioClip& clip, juce::Component& comp)
{
    auto it = thumbnails.find (clip.itemID.getRawID());
    if (it != thumbnails.end())
        return *it->second;

    // Always the original file: a reversed clip plays a rendered copy, and the
    // Timeline mirrors the original instead (it exists before the render does).
    auto thumb = std::make_unique<te::SmartThumbnail> (engine, te::AudioFile (engine, clip.getOriginalFile()), comp, edit.get());
    auto& ref = *thumb;
    thumbnails[clip.itemID.getRawID()] = std::move (thumb);
    return ref;
}

bool AudioEngineManager::isTrackFrozen (te::AudioTrack* track) const
{
    if (track == nullptr) return false;
    return (bool) track->state.getProperty(IDs::frozen, false);
}

bool AudioEngineManager::isTrackFreezing (te::Track* track) const
{
    if (track == nullptr)
        return false;

    const auto id = track->itemID.toString();
    return freezingTracks.contains (id) && freezingTracks[id];
}

struct FreezeListener : public MixdownExportJob::Listener {
    juce::WeakReference<AudioEngineManager> owner;
    juce::String        trackId;
    juce::ValueTree     preFreeze;
    juce::File          destFile;
    double              insertAt;
    uint64_t            generation;

    FreezeListener(AudioEngineManager* o, juce::String id, juce::ValueTree state, juce::File f, double at, uint64_t gen)
        : owner(o), trackId(std::move(id)), preFreeze(std::move(state)), destFile(f), insertAt(at), generation(gen) {}

    void exportProgress(float) override {}

    void exportFinished(const MixdownExportJob::Result& result) override {
        auto ownerRef = owner;
        auto id = trackId;
        auto state = preFreeze;
        auto file = destFile;
        auto at = insertAt;
        auto gen = generation;

        juce::MessageManager::callAsync([ownerRef, id, state, file, at, gen, result]() mutable {
            if (ownerRef == nullptr)
                return;

            auto active = ownerRef->activeFreezeJobs.find (id);
            if (active == ownerRef->activeFreezeJobs.end() || ownerRef->editGeneration != gen)
                return;

            if (auto listener = ownerRef->activeFreezeListeners.find (id);
                listener != ownerRef->activeFreezeListeners.end())
            {
                active->second->removeListener (listener->second);
                delete listener->second;
                ownerRef->activeFreezeListeners.erase (listener);
            }

            ownerRef->activeFreezeJobs.erase (active);
            ownerRef->freezingTracks.remove (id);

            if (! result.ok)
            {
                ownerRef->broadcastStatusChange();
                return;
            }

            auto* track = dynamic_cast<te::AudioTrack*> (ownerRef->findTrackById (id));
            if (track == nullptr)
            {
                ownerRef->broadcastStatusChange();
                return;
            }

            auto oldPreFreeze = track->state.getChildWithName(IDs::preFreeze);
            if (oldPreFreeze.isValid())
                track->state.removeChild(oldPreFreeze, nullptr);
            track->state.addChild(state, -1, nullptr);

            auto clips = track->getClips();
            for (auto* c : clips)
                c->removeFromParent();

            ownerRef->insertAudioClipOnTrack(track, result.outputFile, at);

            for (auto* p : track->getAllPlugins())
                if (dynamic_cast<te::ExternalPlugin*>(p))
                    p->setEnabled(false);

            track->state.setProperty(IDs::frozen, true, nullptr);
            track->state.setProperty(IDs::freezeFile, file.getFullPathName(), nullptr);
            ownerRef->broadcastChange();
        });
    }
};

void AudioEngineManager::freezeTrack (te::AudioTrack* track)
{
    if (track == nullptr || isTrackFrozen(track)) return;

    const auto trackId = track->itemID.toString();
    if (freezingTracks.contains (trackId) && freezingTracks[trackId])
        return;

    auto clips = track->getClips();
    if (clips.isEmpty()) return;

    // Calculate time range encompassing all clips
    auto totalRange = track->getTotalRange();
    double rangeStart = totalRange.getStart().inSeconds();
    double rangeEnd = totalRange.getEnd().inSeconds();

    if (rangeEnd <= rangeStart) return;

    // Serialize pre-freeze clip state. The clip ValueTree is the source of truth
    // (trim, offset, fades, MIDI notes). Numeric fields remain so snapshots
    // written before that copy existed can still be restored.
    juce::ValueTree preFreeze(IDs::preFreeze);
    for (auto* clip : clips) {
        juce::ValueTree cs("ClipState");
        cs.appendChild (clip->state.createCopy(), nullptr);

        if (auto* wc = dynamic_cast<te::WaveAudioClip*>(clip)) {
            cs.setProperty("file", wc->getSourceFileReference().getFile().getFullPathName(), nullptr);
            cs.setProperty(IDs::preFreezeClipType, "audio", nullptr);
            cs.setProperty("offset", wc->getPosition().getOffset().inSeconds(), nullptr);
            cs.setProperty("fadeIn", wc->getFadeIn().inSeconds(), nullptr);
            cs.setProperty("fadeOut", wc->getFadeOut().inSeconds(), nullptr);
        } else if (dynamic_cast<te::MidiClip*>(clip) != nullptr) {
            cs.setProperty(IDs::preFreezeClipType, "midi", nullptr);
        }
        cs.setProperty("startBeat", clip->getStartBeat().inBeats(), nullptr);
        cs.setProperty("endBeat", clip->getEndBeat().inBeats(), nullptr);
        preFreeze.addChild(cs, -1, nullptr);
    }

    // Build destination WAV path
    auto freezeDir = engine.getPropertyStorage().getAppPrefsFolder().getChildFile("Recovery");
    if (!freezeDir.createDirectory().wasOk()) return;

    auto destFile = freezeDir.getChildFile("freeze_" + track->itemID.toString() + ".wav");

    // Kick off async render via MixdownExportJob
    int trackIdx = te::getAllTracks(*edit).indexOf(track);
    auto range = te::TimeRange(te::TimePosition::fromSeconds(rangeStart),
                               te::TimePosition::fromSeconds(rangeEnd));

    auto job = std::make_shared<MixdownExportJob>(
        engine, *edit, destFile, range,
        44100.0, 2, 500 /*tailMs*/, trackIdx);

    if (!job->isValid()) return;

    freezingTracks.set (trackId, true);
    broadcastChange();

    auto* listener = new FreezeListener(this, trackId, preFreeze, destFile, rangeStart, editGeneration);
    job->addListener(listener);
    activeFreezeJobs[trackId] = job;
    activeFreezeListeners[trackId] = listener;
    job->start();
}

namespace
{
    juce::ValueTree savedClipStateFrom (const juce::ValueTree& clipSnapshot)
    {
        for (int i = 0; i < clipSnapshot.getNumChildren(); ++i)
        {
            auto child = clipSnapshot.getChild (i);
            if (te::Clip::isClipState (child))
                return child;
        }

        return {};
    }

    void restorePreFreezeClip (AudioEngineManager& owner, te::AudioTrack& track, const juce::ValueTree& cs)
    {
        const double startBeat = cs.getProperty ("startBeat", 0.0);
        const double endBeat   = cs.getProperty ("endBeat", startBeat);
        auto& ts = owner.getEdit().tempoSequence;
        const auto startTime = ts.beatsToTime (te::BeatPosition::fromBeats (startBeat));
        const auto endTime   = ts.beatsToTime (te::BeatPosition::fromBeats (endBeat));

        // A detached copy: insertClipWithState rejects a node that already has a parent.
        if (auto saved = savedClipStateFrom (cs); saved.isValid())
        {
            if (auto* restored = track.insertClipWithState (saved.createCopy()))
            {
                // Musical position comes from the snapshot. keepLength retains the
                // trimmed duration; preserveSync left false so the source offset stays.
                restored->setStart (startTime, false, true);
                return;
            }
        }

        const auto clipType = cs.getProperty (IDs::preFreezeClipType, "audio").toString();
        if (clipType == "midi")
            return;

        const auto file = juce::File (cs.getProperty ("file").toString());
        if (! file.existsAsFile())
            return;

        auto* restored = owner.insertAudioClipOnTrack (&track, file, startTime.inSeconds());
        if (restored == nullptr)
            return;

        const double lengthSecs = juce::jmax (0.01, (endTime - startTime).inSeconds());
        restored->setLength (te::TimeDuration::fromSeconds (lengthSecs), true);

        if (cs.hasProperty ("offset"))
            restored->setOffset (te::TimeDuration::fromSeconds ((double) cs.getProperty ("offset")));

        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (restored))
        {
            if (cs.hasProperty ("fadeIn"))
                wave->setFadeIn (te::TimeDuration::fromSeconds ((double) cs.getProperty ("fadeIn")));
            if (cs.hasProperty ("fadeOut"))
                wave->setFadeOut (te::TimeDuration::fromSeconds ((double) cs.getProperty ("fadeOut")));
        }
    }
}

void AudioEngineManager::unfreezeTrack (te::AudioTrack* track)
{
    if (track == nullptr || !isTrackFrozen(track)) return;

    auto preFreeze = track->state.getChildWithName(IDs::preFreeze);
    if (!preFreeze.isValid()) return;

    // Remove the frozen WAV clip
    auto clips = track->getClips();
    for (auto* c : clips)
        c->removeFromParent();

    // The public insert path rejects frozen tracks; unfreeze is the controlled
    // transition back to editable source clips.
    track->state.setProperty(IDs::frozen, false, nullptr);

    // Re-insert original clips from preFreeze state
    for (int i = 0; i < preFreeze.getNumChildren(); ++i)
        restorePreFreezeClip (*this, *track, preFreeze.getChild (i));

    // Re-enable ExternalPlugins
    for (auto* p : track->getAllPlugins())
        if (dynamic_cast<te::ExternalPlugin*>(p))
            p->setEnabled(true);

    // Delete the freeze WAV file
    juce::File freezeFile(track->state.getProperty(IDs::freezeFile).toString());
    if (freezeFile.existsAsFile())
        freezeFile.deleteFile();

    // Clear frozen state
    track->state.setProperty(IDs::freezeFile, "", nullptr);
    track->state.removeChild(preFreeze, nullptr);

    broadcastChange();
}

void AudioEngineManager::createNewProject()
{
    cancelActiveFreezeJobs();
    releaseEditResources();
    setupInitialEdit();
    applyDefaultMidiMappings (nullptr);
    ++editGeneration;
    armedTracks.clear();
    inputDeviceMap.clear();
    midiInputDeviceMap.clear();
    monitorModeMap.clear();
    punchEnabled = false;
    syncFolderRouting();
    broadcastStatusChange();
}

void AudioEngineManager::cancelActiveFreezeJobs()
{
    for (auto& jobEntry : activeFreezeJobs)
    {
        if (auto listener = activeFreezeListeners.find (jobEntry.first);
            listener != activeFreezeListeners.end())
        {
            if (jobEntry.second != nullptr)
                jobEntry.second->removeListener (listener->second);
            delete listener->second;
        }
    }

    activeFreezeListeners.clear();
    activeFreezeJobs.clear();
    freezingTracks.clear();
}

bool AudioEngineManager::hasCrashRecovery() const
{
    return hadCrash;
}

juce::File AudioEngineManager::getRecoveryFile() const
{
    return engine.getPropertyStorage().getAppPrefsFolder()
                .getChildFile("Recovery")
                .getChildFile("autosave.aerion");
}

void AudioEngineManager::autoSave (ProjectData* pd)
{
    auto f = getRecoveryFile();
    f.getParentDirectory().createDirectory();
    saveProject(f, pd);
}

int AudioEngineManager::getAutoSaveIntervalMins() const
{
    // Use const_cast since getUserSettings() is non-const but we only read from it
    auto* s = const_cast<AudioEngineManager*>(this)->appProperties.getUserSettings();
    return s ? s->getIntValue("autoSaveIntervalMins", 5) : 5;
}

void AudioEngineManager::setAutoSaveIntervalMins (int mins)
{
    if (auto* s = appProperties.getUserSettings())
    {
        s->setValue("autoSaveIntervalMins", mins);
        s->saveIfNeeded();
    }
}

juce::StringArray AudioEngineManager::collectAndSave (const juce::File& projectFile, class ProjectData* projectData)
{
    juce::StringArray skipped;
    if (edit == nullptr || !projectFile.hasFileExtension ("aerion")) return skipped;

    // Create "<ProjectName> Files/" folder beside the .aerion file
    auto mediaFolder = projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + " Files");
    if (!mediaFolder.createDirectory().wasOk())
        return skipped;

    // Walk all audio clips and copy files
    std::map<juce::String, int> fileCountMap;  // for collision detection

    for (auto* track : te::getAudioTracks (*edit))
    {
        for (auto* clip : track->getClips())
        {
            if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip))
            {
                auto srcFile = waveClip->getSourceFileReference().getFile();

                // Skip if file doesn't exist
                if (!srcFile.existsAsFile())
                {
                    if (srcFile.getFullPathName().isNotEmpty())
                        skipped.addIfNotAlreadyThere (srcFile.getFullPathName());
                    continue;
                }

                // Skip if already inside media folder
                if (srcFile.getParentDirectory() == mediaFolder)
                    continue;

                // Handle filename collisions with _1, _2, ... counter suffix
                auto baseName = srcFile.getFileNameWithoutExtension();
                auto fileExt  = srcFile.getFileExtension();
                int count = ++fileCountMap[baseName + fileExt];

                juce::String destName = baseName + fileExt;
                if (count > 1)
                    destName = baseName + "_" + juce::String (count - 1) + fileExt;

                auto destFile = mediaFolder.getChildFile (destName);

                // Copy file to media folder
                if (srcFile.copyFileTo (destFile))
                {
                    // Update clip to point to new path
                    waveClip->getSourceFileReference().setToDirectFileReference (destFile, false);
                }
            }
        }
    }

    // Save the project with updated paths
    saveProject (projectFile, projectData);
    thumbnails.clear();
    broadcastStatusChange();

    return skipped;
}

void AudioEngineManager::clearRecentProjects()
{
    recentProjects = juce::RecentlyOpenedFilesList();
    if (auto* s = appProperties.getUserSettings())
    {
        s->setValue ("recentProjects", juce::String());
        s->saveIfNeeded();
    }
}

bool AudioEngineManager::hasUnsavedEdits() const
{
    return edit != nullptr && edit->hasChangedSinceSaved();
}

void AudioEngineManager::markEditSaved()
{
    // resetChangedStatus() flushes pending undo notifications first, so edits
    // made just before this call cannot re-mark the Edit afterwards.
    if (edit != nullptr)
        edit->resetChangedStatus();
}

void AudioEngineManager::broadcastStatusChange()
{
    listeners.call ([] (Listener& l) { l.engineStatusChanged(); });
}

void AudioEngineManager::broadcastChange()
{
    listeners.call ([] (Listener& l) { l.editStateChanged(); });
}

void AudioEngineManager::notifyScanFinished (bool finishedNormally)
{
    scanInFlight.store (false);

    if (finishedNormally)
        if (auto* s = appProperties.getUserSettings())
        {
            s->setValue ("pluginScanCompleted", true);
            s->saveIfNeeded();
        }

    broadcastStatusChange();

    if (onScanFinished)
        onScanFinished();
}

bool AudioEngineManager::shouldRunStartupScan()
{
    auto cacheFile = engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Plugins.xml");
    if (! cacheFile.existsAsFile())
        return true; // First run.

    // On by default: the scan runs in the background and only loads plugin
    // files that are new or changed since the last scan, so it doesn't slow startup.
    if (auto* settings = appProperties.getUserSettings())
        return settings->getBoolValue ("pluginScanOnStartup", true);

    return true;
}

bool AudioEngineManager::stopScanThread (int waitMs)
{
    if (scanThread == nullptr)
        return true;

    // A scan that waits on its scanner child process sees this within about
    // 10 ms (Tracktion's CustomScanner polls shouldExit()).
    scanThread->signalThreadShouldExit();

    if (! scanThread->stopThread (waitMs))
    {
        // Still busy, perhaps waiting for a scanner child that is stuck in a
        // plugin: end the child, which also lets the thread stop.
        juce::Logger::writeToLog ("Plugin scan: still running after " + juce::String (waitMs)
                                  + " ms; ending the scanner process");
        ScannerProcesses::endChildren();

        if (! scanThread->stopThread (kScanStopAfterKillMs))
            return false;   // not destroyed: its destructor would wait for it
    }

    scanThread.reset();
    return true;
}


namespace
{
   #if JUCE_WINDOWS
    /** Program Files folders from env (avoids trusting huge default search trees). */
    static juce::File windowsProgramFilesNative()
    {
        auto s64 = juce::SystemStats::getEnvironmentVariable ("ProgramW6432", {});
        if (! s64.isEmpty()) return juce::File (s64);
        return juce::File (juce::SystemStats::getEnvironmentVariable ("ProgramFiles", {}));
    }

    static juce::File windowsProgramFilesX86()
    {
        return juce::File (juce::SystemStats::getEnvironmentVariable ("ProgramFiles(x86)", {}));
    }

    static void addExistingDirPath (juce::FileSearchPath& out, const juce::File& dir)
    {
        if (dir.isDirectory())
            out.add (dir);
    }

    static juce::String trimPathSeparators (juce::String s)
    {
        while (s.endsWithChar ('\\') || s.endsWithChar ('/'))
            s = s.dropLastCharacters (1);
        return s.trimEnd();
    }

    static bool mentionsTypicalWindowsPluginSubtree (const juce::String& full)
    {
        return full.containsIgnoreCase ("VST3") || full.containsIgnoreCase ("VST")
               || full.containsIgnoreCase ("VstPlugins") || full.containsIgnoreCase ("CLAP")
               || full.containsIgnoreCase ("LV2") || full.containsIgnoreCase ("Plug-Ins")
               || full.containsIgnoreCase ("PlugIns");
    }

    /** JUCE/VST installers sometimes register huge search roots; recursive+VST probing is then enormous. */
    static bool windowsSearchRootLikelyTooBroad (const juce::File& dir)
    {
        if (! dir.isDirectory())
            return true;

        auto full = trimPathSeparators (dir.getFullPathName());
        if (full.isEmpty())
            return true;

        // Bare drive roots — never crawl from here recursively.
        {
            auto s = trimPathSeparators (dir.getFullPathName());
            if (s.length() >= 2 && s[1] == ':')
            {
                auto rest = trimPathSeparators (s.substring (2).replaceCharacter ('/', '\\'));
                if (rest.isEmpty())
                    return true;
            }
        }

        const auto pf64 = windowsProgramFilesNative();
        const auto pf86 = windowsProgramFilesX86();

        auto sameDir = [](const juce::File& a, const juce::File& b)
        {
            return a.exists() && b.exists()
                   && trimPathSeparators (a.getFullPathName()).equalsIgnoreCase (
                       trimPathSeparators (b.getFullPathName()));
        };

        if (pf64.exists() && sameDir (dir, pf64)) return true;
        if (pf86.exists() && sameDir (dir, pf86)) return true;

        if (pf64.exists() && dir.isAChildOf (pf64))
            return ! mentionsTypicalWindowsPluginSubtree (full);

        if (pf86.exists() && dir.isAChildOf (pf86))
            return ! mentionsTypicalWindowsPluginSubtree (full);

        if (full.endsWithIgnoreCase ("\\Users"))
            return true;

        return false;
    }

    /** For VST2/VST3 we only crawl standard installer locations plus any user-local VST3 folder Windows uses now. */
    static juce::FileSearchPath windowsStandardHostFormatPaths (const juce::String& formatName)
    {
        juce::FileSearchPath path;

        const auto pf64 = windowsProgramFilesNative();
        const auto pf86 = windowsProgramFilesX86();

        auto addPfChild = [&](const juce::String& leaf)
        {
            if (pf64.exists()) addExistingDirPath (path, pf64.getChildFile (leaf));
            if (pf86.exists()) addExistingDirPath (path, pf86.getChildFile (leaf));
        };

        if (formatName == "VST3")
        {
            addPfChild ("Common Files/VST3");

            if (auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {}); local.isNotEmpty())
                addExistingDirPath (path, juce::File (local).getChildFile ("Programs/Common/VST3"));

            addExistingDirPath (path,
                                juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                    .getChildFile ("Programs/Common/VST3"));

            // Legacy hard-coded fallbacks still seen in broken installers.
            addExistingDirPath (path, juce::File ("C:\\Program Files\\Common Files\\VST3"));
            addExistingDirPath (path, juce::File ("C:\\Program Files (x86)\\Common Files\\VST3"));
        }

        if (formatName == "VST")
        {
            addPfChild ("VSTPlugins");
            addPfChild ("Steinberg/VstPlugins");
            addExistingDirPath (path, juce::File ("C:\\VSTPlugins"));
            addExistingDirPath (path, juce::File ("D:\\VSTPlugins"));
            addExistingDirPath (path, juce::File ("C:\\Program Files\\VSTPlugins"));
            addExistingDirPath (path, juce::File ("C:\\Program Files\\Steinberg\\VstPlugins"));
            addExistingDirPath (path, juce::File ("C:\\Program Files (x86)\\VSTPlugins"));
            addExistingDirPath (path, juce::File ("C:\\Program Files (x86)\\Steinberg\\VstPlugins"));
        }

        if (formatName == "CLAP")
            addPfChild ("Common Files/CLAP");

        if (formatName == "LV2")
            addPfChild ("Common Files/LV2");

        return path;
    }

    static juce::FileSearchPath sanitizedWindowsFallbackPaths (juce::FileSearchPath raw)
    {
        juce::FileSearchPath out;
        for (int pi = 0; pi < raw.getNumPaths(); ++pi)
        {
            auto d = raw[pi];

            juce::String full = trimPathSeparators (d.getFullPathName());

            auto pf64_norm = trimPathSeparators (windowsProgramFilesNative().getFullPathName());
            auto pf86_norm = trimPathSeparators (windowsProgramFilesX86().getFullPathName());

            if (full.equalsIgnoreCase (pf64_norm) || full.equalsIgnoreCase (pf86_norm))
                continue;

            if (windowsSearchRootLikelyTooBroad (d))
                continue;

            addExistingDirPath (out, d);
        }

        return out;
    }

   #endif // JUCE_WINDOWS

    struct PluginScanThread : public juce::Thread
    {
        PluginScanThread (AudioEngineManager& m, juce::File f)
            : Thread ("PluginScanner"), owner (m), weakOwner (&m), cache (f) {}

        void run() override
        {
            const auto scanStartMs = juce::Time::getMillisecondCounterHiRes();
            auto& fm   = owner.getEngine().getPluginManager().pluginFormatManager;
            auto& list = owner.getEngine().getPluginManager().knownPluginList;

            constexpr int scanProgressThrottleMs = 140;
            int64 lastProgressMs = juce::Time::currentTimeMillis() - scanProgressThrottleMs;

            for (int fi = 0; fi < fm.getNumFormats(); ++fi)
            {
                if (threadShouldExit()) break;

                auto* format = fm.getFormat (fi);
                if (format == nullptr) continue;

                const auto formatName = format->getName();
                juce::FileSearchPath path;

               #if JUCE_WINDOWS
                // Known host formats live in predictable folders — avoid blindly merging JUCE's defaults
                // (those can recurse over entire PF / roaming trees on some setups).
                if (formatName == "VST3" || formatName == "VST" || formatName == "LV2"
                    || formatName == "CLAP")
                    path = windowsStandardHostFormatPaths (formatName);

                if (path.getNumPaths() == 0)
                    path = sanitizedWindowsFallbackPaths (format->getDefaultLocationsToSearch());
               #else
                path = format->getDefaultLocationsToSearch();
               #endif

                juce::PluginDirectoryScanner scanner (list, *format, path, /*recursive=*/ true, cache);
                juce::String pluginName;

                while (! threadShouldExit() && scanner.scanNextFile (true, pluginName))
                {
                    for (auto& failed : scanner.getFailedFiles())
                    {
                        if (failedFiles.contains (failed))
                            continue;

                        failedFiles.add (failed);
                        juce::Logger::writeToLog ("Plugin scan: skipped " + failed
                                                  + " (it crashed or failed to load in the scanner)");
                    }

                    const auto nowMs = juce::Time::currentTimeMillis();

                    if (nowMs - lastProgressMs < scanProgressThrottleMs && pluginName.isNotEmpty())
                        continue;

                    lastProgressMs = nowMs;

                    juce::WeakReference<AudioEngineManager> weak = weakOwner;
                    auto nameCopy = pluginName;

                    juce::MessageManager::callAsync ([weak, nameCopy]()
                    {
                        if (auto* o = weak.get())
                            if (o->onScanProgress != nullptr && nameCopy.isNotEmpty())
                                o->onScanProgress (nameCopy);
                    });
                }
            }

            const bool finishedNormally = ! threadShouldExit();

            if (finishedNormally)
            {
                if (auto xml = list.createXml())
                    xml->writeTo (cache);
            }

            juce::WeakReference<AudioEngineManager> weak = weakOwner;
            const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - scanStartMs;
            juce::MessageManager::callAsync ([weak, finishedNormally]
            {
                if (auto* o = weak.get())
                    o->notifyScanFinished (finishedNormally);
            });
            juce::Logger::writeToLog ("Startup: plugin scan "
                                      + juce::String (finishedNormally ? "finished" : "cancelled")
                                      + " in " + juce::String (elapsedMs, 1) + " ms, "
                                      + juce::String (failedFiles.size()) + " plugin file(s) skipped");
        }

        juce::StringArray failedFiles;
        AudioEngineManager& owner;
        juce::WeakReference<AudioEngineManager> weakOwner;
        juce::File cache;
    };
}

void AudioEngineManager::scanPlugins()
{
    if (scanInFlight.exchange (true))
        return; // Already scanning.

    const auto scanSetupStartMs = juce::Time::getMillisecondCounterHiRes();
    auto& list = engine.getPluginManager().knownPluginList;
    auto cacheFile = engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Plugins.xml");

    if (cacheFile.existsAsFile())
        if (auto xml = juce::XmlDocument::parse (cacheFile))
            list.recreateFromXml (*xml);

    auto& fm = engine.getPluginManager().pluginFormatManager;
    if (fm.getNumFormats() == 0)
        fm.addDefaultFormats();

    if (fm.getNumFormats() == 0)
    {
        scanInFlight.store (false);
        if (onScanFinished) onScanFinished();
        return;
    }

    if (! stopScanThread (kScanStopWaitMs))
    {
        juce::Logger::writeToLog ("Plugin scan: the previous scan is still running; not starting another");
        return;   // scanInFlight stays set: that scan is still in flight
    }

    auto t = std::make_unique<PluginScanThread> (*this, cacheFile);
    scanThread = std::unique_ptr<juce::Thread> (t.release());
    scanThread->startThread();
    juce::Logger::writeToLog ("Startup: plugin scan setup completed in "
                              + juce::String (juce::Time::getMillisecondCounterHiRes() - scanSetupStartMs, 1)
                              + " ms");
}

void AudioEngineManager::deletePluginFromBrowserList (const juce::PluginDescription& desc)
{
    // Avoid racing the background scan thread which is mutating the same list.
    if (isScanningPlugins())
        return;

    auto& list = engine.getPluginManager().knownPluginList;
    list.removeType (desc);

    auto cacheFile = engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Plugins.xml");
    if (auto xml = list.createXml())
        xml->writeTo (cacheFile);

    broadcastStatusChange();
}

tracktion::Plugin::Ptr AudioEngineManager::addPluginToTrack (te::Track* track, const juce::PluginDescription& desc)
{
    if (track == nullptr) return {};

    if (auto* at = dynamic_cast<te::AudioTrack*> (track))
        if (isTrackFrozen (at) || isTrackFreezing (at))
            return {};

    // pluginList lives on the base Track, so this works for audio, folder and master tracks.
    auto p = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);
    if (p != nullptr)
    {
        track->pluginList.insertPlugin (p, track->pluginList.size(), nullptr);
        p->setEnabled (true);
        p->setProcessingEnabled (true);
        // Every view showing this track (Inspector, Mixer, Timeline) refreshes,
        // not just the one the plugin was dropped on.
        broadcastChange();
        return p;
    }
    return {};
}

bool AudioEngineManager::isInsertDevice (te::Plugin* plugin)
{
    return dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr;
}

juce::Array<te::Plugin*> AudioEngineManager::getInsertDevices (te::Track* track)
{
    juce::Array<te::Plugin*> devices;

    if (track != nullptr)
        for (auto* p : track->pluginList)
            if (isInsertDevice (p))
                devices.add (p);

    return devices;
}

std::optional<juce::PluginDescription> AudioEngineManager::findDevice (const juce::String& identifier)
{
    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (d.createIdentifierString() == identifier)
            return d;

    return std::nullopt;
}

//==============================================================================
// Milestone 3  -  Recording & Monitoring
//==============================================================================

void AudioEngineManager::resetLoudness()
{
    loudnessAnalyser.requestReset();
    loudnessMeter.resetIntegrated();
    spectrumAnalyser.reset();
    correlationMeter.reset();
}

void AudioEngineManager::timerCallback()
{
    if (edit != nullptr)
    {
        loudnessMeter.update (loudnessAnalyser, isPlaying());
        spectrumAnalyser.update();
        correlationMeter.update();
    }

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
    if ((bool) edit->clickTrackEmphasiseBars == on) return;
    edit->clickTrackEmphasiseBars = on;
    broadcastChange();
}

bool AudioEngineManager::isMetronomeAccentEnabled() const
{
    return edit->clickTrackEmphasiseBars;
}

void AudioEngineManager::setCountInMode (int bars)
{
    using CI = te::Edit::CountIn;
    const auto oldMode = edit->getCountInMode();
    edit->setCountInMode (bars == 1 ? CI::oneBar : bars == 2 ? CI::twoBar : CI::none);
    if (edit->getCountInMode() != oldMode)
        broadcastChange();
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
    if (punchEnabled == on) return;
    punchEnabled = on;
    broadcastChange();
}

void AudioEngineManager::setLatencyCompensationEnabled (bool on)
{
    if (edit->isLatencyCompensationEnabled() == on) return;
    edit->setLatencyCompensationEnabled (on);
    broadcastChange();
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
    track->state.setProperty (IDs::trackInputDeviceIdx, waveDeviceIdx, nullptr);

    // If currently armed, re-arm with the new device selection.
    if (isTrackArmed (track))
        setTrackArmed (track, true);
    else
        broadcastChange();
}

int AudioEngineManager::getTrackInputDeviceIdx (te::Track* track) const
{
    if (track == nullptr) return -1;
    auto key = track->itemID.toString();
    if (inputDeviceMap.contains (key))
        return inputDeviceMap[key];

    return (int) track->state.getProperty (IDs::trackInputDeviceIdx, -1);
}

juce::StringArray AudioEngineManager::getMidiInputDeviceNames() const
{
    juce::StringArray names;
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumMidiInDevices(); ++i)
        if (auto d = dm.getMidiInDevice (i))
            names.add (d->getName());
    return names;
}

void AudioEngineManager::setTrackMidiInputDevice (te::Track* track, int midiDeviceIdx)
{
    if (track == nullptr) return;
    midiInputDeviceMap.set (track->itemID.toString(), midiDeviceIdx);
    track->state.setProperty (IDs::midiInputDevice, midiDeviceIdx, nullptr);

    // If currently armed, re-arm with the new MIDI device selection.
    if (isTrackArmed (track))
        setTrackArmed (track, true);
    else
        broadcastChange();
}

int AudioEngineManager::getTrackMidiInputDeviceIdx (te::Track* track) const
{
    if (track == nullptr) return -1;
    auto key = track->itemID.toString();
    if (midiInputDeviceMap.contains (key))
        return midiInputDeviceMap[key];

    return (int) track->state.getProperty (IDs::midiInputDevice, -1);
}

void AudioEngineManager::setTrackMonitorMode (te::Track* track, MonitorMode mode)
{
    if (track == nullptr) return;
    monitorModeMap.set (track->itemID.toString(), static_cast<int> (mode));
    track->state.setProperty (IDs::monitorMode, static_cast<int> (mode), nullptr);

    // If the track is currently armed, push the new mode through immediately
    // so the user hears the change without having to disarm/re-arm.
    if (isTrackArmed (track))
        setTrackArmed (track, true);
    else
        broadcastChange();
}

AudioEngineManager::MonitorMode AudioEngineManager::getTrackMonitorMode (te::Track* track) const
{
    if (track == nullptr) return MonitorMode::Auto;
    auto key = track->itemID.toString();
    const auto rawMode = monitorModeMap.contains (key)
        ? monitorModeMap[key]
        : (int) track->state.getProperty (IDs::monitorMode, static_cast<int> (MonitorMode::Auto));

    auto mode = static_cast<MonitorMode> (rawMode);
    if (mode != MonitorMode::Auto && mode != MonitorMode::On && mode != MonitorMode::Off)
        return MonitorMode::Auto;

    return mode;
}

AudioEngineManager::BufferInfo AudioEngineManager::getBufferInfo() const
{
    auto& dm = engine.getDeviceManager();
    BufferInfo bi;
    bi.sampleRate = dm.getSampleRate();
    bi.blockSize  = dm.getBlockSize();
    bi.cpuUsage   = dm.getCpuUsage();
    bi.oneBlockMs = dm.getBlockSizeMs();
    bi.driverIoMs = dm.getRecordAdjustmentMs();
    return bi;
}

void AudioEngineManager::applyRecommendedAudioDefaults()
{
    // True "reset" so we recover from any sticky state (e.g. WASAPI Exclusive
    // having locked the sample rate). Wipe the persisted device XML, ask
    // JUCE to re-pick its system defaults, then apply our safe overlay.
    auto& adm = engine.getDeviceManager().deviceManager;

    appProperties.getUserSettings()->removeValue ("audioDeviceState");
    appProperties.getUserSettings()->saveIfNeeded();

    adm.initialiseWithDefaultDevices (2, 2);

    applyBeginnerFriendlyDefaults (engine);
}
