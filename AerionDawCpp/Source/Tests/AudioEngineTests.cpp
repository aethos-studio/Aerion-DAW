#include <JuceHeader.h>
#include "../AudioEngine.h"
#include "../ProjectData.h"

namespace
{
    juce::File createScratchWav (const juce::String& name, double seconds)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile (name);
        file.deleteFile();

        const double sampleRate = 44100.0;
        const int numSamples = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (1, juce::jmax (1, numSamples));
        buffer.clear();

        juce::WavAudioFormat format;
        if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        {
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                    format.createWriterFor (out.get(), sampleRate, 1, 16, {}, 0)))
            {
                out.release();
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            }
        }

        return file;
    }
}

//==============================================================================
// AudioEngineManager smoke tests (Milestone 5).
//
// These exist to give the Milestone 5 performance work a safety net: the paint
// and listener refactors ahead touch track lookup, mute/solo cascading, and the
// tempo/time-signature maps, and until now nothing in CI would notice if any of
// that broke.
//
// Headless constraints:
//   - AudioEngineManager defers opening audio devices to a MessageManager
//     callback. The console runner never pumps the message loop, so no device
//     is ever opened here and these tests stay silent and deterministic.
//   - Constructing tracktion::Engine is expensive, so the whole suite shares a
//     single AudioEngineManager and cleans up the tracks it creates.
//
// Freeze *guards* (rejecting edits while a render is in flight) are still
// exercised in the UI, not here: those paths need an async MixdownExportJob.
// Unfreeze restore of trimmed audio *is* covered below, because that path is
// synchronous and is how freeze/unfreeze can silently discard clip placement.
//
// Expected noise: tearing the suite down logs two Debug-only JUCE assertions
// (juce_WeakReference.h and tracktion_SelectionManager.cpp). They fire inside
// tracktion::Edit::~Edit -> Selectable::notifyListenersOfDeletion, i.e. entirely
// within Tracktion Engine v3.2, and happen whenever an Edit is destroyed - the
// app hits them on shutdown too. They do not fail the run.
//==============================================================================

class AudioEngineTests final : public juce::UnitTest
{
public:
    AudioEngineTests() : juce::UnitTest ("AudioEngineManager", "Aerion") {}

