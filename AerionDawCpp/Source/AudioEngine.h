#pragma once
#include <JuceHeader.h>
#include <map>

class AudioEngineManager : public juce::ChangeListener
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
    bool isScanningPlugins() const { return scanInFlight.load(); }
    tracktion::Plugin::Ptr addPluginToTrack (tracktion::Track* track, const juce::PluginDescription& desc);
    void removePlugin (tracktion::Plugin* plugin);
    tracktion::Plugin* getPluginFor (juce::ValueTree& v);

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

    // Linear dB fader: position 0..1 maps uniformly to kMinVolumeDb..kMaxVolumeDb.
    static float getFaderPosFromDb (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - kMinVolumeDb) / kFaderRangeDb);
    }

    static float getDbFromFaderPos (float pos)
    {
        return kMinVolumeDb + juce::jlimit (0.0f, 1.0f, pos) * kFaderRangeDb;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

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
    std::map<uint64_t, std::unique_ptr<tracktion::SmartThumbnail>> thumbnails;

    std::atomic<bool> scanInFlight { false };
    std::unique_ptr<juce::Thread> scanThread;

    // Per-track meter subscription. Holds a Plugin::Ptr to keep the
    // LevelMeterPlugin (and its measurer) alive for the lifetime of the
    // Client we registered with it — so removeClient in the destructor is
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

    void setupInitialEdit();
};
