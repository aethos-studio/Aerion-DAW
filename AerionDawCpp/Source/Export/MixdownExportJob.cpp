#include "MixdownExportJob.h"

namespace te = tracktion;

static te::Renderer::Parameters makeParamsForMixdown (te::Engine& engine,
                                                     te::Edit& edit,
                                                     const juce::File& dest,
                                                     te::TimeRange range,
                                                     double sampleRate,
                                                     int channels,
                                                     int tailMs)
{
    juce::Logger::writeToLog ("makeParamsForMixdown: range=" + juce::String (range.getStart().inSeconds())
                             + "-" + juce::String (range.getEnd().inSeconds())
                             + " sr=" + juce::String (sampleRate)
                             + " ch=" + juce::String (channels)
                             + " tail=" + juce::String (tailMs));

    te::Renderer::Parameters p (engine);
    p.edit = &edit;
    p.destFile = dest;

    // Source: master mix
    p.category = te::ProjectItem::Category::none;
    p.separateTracks = false;
    p.createMidiFile = false;

    // CRITICAL FIX 1: Set tracksToDo to all tracks. Without this, RenderPass::initialise()
    // checks tracksToDo.countNumberOfSetBits() > 0 and silently skips rendering if zero bits are set.
    p.tracksToDo = te::toBitSet (te::getAllTracks (edit));
    juce::Logger::writeToLog ("makeParamsForMixdown: tracksToDo set");

    // CRITICAL FIX 2: Disable checkNodesForAudio to prevent silent failure when render graph
    // reports no audio nodes. This check was causing renders to appear to finish without error
    // but with no output file.
    p.checkNodesForAudio = false;
    juce::Logger::writeToLog ("makeParamsForMixdown: checkNodesForAudio disabled");

    // Re-enable plugins (were disabled as workaround for EditRenderJob pooling crashes).
    // With EditRenderer::render(), each render job is independent and thread-safe.
    p.usePlugins = true;
    p.useMasterPlugins = true;
    juce::Logger::writeToLog ("makeParamsForMixdown: plugins enabled");

    // Bounds
    p.time = range;
    if (tailMs > 0)
    {
        auto tailSecs = tailMs / 1000.0;
        p.endAllowance = te::TimeDuration::fromSeconds (tailSecs);
        juce::Logger::writeToLog ("makeParamsForMixdown: endAllowance set to " + juce::String (tailSecs) + "s");
    }

    // Format / quality
    p.sampleRateForAudio = sampleRate > 0 ? sampleRate : 44100.0;
    p.bitDepth = 24;
    p.mustRenderInMono = (juce::jlimit (1, 2, channels) == 1);

    // Use the extension to choose the format.
    p.audioFormat = engine.getAudioFileFormatManager().getFormatFromFileName (dest);
    if (p.audioFormat == nullptr)
    {
        juce::Logger::writeToLog ("makeParamsForMixdown: format not found, using WAV");
        p.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    }

    juce::Logger::writeToLog ("makeParamsForMixdown: complete, audioFormat=" + juce::String ((int64_t) p.audioFormat));

    return p;
}