    void runTest() override
    {
        AudioEngineManager engine;

        beginTest ("fresh session is empty and stopped");
        {
            expect (engine.getAudioTracks().isEmpty());
            expect (engine.getTopLevelTracks().isEmpty());
            expect (! engine.isPlaying());
            expect (! engine.isRecording());
            expect (engine.getEdit().getMasterTrack() != nullptr);
        }

        beginTest ("tracks can be added, listed and deleted");
        {
            auto* audio = engine.addAudioTrack();
            expect (audio != nullptr);
            expectEquals (engine.getAudioTracks().size(), 1);
            expectEquals (engine.getTopLevelTracks().size(), 1);

            auto* midi = engine.addMidiTrack();
            expect (midi != nullptr);
            expectEquals (engine.getTopLevelTracks().size(), 2);

            auto* folder = engine.addFolderTrack();
            expect (folder != nullptr);
            expectEquals (engine.getTopLevelTracks().size(), 3);

            engine.deleteTrack (folder);
            engine.deleteTrack (midi);
            engine.deleteTrack (audio);
            expect (engine.getTopLevelTracks().isEmpty());
            expect (engine.getAudioTracks().isEmpty());
        }

        beginTest ("volume and pan round-trip through the engine");
        {
            auto* track = engine.addAudioTrack();

            engine.setTrackVolumeDb (track, -6.0f);
            expectWithinAbsoluteError (engine.getTrackVolumeDb (track), -6.0f, 0.1f);

            engine.setTrackVolumeDb (track, 0.0f);
            expectWithinAbsoluteError (engine.getTrackVolumeDb (track), 0.0f, 0.1f);

            engine.setTrackPan (track, -1.0f);
            expectWithinAbsoluteError (engine.getTrackPan (track), -1.0f, 0.01f);

            engine.setTrackPan (track, 0.5f);
            expectWithinAbsoluteError (engine.getTrackPan (track), 0.5f, 0.01f);

            engine.deleteTrack (track);
        }

        beginTest ("mute, solo and record-arm toggle per track");
        {
            auto* track = engine.addAudioTrack();

            expect (! track->isMuted (false));
            engine.toggleTrackMute (track);
            expect (track->isMuted (false));
            engine.toggleTrackMute (track);
            expect (! track->isMuted (false));

            expect (! track->isSolo (false));
            engine.toggleTrackSolo (track);
            expect (track->isSolo (false));
            engine.toggleTrackSolo (track);
            expect (! track->isSolo (false));

            expect (! engine.isTrackArmed (track));
            engine.setTrackArmed (track, true);
            expect (engine.isTrackArmed (track));
            engine.setTrackArmed (track, false);
            expect (! engine.isTrackArmed (track));

            engine.deleteTrack (track);
        }

        beginTest ("monitor mode round-trips and defaults to Auto");
        {
            auto* track = engine.addAudioTrack();

            expect (engine.getTrackMonitorMode (track) == AudioEngineManager::MonitorMode::Auto);

            engine.setTrackMonitorMode (track, AudioEngineManager::MonitorMode::On);
            expect (engine.getTrackMonitorMode (track) == AudioEngineManager::MonitorMode::On);

            engine.setTrackMonitorMode (track, AudioEngineManager::MonitorMode::Off);
            expect (engine.getTrackMonitorMode (track) == AudioEngineManager::MonitorMode::Off);

            engine.deleteTrack (track);
        }

        beginTest ("folder mute cascades to child tracks");
        {
            auto* child = engine.addAudioTrack();

            juce::Array<tracktion::Track*> toGroup;
            toGroup.add (child);
            auto* folder = engine.groupTracks (toGroup);
            expect (folder != nullptr);

            // Aerion cascades by writing each child's own mute flag rather than
            // relying on Tracktion's mute-by-destination, so the child reads as
            // muted through isMuted(false) - which is what the Mixer and Timeline
            // actually draw. Pinning this keeps a refactor from silently
            // switching to destination semantics and changing what the UI shows.
            engine.toggleTrackMute (folder);
            expect (folder->isMuted (false));
            expect (child->isMuted (false));

            engine.toggleTrackMute (folder);
            expect (! folder->isMuted (false));
            expect (! child->isMuted (false));

            engine.deleteTrack (folder);
            engine.deleteTrack (child);
        }

        beginTest ("submix conversion toggles folder routing");
        {
            auto* folder = engine.addFolderTrack();
            expect (folder != nullptr);

            const bool wasSubmix = engine.isFolderSubmix (folder);

            engine.setFolderSubmix (folder, ! wasSubmix);
            expect (engine.isFolderSubmix (folder) == ! wasSubmix);

            engine.setFolderSubmix (folder, wasSubmix);
            expect (engine.isFolderSubmix (folder) == wasSubmix);

            engine.deleteTrack (folder);
        }

        beginTest ("tempo map inserts, edits and removes nodes");
        {
            const int initialTempos = engine.getNumTempos();
            expect (initialTempos >= 1); // the root tempo node always exists

            engine.insertTempoAtBeat (8.0, 140.0);
            expectEquals (engine.getNumTempos(), initialTempos + 1);

            const int inserted = engine.getNumTempos() - 1;
            expectWithinAbsoluteError (engine.getTempoBpm (inserted), 140.0, 0.01);
            expectWithinAbsoluteError (engine.getTempoStartBeat (inserted), 8.0, 0.01);

            engine.setTempoBpmAtIndex (inserted, 90.0);
            expectWithinAbsoluteError (engine.getTempoBpm (inserted), 90.0, 0.01);

            engine.moveTempoAtIndexToBeat (inserted, 16.0);
            expectWithinAbsoluteError (engine.getTempoStartBeat (inserted), 16.0, 0.01);

            engine.removeTempoAtIndex (inserted);
            expectEquals (engine.getNumTempos(), initialTempos);
        }

        beginTest ("time signature map inserts and removes changes");
        {
            const int initialSigs = engine.getNumTimeSigs();
            expect (initialSigs >= 1); // the root time signature always exists

            engine.setTimeSigAtBeat (8.0, 3, 4);
            expectEquals (engine.getNumTimeSigs(), initialSigs + 1);

            const int inserted = engine.getNumTimeSigs() - 1;
            expectEquals (engine.getTimeSigAtIndex (inserted), juce::String ("3/4"));

            engine.removeTimeSigAtIndex (inserted);
            expectEquals (engine.getNumTimeSigs(), initialSigs);
        }

        beginTest ("a fresh track is neither frozen nor freezing");
        {
            auto* track = engine.addAudioTrack();

            expect (! engine.isTrackFrozen (track));
            expect (! engine.isTrackFreezing (track));

            engine.deleteTrack (track);
        }

        beginTest ("unfreeze restores trimmed audio clip placement from full snapshot");
        {
            auto source = createScratchWav ("aerion_unfreeze_full.wav", 4.0);
            expect (source.existsAsFile());

            auto* track = engine.addAudioTrack();
            auto* clip = engine.insertAudioClipOnTrack (track, source, 1.0);
            expect (clip != nullptr);

            clip->setLength (tracktion::TimeDuration::fromSeconds (1.5), true);
            clip->setOffset (tracktion::TimeDuration::fromSeconds (0.5));
            clip->setFadeIn (tracktion::TimeDuration::fromSeconds (0.1));
            clip->setFadeOut (tracktion::TimeDuration::fromSeconds (0.2));

            juce::ValueTree preFreeze (IDs::preFreeze);
            juce::ValueTree cs ("ClipState");
            cs.setProperty ("file", source.getFullPathName(), nullptr);
            cs.setProperty (IDs::preFreezeClipType, "audio", nullptr);
            cs.setProperty ("startBeat", clip->getStartBeat().inBeats(), nullptr);
            cs.setProperty ("endBeat", clip->getEndBeat().inBeats(), nullptr);
            cs.setProperty ("offsetSeconds", clip->getPosition().getOffset().inSeconds(), nullptr);
            cs.setProperty ("fadeInSeconds", clip->getFadeIn().inSeconds(), nullptr);
            cs.setProperty ("fadeOutSeconds", clip->getFadeOut().inSeconds(), nullptr);
            cs.appendChild (clip->state.createCopy(), nullptr);
            preFreeze.addChild (cs, -1, nullptr);
            track->state.addChild (preFreeze, -1, nullptr);
            track->state.setProperty (IDs::frozen, true, nullptr);

            engine.unfreezeTrack (track);

            expect (! engine.isTrackFrozen (track));
            expectEquals (track->getClips().size(), 1);
            auto* restored = dynamic_cast<tracktion::WaveAudioClip*> (track->getClips()[0]);
            expect (restored != nullptr);
            expectWithinAbsoluteError (restored->getPosition().getStart().inSeconds(), 1.0, 0.02);
            expectWithinAbsoluteError (restored->getPosition().getLength().inSeconds(), 1.5, 0.02);
            expectWithinAbsoluteError (restored->getPosition().getOffset().inSeconds(), 0.5, 0.02);
            expectWithinAbsoluteError (restored->getFadeIn().inSeconds(), 0.1, 0.02);
            expectWithinAbsoluteError (restored->getFadeOut().inSeconds(), 0.2, 0.02);

            engine.deleteTrack (track);
            source.deleteFile();
        }

        beginTest ("unfreeze restores legacy audio snapshot trim and offset");
        {
            auto source = createScratchWav ("aerion_unfreeze_legacy.wav", 4.0);
            expect (source.existsAsFile());

            auto* track = engine.addAudioTrack();
            auto* clip = engine.insertAudioClipOnTrack (track, source, 2.0);
            expect (clip != nullptr);

            clip->setLength (tracktion::TimeDuration::fromSeconds (1.25), true);
            clip->setOffset (tracktion::TimeDuration::fromSeconds (0.75));

            // Pre-fix snapshots stored file + start/end beat only. The end beat
            // was written and then ignored, which re-inserted the whole file.
            juce::ValueTree preFreeze (IDs::preFreeze);
            juce::ValueTree cs ("ClipState");
            cs.setProperty ("file", source.getFullPathName(), nullptr);
            cs.setProperty (IDs::preFreezeClipType, "audio", nullptr);
            cs.setProperty ("startBeat", clip->getStartBeat().inBeats(), nullptr);
            cs.setProperty ("endBeat", clip->getEndBeat().inBeats(), nullptr);
            cs.setProperty ("offsetSeconds", clip->getPosition().getOffset().inSeconds(), nullptr);
            preFreeze.addChild (cs, -1, nullptr);
            track->state.addChild (preFreeze, -1, nullptr);
            track->state.setProperty (IDs::frozen, true, nullptr);

            engine.unfreezeTrack (track);

            expectEquals (track->getClips().size(), 1);
            auto* restored = dynamic_cast<tracktion::WaveAudioClip*> (track->getClips()[0]);
            expect (restored != nullptr);
            expectWithinAbsoluteError (restored->getPosition().getStart().inSeconds(), 2.0, 0.02);
            expectWithinAbsoluteError (restored->getPosition().getLength().inSeconds(), 1.25, 0.02);
            expectWithinAbsoluteError (restored->getPosition().getOffset().inSeconds(), 0.75, 0.02);

            engine.deleteTrack (track);
            source.deleteFile();
        }

        beginTest ("mix snapshots capture and restore fader state");
        {
            auto* track = engine.addAudioTrack();
            engine.setTrackVolumeDb (track, -3.0f);

            engine.saveMixSnapshot ("smoke");
            expect (engine.getMixSnapshotNames().contains ("smoke"));

            engine.setTrackVolumeDb (track, -18.0f);
            expectWithinAbsoluteError (engine.getTrackVolumeDb (track), -18.0f, 0.1f);

            engine.recallMixSnapshot ("smoke");
            expectWithinAbsoluteError (engine.getTrackVolumeDb (track), -3.0f, 0.1f);

            engine.deleteTrack (track);
        }

        beginTest ("transport option flags round-trip");
        {
            const bool loopWas = engine.isLooping();
            engine.toggleLoop();
            expect (engine.isLooping() == ! loopWas);
            engine.toggleLoop();
            expect (engine.isLooping() == loopWas);

            engine.setPunchEnabled (true);
            expect (engine.isPunchEnabled());
            engine.setPunchEnabled (false);
            expect (! engine.isPunchEnabled());

            engine.setLatencyCompensationEnabled (false);
            expect (! engine.isLatencyCompensationEnabled());
            engine.setLatencyCompensationEnabled (true);
            expect (engine.isLatencyCompensationEnabled());

            engine.setCountInMode (2);
            expectEquals (engine.getCountInBars(), 2);
            engine.setCountInMode (0);
            expectEquals (engine.getCountInBars(), 0);
        }

        beginTest ("metronome settings round-trip");
        {
            engine.setMetronomeEnabled (true);
            expect (engine.isMetronomeEnabled());
            engine.setMetronomeEnabled (false);
            expect (! engine.isMetronomeEnabled());

            engine.setMetronomeVolumeDb (-12.0f);
            expectWithinAbsoluteError (engine.getMetronomeVolumeDb(), -12.0f, 0.1f);

            engine.setMetronomeAccentEnabled (true);
            expect (engine.isMetronomeAccentEnabled());
            engine.setMetronomeAccentEnabled (false);
            expect (! engine.isMetronomeAccentEnabled());
        }

        beginTest ("createNewProject clears the session back to empty");
        {
            engine.addAudioTrack();
            engine.addAudioTrack();
            expect (! engine.getAudioTracks().isEmpty());

            engine.createNewProject();

            expect (engine.getAudioTracks().isEmpty());
            expect (engine.getTopLevelTracks().isEmpty());
            expect (engine.getEdit().getMasterTrack() != nullptr);
        }
    }
};

static AudioEngineTests audioEngineTests;
