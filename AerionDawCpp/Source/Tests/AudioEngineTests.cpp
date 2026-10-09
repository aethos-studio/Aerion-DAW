#include <JuceHeader.h>
#include "../AudioEngine.h"
#include "../ProjectData.h"
#include "../ScannerProcesses.h"

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
// Freeze render itself stays out of this suite (it is async and needs a device).
// Unfreeze placement is covered: a frozen snapshot must restore trim, offset
// and fades, not the whole source file.
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

        beginTest ("unfreeze restores trimmed audio from the clip snapshot");
        {
            const auto file = writeTestWav ("aerion_unfreeze_state.wav", 2.0);
            expect (file.existsAsFile());

            auto* track = engine.addAudioTrack();
            auto* clip = engine.insertAudioClipOnTrack (track, file, 1.25);
            expect (clip != nullptr);

            if (clip != nullptr)
            {
                clip->setName ("phrase");
                clip->setLength (tracktion::TimeDuration::fromSeconds (0.5), true);
                clip->setOffset (tracktion::TimeDuration::fromSeconds (0.35));
                clip->setFadeIn (tracktion::TimeDuration::fromSeconds (0.04));
                clip->setFadeOut (tracktion::TimeDuration::fromSeconds (0.06));

                const double start = clip->getPosition().getStart().inSeconds();
                const double length = clip->getPosition().getLength().inSeconds();
                const double offset = clip->getPosition().getOffset().inSeconds();

                juce::ValueTree preFreeze (IDs::preFreeze);
                auto cs = clipSnapshot (*clip, true);
                // Numeric fields deliberately disagree with the clip state. Restore
                // must prefer the state, otherwise a trimmed phrase comes back as a
                // different edit (or the whole source file).
                cs.setProperty ("offset", 9.0, nullptr);
                cs.setProperty ("fadeIn", 0.0, nullptr);
                cs.setProperty ("fadeOut", 0.0, nullptr);
                preFreeze.addChild (cs, -1, nullptr);

                clip->removeFromParent();
                installFrozenSnapshot (*track, preFreeze);
                engine.unfreezeTrack (track);

                expectEquals (track->getClips().size(), 1);
                if (track->getClips().size() == 1)
                {
                    auto* restored = dynamic_cast<tracktion::WaveAudioClip*> (track->getClips()[0]);
                    expect (restored != nullptr);
                    if (restored != nullptr)
                    {
                        expectEquals (restored->getName(), juce::String ("phrase"));
                        expectWithinAbsoluteError (restored->getPosition().getStart().inSeconds(), start, 0.02);
                        expectWithinAbsoluteError (restored->getPosition().getLength().inSeconds(), length, 0.02);
                        expectWithinAbsoluteError (restored->getPosition().getOffset().inSeconds(), offset, 0.02);
                        expectWithinAbsoluteError (restored->getFadeIn().inSeconds(), 0.04, 0.01);
                        expectWithinAbsoluteError (restored->getFadeOut().inSeconds(), 0.06, 0.01);
                        expect (restored->getPosition().getLength().inSeconds() < 1.0);
                    }
                }
            }

            engine.deleteTrack (track);
            file.deleteFile();
        }

        beginTest ("unfreeze of a legacy snapshot keeps length, offset and fades");
        {
            const auto file = writeTestWav ("aerion_unfreeze_legacy.wav", 2.0);
            expect (file.existsAsFile());

            auto* track = engine.addAudioTrack();
            auto* clip = engine.insertAudioClipOnTrack (track, file, 0.8);
            expect (clip != nullptr);

            if (clip != nullptr)
            {
                clip->setLength (tracktion::TimeDuration::fromSeconds (0.4), true);
                clip->setOffset (tracktion::TimeDuration::fromSeconds (0.22));
                clip->setFadeIn (tracktion::TimeDuration::fromSeconds (0.03));
                clip->setFadeOut (tracktion::TimeDuration::fromSeconds (0.05));

                const double start = clip->getPosition().getStart().inSeconds();
                auto cs = clipSnapshot (*clip, false);
                clip->removeFromParent();

                juce::ValueTree preFreeze (IDs::preFreeze);
                preFreeze.addChild (cs, -1, nullptr);
                installFrozenSnapshot (*track, preFreeze);
                engine.unfreezeTrack (track);

                expectEquals (track->getClips().size(), 1);
                if (track->getClips().size() == 1)
                {
                    auto* restored = dynamic_cast<tracktion::WaveAudioClip*> (track->getClips()[0]);
                    expect (restored != nullptr);
                    if (restored != nullptr)
                    {
                        expectWithinAbsoluteError (restored->getPosition().getStart().inSeconds(), start, 0.02);
                        expectWithinAbsoluteError (restored->getPosition().getLength().inSeconds(), 0.4, 0.02);
                        expectWithinAbsoluteError (restored->getPosition().getOffset().inSeconds(), 0.22, 0.02);
                        expectWithinAbsoluteError (restored->getFadeIn().inSeconds(), 0.03, 0.01);
                        expectWithinAbsoluteError (restored->getFadeOut().inSeconds(), 0.05, 0.01);
                    }
                }
            }

            engine.deleteTrack (track);
            file.deleteFile();
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

        beginTest ("a track's lane height is saved with the project");
        {
            auto* track = engine.addAudioTrack();
            expect (track != nullptr);
            track->state.setProperty (IDs::laneHeight, 140, nullptr);

            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("aerion_lane_height_test.aerion");
            engine.saveProject (file);
            engine.createNewProject();
            engine.loadProject (file);

            auto tracks = engine.getAudioTracks();
            expectEquals (tracks.size(), 1);
            if (! tracks.isEmpty())
                expectEquals ((int) tracks.getFirst()->state.getProperty (IDs::laneHeight, 0), 140);

            engine.createNewProject();
            file.deleteFile();
        }

        // With an automation curve the parameter follows the curve on every block,
        // so a fader move used to snap back at once. It now overrides the curve.
        beginTest ("moving an automated fader overrides its automation until re-enabled");
        {
            auto* track = engine.addAudioTrack();
            auto* vol = engine.getAutomationParam (track, AudioEngineManager::AutomationParamKind::Volume);
            expect (vol != nullptr);

            const float curveValue = std::exp ((-20.0f - 6.0f) / 20.0f); // -20 dB
            auto& curve = vol->getCurve();
            curve.addPoint (tracktion::TimePosition::fromSeconds (0.0), curveValue, 0.0f);
            curve.addPoint (tracktion::TimePosition::fromSeconds (10.0), curveValue, 0.0f);
            vol->updateToFollowCurve (tracktion::TimePosition::fromSeconds (1.0));
            expectWithinAbsoluteError (engine.getTrackVolumeDb (track), -20.0f, 0.1f);
            expect (! engine.isTrackAutomationOverridden (track));

            engine.setTrackVolumeDb (track, -6.0f);
            vol->updateToFollowCurve (tracktion::TimePosition::fromSeconds (1.0)); // what playback does next
            expectWithinAbsoluteError (engine.getTrackVolumeDb (track), -6.0f, 0.1f);
            expect (engine.isTrackAutomationOverridden (track));
            expectEquals (curve.getNumPoints(), 2, "the curve itself is kept");

            engine.reenableTrackAutomation (track);
            expect (! engine.isTrackAutomationOverridden (track));
            vol->updateToFollowCurve (tracktion::TimePosition::fromSeconds (1.0));
            expectWithinAbsoluteError (engine.getTrackVolumeDb (track), -20.0f, 0.1f);

            engine.deleteTrack (track);
        }

        beginTest ("a new MIDI track has no instrument until one is added");
        {
            auto* track = engine.addMidiTrack();
            expect (track != nullptr);
            expect (AudioEngineManager::getInsertDevices (track).isEmpty());
            expect (track->getLevelMeterPlugin() != nullptr);
            engine.deleteTrack (track);
        }

        // Plugins hand their settings to the Edit only when it is flushed (external
        // plugins call getStateInformation there). A plugin's window position takes
        // the same route, so it stands in for state changed inside a plugin's UI.
        beginTest ("saving a project flushes plugin state into the file");
        {
            auto* track = engine.addAudioTrack();
            expect (track != nullptr);

            auto plugin = engine.getEdit().getPluginCache()
                              .createNewPlugin (tracktion::ReverbPlugin::xmlTypeName, {});
            expect (plugin != nullptr);
            track->pluginList.insertPlugin (plugin, 0, nullptr);
            plugin->windowState->lastWindowBounds = juce::Rectangle<int> (123, 45, 300, 200);
            plugin = nullptr; // must not outlive its Edit

            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("aerion_plugin_flush_test.aerion");
            engine.saveProject (file);
            engine.createNewProject();
            engine.loadProject (file);

            int windowX = -1;
            if (auto tracks = engine.getAudioTracks(); ! tracks.isEmpty())
                for (auto* p : tracks.getFirst()->pluginList)
                    if (dynamic_cast<tracktion::ReverbPlugin*> (p) != nullptr)
                        windowX = p->state.getProperty (tracktion::IDs::windowX, -1);

            expectEquals (windowX, 123);

            engine.createNewProject();
            file.deleteFile();
        }

        // The master's inserts used to be unreachable once added.
        beginTest ("a plugin on the master can be removed");
        {
            auto* master = engine.getEdit().getMasterTrack();
            auto plugin = engine.getEdit().getPluginCache()
                              .createNewPlugin (tracktion::ReverbPlugin::xmlTypeName, {});
            master->pluginList.insertPlugin (plugin, master->pluginList.size(), nullptr);
            expect (master->pluginList.indexOf (plugin.get()) >= 0);

            engine.removePlugin (plugin.get());
            expect (master->pluginList.indexOf (plugin.get()) < 0);
            plugin = nullptr;
        }

        beginTest ("clip gain, reverse and normalise survive save and reload");
        {
            const auto file = writeTestWav ("aerion_clip_processing.wav", 1.0, 0.5f);
            auto* track = engine.addAudioTrack();
            auto* clip = engine.insertAudioClipOnTrack (track, file, 0.0);
            expect (clip != nullptr);

            if (clip != nullptr)
            {
                expectWithinAbsoluteError (engine.measureClipPeak (*clip), 0.5f, 0.01f);
                expect (engine.normaliseClip (*clip));
                const float expectedDb = AudioEngineManager::kNormaliseTargetDb - juce::Decibels::gainToDecibels (0.5f);
                expectWithinAbsoluteError (clip->getGainDB(), expectedDb, 0.1f);

                clip->setIsReversed (true);

                auto project = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("aerion_clip_processing.aerion");
                engine.saveProject (project);
                engine.createNewProject();
                engine.loadProject (project);

                auto* reloaded = engine.getAudioTracks().isEmpty() ? nullptr
                               : dynamic_cast<tracktion::WaveAudioClip*> (engine.getAudioTracks().getFirst()->getClips().getFirst());
                expect (reloaded != nullptr);
                if (reloaded != nullptr)
                {
                    expectWithinAbsoluteError (reloaded->getGainDB(), expectedDb, 0.1f);
                    expect (reloaded->getIsReversed());
                }

                engine.createNewProject();
                project.deleteFile();
            }
            else
            {
                engine.deleteTrack (track);
            }
            file.deleteFile();
        }

        beginTest ("normalising a silent clip changes nothing");
        {
            const auto file = writeTestWav ("aerion_clip_silent.wav", 0.5);
            auto* track = engine.addAudioTrack();
            if (auto* clip = engine.insertAudioClipOnTrack (track, file, 0.0))
            {
                expect (! engine.normaliseClip (*clip));
                expectWithinAbsoluteError (clip->getGainDB(), 0.0f, 0.001f);
            }
            engine.deleteTrack (track);
            file.deleteFile();
        }

        beginTest ("a sidechain source is offered, set and saved with the project");
        {
            auto* target = engine.addAudioTrack();
            auto* source = engine.addAudioTrack();
            source->setName ("Kick");

            auto plugin = engine.getEdit().getPluginCache()
                              .createNewPlugin (tracktion::ReverbPlugin::xmlTypeName, {});
            target->pluginList.insertPlugin (plugin, 0, nullptr);

            auto candidates = engine.getSidechainSourceCandidates (*plugin);
            expect (candidates.contains (source));
            expect (! candidates.contains (target));

            engine.setPluginSidechainSource (*plugin, source);
            const auto sourceID = source->itemID;
            expect (plugin->getSidechainSourceID() == sourceID);
            plugin = nullptr;

            auto project = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("aerion_sidechain_test.aerion");
            engine.saveProject (project);
            engine.createNewProject();
            engine.loadProject (project);

            bool found = false;
            for (auto* t : engine.getAudioTracks())
                for (auto* p : t->pluginList)
                    if (dynamic_cast<tracktion::ReverbPlugin*> (p) != nullptr)
                        found = p->getSidechainSourceID() == sourceID;
            expect (found);

            if (auto* p = engine.getAudioTracks().isEmpty() ? nullptr : engine.getAudioTracks().getFirst()->pluginList.getPluginsOfType<tracktion::ReverbPlugin>().getFirst())
            {
                engine.setPluginSidechainSource (*p, nullptr);
                expect (! p->getSidechainSourceID().isValid());
            }

            engine.createNewProject();
            project.deleteFile();
        }

        beginTest ("a template keeps tracks and plugins, and drops clips unless asked");
        {
            const auto wav = writeTestWav ("aerion_template_clip.wav", 0.5, 0.25f);
            auto* track = engine.addAudioTrack();
            track->setName ("Vocals");
            engine.insertAudioClipOnTrack (track, wav, 0.0);

            const juce::String name = "Aerion smoke test template " + juce::String (juce::Random::getSystemRandom().nextInt());
            const auto withClips = engine.saveProjectAsTemplate (name, nullptr, true);
            expect (withClips.existsAsFile());
            expect (engine.getProjectTemplates().contains (withClips));

            engine.createNewProject();
            engine.loadProject (withClips);
            if (! engine.getAudioTracks().isEmpty())
                expectEquals (engine.getAudioTracks().getFirst()->getClips().size(), 1);

            // Same name again replaces it, this time without the clips.
            const auto withoutClips = engine.saveProjectAsTemplate (name, nullptr, false);
            expect (withoutClips == withClips);
            engine.createNewProject();
            engine.loadProject (withoutClips);
            expectEquals (engine.getAudioTracks().size(), 1);
            if (! engine.getAudioTracks().isEmpty())
            {
                expectEquals (engine.getAudioTracks().getFirst()->getName(), juce::String ("Vocals"));
                expect (engine.getAudioTracks().getFirst()->getClips().isEmpty());
            }

            expect (engine.saveProjectAsTemplate ("   ", nullptr, false) == juce::File());

            engine.createNewProject();
            withoutClips.deleteFile();
            wav.deleteFile();
        }

        beginTest ("a track template keeps plugins and sends and inserts with new IDs");
        {
            const auto wav = writeTestWav ("aerion_track_template_clip.wav", 0.5, 0.25f);
            auto* track = engine.addAudioTrack();
            track->setName ("Lead Vox");
            track->pluginList.insertPlugin (engine.getEdit().getPluginCache()
                                                .createNewPlugin (tracktion::ReverbPlugin::xmlTypeName, {}), 0, nullptr);
            engine.addSendToNewBus (track);
            engine.insertAudioClipOnTrack (track, wav, 0.0);

            // A sidechain from a track outside the template is dropped.
            auto* kick = engine.addAudioTrack();
            if (auto* reverb = track->pluginList.getPluginsOfType<tracktion::ReverbPlugin>().getFirst())
                engine.setPluginSidechainSource (*reverb, kick);

            const juce::String name = "Aerion smoke test track template " + juce::String (juce::Random::getSystemRandom().nextInt());
            const auto file = engine.saveTrackAsTemplate (track, name, false);
            expect (file.existsAsFile());
            expect (engine.getTrackTemplates().contains (file));
            expect (engine.saveTrackAsTemplate (track, "  ", false) == juce::File());
            expect (engine.saveTrackAsTemplate (engine.getMasterTrack(), name, false) == juce::File());

            engine.createNewProject();
            auto* first  = engine.insertTrackTemplate (file);
            auto* second = engine.insertTrackTemplate (file);
            expect (first != nullptr && second != nullptr);

            if (first != nullptr && second != nullptr)
            {
                expect (first->itemID != second->itemID);
                expectEquals (first->getName(), juce::String ("Lead Vox"));

                auto* at = dynamic_cast<tracktion::AudioTrack*> (first);
                expect (at != nullptr && at->getClips().isEmpty());

                auto reverbs = first->pluginList.getPluginsOfType<tracktion::ReverbPlugin>();
                expectEquals (reverbs.size(), 1);
                if (! reverbs.isEmpty())
                    expect (! reverbs.getFirst()->getSidechainSourceID().isValid());
                expect (! first->pluginList.getPluginsOfType<tracktion::AuxSendPlugin>().isEmpty());

                auto otherReverbs = second->pluginList.getPluginsOfType<tracktion::ReverbPlugin>();
                if (! reverbs.isEmpty() && ! otherReverbs.isEmpty())
                    expect (reverbs.getFirst()->itemID != otherReverbs.getFirst()->itemID);
            }

            engine.createNewProject();
            file.deleteFile();
            wav.deleteFile();
        }

        beginTest ("a folder track template brings its sub-tracks");
        {
            auto* a = engine.addAudioTrack();
            auto* b = engine.addAudioTrack();
            a->setName ("Kick");
            b->setName ("Snare");
            auto* folder = engine.groupTracks ({ a, b });
            expect (folder != nullptr);
            if (folder != nullptr)
                folder->setName ("Drums");

            const juce::String name = "Aerion smoke test folder template " + juce::String (juce::Random::getSystemRandom().nextInt());
            const auto file = engine.saveTrackAsTemplate (folder, name, false);
            expect (file.existsAsFile());

            engine.createNewProject();
            auto* inserted = dynamic_cast<tracktion::FolderTrack*> (engine.insertTrackTemplate (file));
            expect (inserted != nullptr);
            if (inserted != nullptr)
            {
                expectEquals (inserted->getName(), juce::String ("Drums"));
                expectEquals (inserted->getAllSubTracks (false).size(), 2);
            }

            engine.createNewProject();
            file.deleteFile();
        }

        beginTest ("MIDI learn marks the parameter, and a mapping drives it, saves and clears");
        {
            using Kind = AudioEngineManager::AutomationParamKind;
            auto* track = engine.addAudioTrack();
            auto* volume = engine.getAutomationParam (track, Kind::Volume);
            expect (volume != nullptr);

            if (volume != nullptr)
            {
                engine.learnParameter (*volume);
                expect (engine.isMidiLearnActive());
                expect (engine.getEdit().getParameterChangeHandler().getPendingParam (false).get() == volume);
                engine.setMidiLearnActive (false);
                expect (! engine.isMidiLearnActive());
                expect (! engine.getEdit().getParameterChangeHandler().isParameterPending());

                // What a learnt mapping looks like in the Edit: CC 7 on channel 1.
                constexpr int cc7 = 0x10000 + 7;
                juce::ValueTree mappings (tracktion::IDs::CONTROLLERMAPPINGS);
                mappings.appendChild (tracktion::createValueTree (tracktion::IDs::MAP,
                                                                  tracktion::IDs::id, cc7,
                                                                  tracktion::IDs::channel, 1,
                                                                  tracktion::IDs::param, volume->getFullName(),
                                                                  tracktion::IDs::pluginID, volume->getOwnerID().toString()), nullptr);
                engine.getEdit().state.appendChild (mappings, nullptr);
                engine.getEdit().getParameterControlMappings().loadFromEdit();

                expectEquals (engine.getMidiMappingText (volume), juce::String (juce::CharPointer_UTF8 ("CC 7 \xc2\xb7 Ch 1")));
                expectEquals (engine.getMidiMappings().size(), 1);

                engine.getEdit().getParameterControlMappings().sendChange (cc7, 0.25f, 1);
                expectWithinAbsoluteError (volume->getCurrentNormalisedValue(), 0.25f, 0.01f);

                const auto trackID = track->itemID;
                auto project = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("aerion_midi_learn_test.aerion");
                engine.saveProject (project);
                engine.createNewProject();
                engine.loadProject (project);

                auto* reloaded = engine.getAutomationParam (tracktion::findTrackForID (engine.getEdit(), trackID), Kind::Volume);
                expect (reloaded != nullptr && engine.getMidiMappingText (reloaded).isNotEmpty());

                engine.clearMidiMapping (reloaded);
                expect (engine.getMidiMappingText (reloaded).isEmpty());
                expect (engine.getMidiMappings().isEmpty());

                engine.createNewProject();
                project.deleteFile();
            }
        }

        beginTest ("default MIDI mappings are saved from a project and added to new projects and tracks");
        {
            using Kind = AudioEngineManager::AutomationParamKind;
            auto* settings = engine.getUserSettings();
            expect (settings != nullptr);

            // These tests share the user's settings file: keep what is there.
            auto previous = settings->getXmlValue (AudioEngineManager::kDefaultMidiMappingsKey);

            auto addMapping = [&engine] (tracktion::AutomatableParameter& p, int controller)
            {
                auto state = engine.getEdit().state.getOrCreateChildWithName (tracktion::IDs::CONTROLLERMAPPINGS, nullptr);
                state.appendChild (tracktion::createValueTree (tracktion::IDs::MAP,
                                                               tracktion::IDs::id, controller,
                                                               tracktion::IDs::channel, 1,
                                                               tracktion::IDs::param, p.getFullName(),
                                                               tracktion::IDs::pluginID, p.getOwnerID().toString()), nullptr);
                engine.getEdit().getParameterControlMappings().loadFromEdit();
            };

            engine.createNewProject();
            auto* first = engine.addAudioTrack();
            auto* masterVolume = engine.getAutomationParam (engine.getMasterTrack(), Kind::Volume);
            auto* trackPan     = engine.getAutomationParam (first, Kind::Pan);
            expect (masterVolume != nullptr && trackPan != nullptr);

            if (masterVolume != nullptr && trackPan != nullptr)
            {
                addMapping (*masterVolume, 0x10000 + 7);
                addMapping (*trackPan, 0x10000 + 10);
                // A plugin parameter is per project only.
                auto reverb = engine.getEdit().getPluginCache().createNewPlugin (tracktion::ReverbPlugin::xmlTypeName, {});
                first->pluginList.insertPlugin (reverb, 0, nullptr);
                if (auto p = reverb->getAutomatableParameter (0))
                    addMapping (*p, 0x10000 + 20);

                const auto saved = engine.saveMidiMappingsAsDefault();
                expectEquals (saved.saved, 2);
                expectEquals (saved.skipped, 1);
                expectEquals (engine.getNumDefaultMidiMappings(), 2);
                reverb = nullptr;

                // A new project gets the master mapping, its first new track the pan mapping.
                engine.createNewProject();
                expect (engine.getMidiMappingText (engine.getAutomationParam (engine.getMasterTrack(), Kind::Volume)).isNotEmpty());
                auto* newTrack = engine.addAudioTrack();
                expect (engine.getMidiMappingText (engine.getAutomationParam (newTrack, Kind::Pan)).isNotEmpty());
                expect (engine.getMidiMappingText (engine.getAutomationParam (newTrack, Kind::Volume)).isEmpty());
                // The second track has no default.
                auto* second = engine.addAudioTrack();
                expect (engine.getMidiMappingText (engine.getAutomationParam (second, Kind::Pan)).isEmpty());

                engine.clearDefaultMidiMappings();
                expectEquals (engine.getNumDefaultMidiMappings(), 0);
                engine.createNewProject();
                expect (engine.getMidiMappingText (engine.getAutomationParam (engine.getMasterTrack(), Kind::Volume)).isEmpty());
            }

            if (previous != nullptr)
                settings->setValue (AudioEngineManager::kDefaultMidiMappingsKey, previous.get());
            else
                settings->removeValue (AudioEngineManager::kDefaultMidiMappingsKey);
            settings->saveIfNeeded();
            engine.createNewProject();
        }

        // Quitting during a plugin scan ends the scanner child process, which also
        // lets a scan that waits on it stop.
        beginTest ("ending child processes ends a running child");
        {
            juce::ChildProcess child;
           #if JUCE_WINDOWS
            const juce::String program = "ping.exe";
            const bool started = child.start (juce::StringArray { program, "-n", "60", "127.0.0.1" }, 0);
           #else
            const juce::String program = "sleep";
            const bool started = child.start (juce::StringArray { "/bin/sleep", "60" }, 0);
           #endif
            expect (started);

            if (started)
            {
                expect (child.isRunning());
                ScannerProcesses::endChildren (program);
                expect (child.waitForProcessToFinish (5000), "the child was still running");
            }
        }

        // Unsaved-changes tracking (hasUnsavedEdits / markEditSaved) is checked by
        // `AerionBench --verify` instead: Tracktion attaches its change listener
        // on the message loop after an Edit is created, and running the loop here
        // would also run the deferred audio device init this runner must avoid.
    }