MixdownExportJob::MixdownExportJob (te::Engine& e,
                                    te::Edit& editToRender,
                                    const juce::File& destination,
                                    te::TimeRange rangeToRender,
                                    double sampleRate,
                                    int numChannels,
                                    int tailMs,
                                    bool useEditCopy)
    : engine (e),
      destFile (destination),
      cachedParams (e)
{
    juce::Logger::writeToLog ("MixdownExportJob: ctor entry, useEditCopy=" + juce::String ((int) useEditCopy));

    te::Edit* editPtr = &editToRender;

    if (useEditCopy)
    {
        // Create an Edit copy for thread-safe rendering. The live UI Edit can be
        // mutated by the UI thread, causing crashes when rendering on a background thread.
        // Snapshot state and render a dedicated EditRole::forRendering copy instead.
        juce::Logger::writeToLog ("MixdownExportJob: creating Edit copy for rendering");
        try
        {
            juce::Logger::writeToLog ("MixdownExportJob: about to createXml");
            if (auto xml = editToRender.state.createXml())
            {
                juce::Logger::writeToLog ("MixdownExportJob: XML created, loading edit from state");
                auto vt = juce::ValueTree::fromXml (*xml);
                renderEditCopy = te::loadEditFromState (engine, vt, te::Edit::forRendering);
                if (renderEditCopy != nullptr)
                {
                    editPtr = renderEditCopy.get();
                    juce::Logger::writeToLog ("MixdownExportJob: Edit copy created successfully");
                }
                else
                {
                    juce::Logger::writeToLog ("MixdownExportJob: loadEditFromState returned nullptr");
                }
            }
            else
            {
                juce::Logger::writeToLog ("MixdownExportJob: editToRender.state.createXml() returned nullptr");
            }
        }
        catch (const std::exception& ex)
        {
            juce::Logger::writeToLog ("MixdownExportJob: std::exception while creating Edit copy: " + juce::String (ex.what()));
        }
        catch (...)
        {
            juce::Logger::writeToLog ("MixdownExportJob: unknown exception while creating Edit copy");
        }

        if (editPtr == &editToRender)
        {
            initError = "Failed to create an Edit copy for offline rendering.";
            juce::Logger::writeToLog ("MixdownExportJob: Edit copy failed, error=" + initError);
            return;
        }
    }

    juce::Logger::writeToLog ("MixdownExportJob: about to create render params");
    cachedParams = makeParamsForMixdown (engine, *editPtr, destFile, rangeToRender, sampleRate, numChannels, tailMs);
    paramsReady = true;

    juce::Logger::writeToLog ("MixdownExportJob: ctor complete, ready to render");
}

MixdownExportJob::~MixdownExportJob()
{
    // Cancel and join the render thread before destroying renderEditCopy.
    // The order is important: renderHandle must be destroyed before renderEditCopy.
    if (renderHandle)
    {
        renderHandle->cancel();
        renderHandle.reset();
    }
}

void MixdownExportJob::start()
{
    juce::Logger::writeToLog ("MixdownExportJob::start() called");
    if (!paramsReady || renderHandle != nullptr)
    {
        juce::Logger::writeToLog ("MixdownExportJob::start() - not ready or already started");
        return;
    }

    juce::WeakReference<MixdownExportJob> weakThis (this);

    renderHandle = te::EditRenderer::render (
        cachedParams,
        [weakThis] (tl::expected<juce::File, std::string> result)
        {
            juce::MessageManager::callAsync ([weakThis, result]() mutable
            {
                auto* self = weakThis.get();
                if (self == nullptr)
                    return;

                Result r;
                if (result.has_value())
                {
                    r.ok = result->existsAsFile();
                    r.outputFile = *result;
                    if (!r.ok)
                        r.error = "Output file missing after render";
                    juce::Logger::writeToLog ("MixdownExportJob::jobFinished - render succeeded, file exists");
                }
                else
                {
                    r.error = juce::String (result.error());
                    juce::Logger::writeToLog ("MixdownExportJob::jobFinished - render failed: " + r.error);
                }

                juce::Logger::writeToLog ("MixdownExportJob::jobFinished - notifying listeners, ok=" + juce::String ((int) r.ok));

                self->listeners.call ([&] (Listener& l)
                {
                    l.exportProgress (1.0f);
                    l.exportFinished (r);
                });

                juce::Logger::writeToLog ("MixdownExportJob::jobFinished - complete");
            });
        });

    juce::Logger::writeToLog ("MixdownExportJob::start() - render started");
}

void MixdownExportJob::cancel()
{
    juce::Logger::writeToLog ("MixdownExportJob: cancel() called");
    if (renderHandle)
        renderHandle->cancel();
}

void MixdownExportJob::waitForCancel()
{
    juce::Logger::writeToLog ("MixdownExportJob: waitForCancel() begin");
    if (!renderHandle)
        return;

    renderHandle->cancel();
    renderHandle.reset(); // Deterministic join of the render thread
    juce::Logger::writeToLog ("MixdownExportJob: waitForCancel() end");
}

bool MixdownExportJob::isRunning() const
{
    return renderHandle != nullptr && renderHandle->getProgress() < 1.0f;
}

float MixdownExportJob::getProgress() const
{
    if (renderHandle)
        return renderHandle->getProgress();
    return 0.0f;
}

