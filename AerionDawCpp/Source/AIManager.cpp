#include "AIManager.h"

namespace te = tracktion;

AIManager::AIManager(te::Edit& e) : juce::Thread("AIManager"), edit(&e)
{
}

AIManager::~AIManager()
{
    stopThread(2000);
}

void AIManager::setEdit (te::Edit& e)
{
    stopThread (2000);
    edit = &e;
    clipToProcess = nullptr;
}

void AIManager::convertAudioToMidi(te::WaveAudioClip& audioClip)
{
    if (isThreadRunning())
        stopThread (2000);

    clipToProcess = &audioClip;
    startThread();
}

void AIManager::run()
{
    if (clipToProcess == nullptr) return;

    processTranscription();
}

void AIManager::processTranscription()
{
    // 1. Get audio data from clip
    // auto reader = clipToProcess->getAudioFile().createReader();
    
    // 2. Mock AI Inference Delay
    juce::Thread::sleep(2000); 

    // 3. Create MIDI notes (Mocked transcription)
    juce::MidiMessageSequence notes;
    notes.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0.0);
    notes.addEvent(juce::MidiMessage::noteOff(1, 60), 1.0);
    
    // 4. Update Tracktion Engine on Message Thread
    juce::WeakReference<AIManager> weakThis (this);
    juce::MessageManager::callAsync([weakThis, notes]()
    {
        if (weakThis == nullptr || weakThis->edit == nullptr)
            return;

        // Find or create MIDI track
        auto tracks = te::getAudioTracks (*weakThis->edit);
        if (! tracks.isEmpty())
        {
            // Insert MIDI clip
            // track->insertMIDIClip("AI Transcription", { 0.0, 4.0 }, &notes);
        }
    });
}
