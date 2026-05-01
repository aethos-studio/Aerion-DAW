#include "AIManager.h"

namespace te = tracktion;

AIManager::AIManager(te::Edit& e) : juce::Thread("AIManager"), edit(e)
{
}

AIManager::~AIManager()
{
    stopThread(2000);
}

void AIManager::convertAudioToMidi(te::WaveAudioClip& audioClip)
{
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
    juce::MessageManager::callAsync([this, notes]()
    {
        // Find or create MIDI track
        if (auto* track = te::getAudioTracks(edit)[0]) // Just use first track for demo
        {
            // Insert MIDI clip
            // track->insertMIDIClip("AI Transcription", { 0.0, 4.0 }, &notes);
        }
    });
}