private:
    static juce::File writeTestWav (const juce::String& name, double seconds, float sineAmplitude = 0.0f)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        file.deleteFile();

        const double sampleRate = 44100.0;
        const int numSamples = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (1, numSamples);
        buffer.clear();
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (0, i, sineAmplitude * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sampleRate));

        juce::WavAudioFormat format;
        if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        {
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                    format.createWriterFor (out.get(), sampleRate, 1, 16, {}, 0)))
            {
                out.release();
                writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
            }
        }

        return file;
    }

    static juce::ValueTree clipSnapshot (tracktion::Clip& clip, bool includeState)
    {
        juce::ValueTree cs ("ClipState");
        if (includeState)
            cs.appendChild (clip.state.createCopy(), nullptr);

        if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (&clip))
        {
            cs.setProperty ("file", wave->getSourceFileReference().getFile().getFullPathName(), nullptr);
            cs.setProperty (IDs::preFreezeClipType, "audio", nullptr);
            cs.setProperty ("offset", wave->getPosition().getOffset().inSeconds(), nullptr);
            cs.setProperty ("fadeIn", wave->getFadeIn().inSeconds(), nullptr);
            cs.setProperty ("fadeOut", wave->getFadeOut().inSeconds(), nullptr);
        }

        cs.setProperty ("startBeat", clip.getStartBeat().inBeats(), nullptr);
        cs.setProperty ("endBeat", clip.getEndBeat().inBeats(), nullptr);
        return cs;
    }

    static void installFrozenSnapshot (tracktion::AudioTrack& track, const juce::ValueTree& preFreeze)
    {
        auto existing = track.state.getChildWithName (IDs::preFreeze);
        if (existing.isValid())
            track.state.removeChild (existing, nullptr);

        track.state.addChild (preFreeze.createCopy(), -1, nullptr);
        track.state.setProperty (IDs::frozen, true, nullptr);
    }
};

static AudioEngineTests audioEngineTests;
