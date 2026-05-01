#pragma once
#include <JuceHeader.h>
#include <map>

class AudioEngineManager
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
    ~AudioEngineManager();

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

    juce::Array<tracktion::AudioTrack*> getAudioTracks();
    juce::Array<tracktion::Track*>      getTopLevelTracks();

    tracktion::AudioTrack*  addAudioTrack();
    tracktion::FolderTrack* addFolderTrack();
    tracktion::FolderTrack* groupTracks (const juce::Array<tracktion::Track*>& tracks);
    void deleteTrack (tracktion::Track*);

    // Reorder a top-level track to a new index in the top-level list.
    void moveTrackToIndex (tracktion::Track*, int newIndex);

    // Record-arm state (per track itemID).
    void setTrackArmed (tracktion::Track*, bool);
    bool isTrackArmed (tracktion::Track*) const;

    void toggleTrackMute (tracktion::Track*);
    void toggleTrackSolo (tracktion::Track*);

    // Undo / Redo
    void undo() { edit->getUndoManager().undo(); }
    void redo() { edit->getUndoManager().redo(); }
    juce::UndoManager& getUndoManager() { return edit->getUndoManager(); }

    // Audio Import
    void importAudioFile (const juce::File& file);

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

    tracktion::Track* getMasterTrack() { return edit->getMasterTrack(); }

    // Transport status helpers
    double getTempoAtPosition (double seconds);
    juce::String getTimeSigAtPosition (double seconds);
    juce::String getBarsBeatsString (double seconds);

    // Pan helpers (range -1..1). No-op for tracks without a VolumeAndPanPlugin (e.g. Master).
    void  setTrackPan (tracktion::Track* track, float pan);
    float getTrackPan (tracktion::Track* track);

    // Automation
    enum class AutomationParamKind { Volume, Pan };
    tracktion::AutomatableParameter* getAutomationParam (tracktion::Track* track, AutomationParamKind kind);

private:
    static std::unique_ptr<tracktion::UIBehaviour> makeUIBehaviour();

    tracktion::Engine engine { ProjectInfo::projectName, makeUIBehaviour(), nullptr };
    std::unique_ptr<tracktion::Edit> edit;

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

    void setupInitialEdit();
};
