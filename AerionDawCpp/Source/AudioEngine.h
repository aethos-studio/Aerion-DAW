#pragma once
#include <JuceHeader.h>
#include <map>

class AudioEngineManager : public juce::ChangeListener,
                           private juce::Timer
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void editStateChanged() = 0;
    };

    void addListener (Listener* l)      { listeners.add (l); }
    void removeListener (Listener* l)   { listeners.remove (l); }

    AudioEngineManager();
    ~AudioEngineManager() override;

    tracktion::Engine& getEngine() { return engine; }
    tracktion::Edit& getEdit()     { return *edit; }

    void play()  { edit->getTransport().play(false); }
    void stop()  { edit->getTransport().stop(false, false); }
    void record() { edit->getTransport().record(false); }
    
    bool isPlaying() const   { return edit->getTransport().isPlaying(); }
    bool isRecording() const { return edit->getTransport().isRecording(); }
    
    double getTransportPosition() const 
    { 
        if (auto* pc = edit->getTransport().getCurrentPlaybackContext())
            return pc->getPosition().inSeconds();
        return edit->getTransport().getPosition().inSeconds(); 
    }
    void setTransportPosition (double seconds) { edit->getTransport().setPosition(tracktion::TimePosition::fromSeconds(seconds)); }

    void setTempo (double bpm);
    void setTimeSig (int numerator, int denominator);

    juce::Array<tracktion::AudioTrack*> getAudioTracks();
    juce::Array<tracktion::Track*>      getTopLevelTracks();
    juce::Array<tracktion::Track*>      getMixerTracks();
    
    void syncFolderRouting();

    /** Tracktion treats a folder as a submix when it has real insert plugins (Volume+Meter vs VCA-only). */
    bool isFolderSubmix (tracktion::FolderTrack* folder) const;
    void setFolderSubmix (tracktion::FolderTrack* folder, bool asSubmix);
    
    // Routing & Sends (Phase 4)
    void addSendToNewBus (tracktion::Track* track);
    void setAuxSendLevelDb (tracktion::AuxSendPlugin* plugin, float db);
    float getAuxSendLevelDb (tracktion::AuxSendPlugin* plugin);
    
    // Snapshots (Phase 5)
    void saveMixSnapshot (const juce::String& name);
    void recallMixSnapshot (const juce::String& name);
    juce::StringArray getMixSnapshotNames();

    tracktion::AudioTrack*  addAudioTrack();
    tracktion::FolderTrack* addFolderTrack();
    tracktion::FolderTrack* groupTracks (const juce::Array<tracktion::Track*>& tracks);
    void deleteTrack (tracktion::Track*);

    // Move track after `preceding` within `folder` (nullptr = top level).
    // preceding = nullptr means insert as first child/top-level item.
    void moveTrackAfter (tracktion::Track* t, tracktion::Track* preceding, tracktion::FolderTrack* folder = nullptr);

    // Record-arm state (per track itemID).
    void setTrackArmed (tracktion::Track*, bool);
    bool isTrackArmed (tracktion::Track*) const;
    float getTrackPeak (tracktion::Track*);
    float getTrackMaxPeak (tracktion::Track*);
    void  clearTrackMaxPeak (tracktion::Track*);

    void toggleTrackMute (tracktion::Track*);
    void toggleTrackSolo (tracktion::Track*);

    // Undo / Redo
    void undo() { edit->getUndoManager().undo(); }
    void redo() { edit->getUndoManager().redo(); }
    juce::UndoManager& getUndoManager() { return edit->getUndoManager(); }

    // Audio Import
    void importAudioFile (const juce::File& file);

    // Insert a clip onto an existing track at a specific time position.
    tracktion::WaveAudioClip* insertAudioClipOnTrack (tracktion::AudioTrack* track,
                                                       const juce::File& file,
                                                       double startTimeSecs);

    // Create a new track and insert the file at the given time position.
    tracktion::AudioTrack* importAudioFileAtPosition (const juce::File& file,
                                                      double startTimeSecs);

    // Waveform Rendering
    tracktion::SmartThumbnail& getThumbnailForClip (tracktion::WaveAudioClip& clip, juce::Component& componentToRepaint);

    // Persistence
    void saveProject (const juce::File& file);
    void loadProject (const juce::File& file);
    void createNewProject();

    // Plugins
    void scanPlugins();
    void cancelScan();
    bool isScanningPlugins() const { return scanInFlight.load(); }
    bool shouldRunStartupScan();
    bool areAudioDevicesConnected() const { return audioDevicesConnected; }
    void deletePluginFromBrowserList (const juce::PluginDescription& desc);

    // Boot-time scan progress callbacks. Posted on the message thread.
    std::function<void(juce::String pluginName)> onScanProgress;
    std::function<void()>                        onScanFinished;

    // Internal: invoked on the message thread by the scan worker once it returns.
    // Public so the scan thread (defined in the .cpp anon namespace) can dispatch here
    // via a juce::WeakReference without needing access to private members.
    void notifyScanFinished (bool finishedNormally);

    tracktion::Plugin::Ptr addPluginToTrack (tracktion::Track* track, const juce::PluginDescription& desc);
    void removePlugin (tracktion::Plugin* plugin);
    tracktion::Plugin* getPluginFor (juce::ValueTree& v);
    
    // Plugin Presets (Phase 6)
    int  getPluginNumPrograms (tracktion::Plugin* plugin);
    juce::String getPluginProgramName (tracktion::Plugin* plugin, int index);
    void setPluginProgram (tracktion::Plugin* plugin, int index);

    tracktion::Track* getMasterTrack() { return edit->getMasterTrack(); }

    // Transport status helpers
    double getTempoAtPosition (double seconds);
    juce::String getTimeSigAtPosition (double seconds);
    juce::String getBarsBeatsString (double seconds);

    // Metronome
    bool isMetronomeEnabled() const;
    void setMetronomeEnabled (bool);
    void toggleMetronome();
    float getMetronomeVolumeDb() const;
    void setMetronomeVolumeDb (float db);
    void setMetronomeAccentEnabled (bool on);
    bool isMetronomeAccentEnabled() const;

    // Count-in / Pre-roll
    void setCountInMode (int bars);   // 0=off, 1=1bar, 2=2bars
    int  getCountInBars() const;

    // Loop
    bool isLooping() const  { return (bool) edit->getTransport().looping; }
    void toggleLoop()       { auto& tp = edit->getTransport(); tp.looping.setValue (! (bool) tp.looping, nullptr); }

    // Punch in/out (uses loop range as punch region)
    void setPunchEnabled (bool on);
    bool isPunchEnabled() const { return punchEnabled; }

    // Plugin Delay Compensation
    void setLatencyCompensationEnabled (bool on);
    bool isLatencyCompensationEnabled() const;

    // Multi-channel input routing
    juce::StringArray getInputDeviceNames() const;
    void setTrackInputDevice (tracktion::Track* track, int waveDeviceIdx);
    int  getTrackInputDeviceIdx (tracktion::Track* track) const;

    // MIDI controller routing per track. Index refers to the entry order in
    // getMidiInputDeviceNames(). -1 means "all enabled MIDI controllers" (the
    // Tracktion default).
    juce::StringArray getMidiInputDeviceNames() const;
    void setTrackMidiInputDevice (tracktion::Track* track, int midiDeviceIdx);
    int  getTrackMidiInputDeviceIdx (tracktion::Track* track) const;

    // Per-track input monitoring override.
    //   Auto: monitor when armed, suspend during clip playback (Tracktion's
    //         "automatic" mode - matches the previous always-Auto behaviour).
    //   On  : always monitor while armed (input echo, useful for soft-synth
    //         tracking through plugin FX).
    //   Off : never monitor through the FX chain (e.g. relying on hardware
    //         monitoring from the audio interface).
    enum class MonitorMode { Auto = 0, On = 1, Off = 2 };
    void        setTrackMonitorMode (tracktion::Track* track, MonitorMode mode);
    MonitorMode getTrackMonitorMode (tracktion::Track* track) const;

    // Buffer / CPU info for status bar (ms values from Tracktion DeviceManager)
    struct BufferInfo
    {
        double sampleRate   = 0;
        int    blockSize    = 0;
        float  cpuUsage     = 0;
        double oneBlockMs   = 0; // one audio callback period
        double driverIoMs   = 0; // reported input+output latency (align / monitoring feel)
    };
    BufferInfo getBufferInfo() const;

    /** Apply newcomer-friendly audio defaults (best available backend, 48 kHz,
        ~256 sample buffer). Surfaced from the Audio Settings dialog so users
        can recover from a misconfigured `audioDeviceState` without editing
        files. Called automatically on first run. */
    void applyRecommendedAudioDefaults();

    // Pan helpers (range -1..1). No-op for tracks without a VolumeAndPanPlugin (e.g. Master).
    void  setTrackPan (tracktion::Track* track, float pan);
    float getTrackPan (tracktion::Track* track);

    // Automation
    enum class AutomationParamKind { Volume, Pan };
    tracktion::AutomatableParameter* getAutomationParam (tracktion::Track* track, AutomationParamKind kind);

    // Volume Helpers
    static constexpr float kMinVolumeDb  = -60.0f;
    static constexpr float kMaxVolumeDb  =  30.0f;
    static constexpr float kFaderRangeDb = kMaxVolumeDb - kMinVolumeDb;

    void  setTrackVolumeDb (tracktion::Track* track, float db);
    float getTrackVolumeDb (tracktion::Track* track);
    void  ensureVolumeRange (tracktion::Track* track);

    // Filter, Phase and Mono (Phase 1)
    float getTrackHPF (tracktion::Track* track);
    void  setTrackHPF (tracktion::Track* track, float freq);
    float getTrackLPF (tracktion::Track* track);
    void  setTrackLPF (tracktion::Track* track, float freq);
    
    bool  getTrackPhase (tracktion::Track* track);
    void  setTrackPhase (tracktion::Track* track, bool phaseInverted);
    
    bool  getTrackMono (tracktion::Track* track);
    void  setTrackMono (tracktion::Track* track, bool mono);

    // DAW-style dB fader mapping:
    // - long throw for -60..0 dB (fine control where it matters)
    // - medium throw for 0..+6 dB (so +6 isn't glued to 0)
    // - short throw for +6..+30 dB (quick access above that)
    static float getFaderPosFromDb (float db)
    {
        constexpr float splitDb0   = 0.0f;
        constexpr float splitDb6   = 6.0f;
        constexpr float splitPos0  = 0.85f; // where 0 dB lands on the fader travel
        constexpr float splitPos6  = 0.95f; // where +6 dB lands on the fader travel

        db = juce::jlimit (kMinVolumeDb, kMaxVolumeDb, db);

        if (db <= splitDb0)
        {
            const float t = (db - kMinVolumeDb) / (splitDb0 - kMinVolumeDb); // -60..0 -> 0..1
            return juce::jlimit (0.0f, splitPos0, t * splitPos0);
        }

        if (db <= splitDb6)
        {
            const float t = (db - splitDb0) / (splitDb6 - splitDb0); // 0..+6 -> 0..1
            return juce::jlimit (splitPos0, splitPos6, splitPos0 + t * (splitPos6 - splitPos0));
        }

        const float t = (db - splitDb6) / (kMaxVolumeDb - splitDb6); // +6..+30 -> 0..1
        return juce::jlimit (splitPos6, 1.0f, splitPos6 + t * (1.0f - splitPos6));
    }

    static float getDbFromFaderPos (float pos)
    {
        constexpr float splitDb0   = 0.0f;
        constexpr float splitDb6   = 6.0f;
        constexpr float splitPos0  = 0.85f;
        constexpr float splitPos6  = 0.95f;

        pos = juce::jlimit (0.0f, 1.0f, pos);

        if (pos <= splitPos0)
        {
            const float t = (splitPos0 > 0.0f) ? (pos / splitPos0) : 0.0f; // 0..splitPos0 -> 0..1
            return kMinVolumeDb + t * (splitDb0 - kMinVolumeDb);           // -60..0
        }

        if (pos <= splitPos6)
        {
            const float t = (splitPos6 > splitPos0) ? ((pos - splitPos0) / (splitPos6 - splitPos0)) : 0.0f; // splitPos0..splitPos6 -> 0..1
            return splitDb0 + t * (splitDb6 - splitDb0);                                                       // 0..+6
        }

        const float t = (1.0f > splitPos6) ? ((pos - splitPos6) / (1.0f - splitPos6)) : 0.0f; // splitPos6..1 -> 0..1
        return splitDb6 + t * (kMaxVolumeDb - splitDb6);                                       // +6..+30
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

private:
    static std::unique_ptr<tracktion::UIBehaviour> makeUIBehaviour();

    tracktion::Engine engine { ProjectInfo::projectName, makeUIBehaviour(), nullptr };
    std::unique_ptr<tracktion::Edit> edit;

    juce::ApplicationProperties appProperties;

    juce::ListenerList<Listener> listeners;

    struct EditListener : public tracktion::SelectableListener
    {
        EditListener (AudioEngineManager& m) : owner (m) {}
        void selectableObjectChanged (tracktion::Selectable*) override { owner.broadcastChange(); }
        void selectableObjectAboutToBeDeleted (tracktion::Selectable*) override {}
        AudioEngineManager& owner;
    };
    std::unique_ptr<EditListener> editListener;

    void broadcastChange();

    juce::HashMap<juce::String, bool> armedTracks;
    juce::HashMap<juce::String, int>  inputDeviceMap;       // trackID -> waveDeviceIdx
    juce::HashMap<juce::String, int>  midiInputDeviceMap;   // trackID -> midiDeviceIdx
    juce::HashMap<juce::String, int>  monitorModeMap;       // trackID -> MonitorMode int

    bool punchEnabled = false;
    std::map<uint64_t, std::unique_ptr<tracktion::SmartThumbnail>> thumbnails;

    std::atomic<bool> scanInFlight { false };
    std::unique_ptr<juce::Thread> scanThread;

    // Per-track meter subscription. Holds a Plugin::Ptr to keep the
    // LevelMeterPlugin (and its measurer) alive for the lifetime of the
    // Client we registered with it  -  so removeClient in the destructor is
    // always safe, even if the plugin has been removed from the track or
    // the Edit has been torn down.
    struct TrackMeter
    {
        juce::ReferenceCountedObjectPtr<tracktion::LevelMeterPlugin> plugin;
        tracktion::LevelMeasurer::Client client;
        float lastPeakDb = -100.0f;
        float maxPeakDb = -100.0f;
        juce::uint32 lastUpdateMs = 0;

        ~TrackMeter()
        {
            if (plugin != nullptr)
                plugin->measurer.removeClient (client);
        }
    };
    std::map<juce::String, std::unique_ptr<TrackMeter>> trackMeters;

    std::atomic<bool> closing { false };
    bool audioDevicesConnected = false;

    void setupInitialEdit();

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioEngineManager)
};
