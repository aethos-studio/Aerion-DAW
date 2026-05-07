#include "MixdownExportDialog.h"

namespace te = tracktion;

static juce::String boundsLabel (MixdownExportDialog::BoundsMode m)
{
    switch (m)
    {
        case MixdownExportDialog::BoundsMode::SelectionOrLoopOrFull: return "Selection/Loop/Full";
        case MixdownExportDialog::BoundsMode::Loop:                  return "Loop range";
        case MixdownExportDialog::BoundsMode::Full:                  return "Entire project";
        default:                                                     return "Entire project";
    }
}

static juce::File defaultExportDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("Aerion Projects")
                   .getChildFile ("Renders");
    dir.createDirectory();
    return dir;
}

static juce::File presetsFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("AerionDAW");
    dir.createDirectory();
    return dir.getChildFile ("render-presets.xml");
}

static juce::ValueTree loadPresets()
{
    auto f = presetsFile();
    if (f.existsAsFile())
        if (auto xml = juce::XmlDocument::parse (f))
            return juce::ValueTree::fromXml (*xml);
    return juce::ValueTree ("RenderPresets");
}

static void savePresets (const juce::ValueTree& vt)
{
    auto f = presetsFile();
    if (auto xml = vt.createXml())
        xml->writeTo (f);
}

MixdownExportDialog::MixdownExportDialog (te::Engine& e,
                                          te::Edit& ed,
                                          juce::String projectNameForWildcards,
                                          std::optional<te::TimeRange> selectedTimeRange)
    : engine (e),
      edit (ed),
      projectName (projectNameForWildcards.isNotEmpty() ? projectNameForWildcards : "My Song"),
      selectionRange (selectedTimeRange),
      progressBar (progressValue)
{
    juce::Logger::writeToLog ("MixdownExportDialog: ctor start");

    // Register standard audio formats for waveform analysis
    audioFormatManager.registerBasicFormats();

    title.setFont (juce::Font (16.0f, juce::Font::bold));
    addAndMakeVisible (title);
    juce::Logger::writeToLog ("MixdownExportDialog: title ok");
    title.setColour (juce::Label::textColourId, Theme::textMain);

    sourceBox.addItem ("Master mix", 1);
    sourceBox.setSelectedId (1);
    addAndMakeVisible (sourceBox);
    juce::Logger::writeToLog ("MixdownExportDialog: sourceBox ok");

    boundsBox.addItem (boundsLabel (BoundsMode::SelectionOrLoopOrFull), 1);
    boundsBox.addItem (boundsLabel (BoundsMode::Loop), 2);
    boundsBox.addItem (boundsLabel (BoundsMode::Full), 3);
    boundsBox.setSelectedId (3); // Default to "Entire project"
    addAndMakeVisible (boundsBox);
    juce::Logger::writeToLog ("MixdownExportDialog: boundsBox ok");

    tailToggle.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (tailToggle);
    tailToggle.setColour (juce::ToggleButton::textColourId, Theme::textMuted);

    tailMs.setRange (0, 20000, 50);
    tailMs.setValue (1000);
    tailMs.setTextValueSuffix (" ms");
    tailMs.setNumDecimalPlacesToDisplay (0);
    addAndMakeVisible (tailMs);
    juce::Logger::writeToLog ("MixdownExportDialog: tail ok");

    addAndMakeVisible (dirLabel);
    dirLabel.setColour (juce::Label::textColourId, Theme::textMuted);
    dirEdit.setText (defaultExportDir().getFullPathName());
    addAndMakeVisible (dirEdit);
    addAndMakeVisible (browseDir);
    juce::Logger::writeToLog ("MixdownExportDialog: dir ok");

    addAndMakeVisible (nameLabel);
    nameLabel.setColour (juce::Label::textColourId, Theme::textMuted);
    nameEdit.setText ("$project_$bounds_$date");
    addAndMakeVisible (nameEdit);
    addAndMakeVisible (wildcardsBtn);
    juce::Logger::writeToLog ("MixdownExportDialog: name ok");

    addAndMakeVisible (formatLabel);
    formatLabel.setColour (juce::Label::textColourId, Theme::textMuted);
    addAndMakeVisible (formatBox);

    addAndMakeVisible (srLabel);
    srLabel.setColour (juce::Label::textColourId, Theme::textMuted);
    sampleRateBox.addItem ("44100 Hz", 44100);
    sampleRateBox.addItem ("48000 Hz", 48000);
    sampleRateBox.addItem ("96000 Hz", 96000);
    sampleRateBox.setSelectedId (44100);
    addAndMakeVisible (sampleRateBox);
    juce::Logger::writeToLog ("MixdownExportDialog: sampleRate ok");

    addAndMakeVisible (chLabel);
    chLabel.setColour (juce::Label::textColourId, Theme::textMuted);
    channelsBox.addItem ("Stereo", 2);
    channelsBox.addItem ("Mono", 1);
    channelsBox.setSelectedId (2);
    addAndMakeVisible (channelsBox);
    juce::Logger::writeToLog ("MixdownExportDialog: channels ok");

    addAndMakeVisible (presetsBtn);
    addAndMakeVisible (renderBtn);
    addAndMakeVisible (cancelBtn);

    addAndMakeVisible (progressBar);
    juce::Logger::writeToLog ("MixdownExportDialog: progress ok");

    waveform = std::make_unique<WaveformPreviewComponent>();
    addAndMakeVisible (*waveform);
    juce::Logger::writeToLog ("MixdownExportDialog: waveform ok");

    // Only set size once the child pointers used in resized()
    // (e.g. waveform) have been created.
    setSize (820, 520);

    presetsBtn.onClick = [this] { showPresetsMenu(); };
    juce::Logger::writeToLog ("MixdownExportDialog: presets callback ok");

    // ASCII labels only (avoid mojibake on Windows codepages)
    browseDir.setButtonText ("Browse...");
    wildcardsBtn.setButtonText ("Wildcards");
    presetsBtn.setButtonText ("Presets");
    renderBtn.setButtonText ("Render 1 file");
    cancelBtn.setButtonText ("Cancel");

    // Text editor styling
    auto styleEditor = [] (juce::TextEditor& ed)
    {
        ed.setColour (juce::TextEditor::backgroundColourId, Theme::bgPanel.darker (0.25f));
        ed.setColour (juce::TextEditor::outlineColourId, Theme::border);
        ed.setColour (juce::TextEditor::textColourId, Theme::textMain);
        ed.setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.25f));
        ed.setColour (juce::TextEditor::highlightedTextColourId, Theme::textMain);
    };
    styleEditor (dirEdit);
    styleEditor (nameEdit);

    browseDir.onClick = [this]
    {
        auto fc = std::make_shared<juce::FileChooser> ("Choose render directory…",
                                                       juce::File (dirEdit.getText()));
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

        fc->launchAsync (flags, [this, fc] (const juce::FileChooser& chooser)
        {
            if (chooser.getResult().isDirectory())
                dirEdit.setText (chooser.getResult().getFullPathName(), true);
        });
    };
    juce::Logger::writeToLog ("MixdownExportDialog: browse callback ok");

    wildcardsBtn.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Insert $project");
        m.addItem (2, "Insert $date");
        m.addItem (3, "Insert $bounds");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (wildcardsBtn),
                         [this] (int r)
                         {
                             if (r == 1) nameEdit.insertTextAtCaret ("$project");
                             if (r == 2) nameEdit.insertTextAtCaret ("$date");
                             if (r == 3) nameEdit.insertTextAtCaret ("$bounds");
                         });
    };

    renderBtn.onClick = [this]
    {
        juce::Logger::writeToLog ("MixdownExportDialog: Render button clicked");
        // Defer export start to avoid re-entrancy from the button callback.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MixdownExportDialog> (this)]
        {
            if (safe != nullptr)
                safe->startExportRender();
        });
    };
    cancelBtn.onClick = [this]
    {
        cancelRenders();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };
    juce::Logger::writeToLog ("MixdownExportDialog: buttons ok");

    auto restartPreview = [this] { requestPreviewRestart(); };
    boundsBox.onChange = restartPreview;
    tailToggle.onClick = restartPreview;
    tailMs.onValueChange = restartPreview;
    formatBox.onChange = restartPreview;
    sampleRateBox.onChange = restartPreview;
    channelsBox.onChange = restartPreview;
    // Note: dirEdit and nameEdit don't affect the render, only the output file path,
    // so they don't trigger preview restart to avoid blocking during text input.
    juce::Logger::writeToLog ("MixdownExportDialog: change handlers ok");

    rebuildFormatList();
    juce::Logger::writeToLog ("MixdownExportDialog: format list ok");
    // Defer the true pre-render preview until after the window is shown.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MixdownExportDialog> (this)]
    {
        if (safe != nullptr)
            safe->requestPreviewRestart();
    });
    juce::Logger::writeToLog ("MixdownExportDialog: callAsync queued");
    startTimerHz (20);
    juce::Logger::writeToLog ("MixdownExportDialog: ctor end");
}

void MixdownExportDialog::requestPreviewRestart()
{
    // Debounce preview rebuilds so we don't thrash Tracktion's render manager.
    const auto token = ++previewRestartToken;
    juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<MixdownExportDialog> (this), token]
    {
        if (safe == nullptr) return;
        if (safe->previewRestartToken.load() != token) return; // superseded
        safe->startPreviewRender();
    });
}

void MixdownExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bgBase);

    // Subtle ASCII pattern overlay (branding, but not distracting)
    g.setColour (Theme::bgPanel.brighter (0.10f).withAlpha (0.10f));
    g.setFont (Theme::uiSize (10.0f));

    const juce::String row = "///---\\\\\\---///---\\\\\\---";
    auto area = getLocalBounds();
    for (int y = 36; y < area.getBottom(); y += 18)
    {
        // Offset every other row for a diagonal feel
        const int x = (y / 18) % 2 == 0 ? 14 : 28;
        g.drawText (row, x, y, area.getWidth(), 16, juce::Justification::left);
    }

    // Top separator
    g.setColour (Theme::border);
    g.drawLine (0.0f, 34.0f, (float) getWidth(), 34.0f, 1.0f);
}

MixdownExportDialog::~MixdownExportDialog()
{
    stopTimer();
    cancelRenders();
}

void MixdownExportDialog::resized()
{
    auto b = getLocalBounds().reduced (16);

    title.setBounds (b.removeFromTop (28));
    b.removeFromTop (6);

    auto topRow = b.removeFromTop (28);
    sourceBox.setBounds (topRow.removeFromLeft (220));
    topRow.removeFromLeft (10);
    boundsBox.setBounds (topRow.removeFromLeft (220));
    topRow.removeFromLeft (10);
    tailToggle.setBounds (topRow.removeFromLeft (60));
    tailMs.setBounds (topRow.removeFromLeft (200));
    presetsBtn.setBounds (topRow.removeFromRight (100));

    b.removeFromTop (10);

    auto outRow1 = b.removeFromTop (28);
    dirLabel.setBounds (outRow1.removeFromLeft (80));
    browseDir.setBounds (outRow1.removeFromRight (96));
    outRow1.removeFromRight (8);
    dirEdit.setBounds (outRow1);

    b.removeFromTop (8);

    auto outRow2 = b.removeFromTop (28);
    nameLabel.setBounds (outRow2.removeFromLeft (80));
    wildcardsBtn.setBounds (outRow2.removeFromRight (96));
    outRow2.removeFromRight (8);
    nameEdit.setBounds (outRow2);

    b.removeFromTop (10);

    auto optRow = b.removeFromTop (28);
    formatLabel.setBounds (optRow.removeFromLeft (80));
    formatBox.setBounds (optRow.removeFromLeft (180));
    optRow.removeFromLeft (10);
    srLabel.setBounds (optRow.removeFromLeft (90));
    sampleRateBox.setBounds (optRow.removeFromLeft (140));
    optRow.removeFromLeft (10);
    chLabel.setBounds (optRow.removeFromLeft (80));
    channelsBox.setBounds (optRow.removeFromLeft (120));

    b.removeFromTop (10);
    if (waveform != nullptr)
        waveform->setBounds (b.removeFromTop (260));
    else
        b.removeFromTop (260);

    b.removeFromTop (10);
    auto bottom = b.removeFromTop (30);
    progressBar.setBounds (bottom.removeFromLeft (b.getWidth() - 240));
    bottom.removeFromLeft (10);
    renderBtn.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (10);
    cancelBtn.setBounds (bottom.removeFromLeft (100));
}

void MixdownExportDialog::timerCallback()
{
    if (previewJob != nullptr)
    {
        progressValue = previewJob->getProgress();
        waveform->repaint();
    }
    else if (exportJob != nullptr)
    {
        progressValue = exportJob->getProgress();
        waveform->repaint();
    }
    else
    {
        progressValue = 0.0;
    }
}

void MixdownExportDialog::exportProgress (float progress01)
{
    progressValue = progress01;
}

void MixdownExportDialog::exportFinished (const MixdownExportJob::Result& r)
{
    exportJob.reset();

    if (! r.ok)
    {
        showThemedAlert ("Export Mixdown",
                        r.error.isNotEmpty() ? r.error : "Export failed.",
                        /*isError*/ true);
        return;
    }

    // Load the rendered file into the thumbnail for waveform display
    exportThumbnail.setSource (new juce::FileInputSource (r.outputFile));

    // Analyze for clipping and show in waveform
    waveform->setThumbnail (&exportThumbnail);
    waveform->analyzeForClipping (r.outputFile, audioFormatManager);

    showThemedAlert ("Export Mixdown",
                    "Rendered:\n" + r.outputFile.getFullPathName(),
                    /*isError*/ false);
}

void MixdownExportDialog::rebuildFormatList()
{
    formatBox.clear();

    const auto& fm = engine.getAudioFileFormatManager();
    const auto& formats = fm.getWriteFormats();

    int id = 1;
    for (auto* f : formats)
    {
        if (f == nullptr) continue;
        auto name = f->getFormatName();
        formatBox.addItem (name, id++);
    }

    if (formatBox.getNumItems() == 0)
    {
        formatBox.addItem ("WAV", 1);
    }

    formatBox.setSelectedItemIndex (0);
}

te::TimeRange MixdownExportDialog::computeBounds() const
{
    auto& transport = edit.getTransport();
    const auto full = te::TimeRange (te::TimePosition::fromSeconds (0.0),
                                     te::TimePosition::fromSeconds (edit.getLength().inSeconds()));

    auto loop = transport.getLoopRange();
    const bool loopEnabled = (bool) transport.looping;

    const auto mode = (BoundsMode) (boundsBox.getSelectedId() - 1);
    if (mode == BoundsMode::Loop)
        return loop;
    if (mode == BoundsMode::Full)
        return full;

    if (selectionRange.has_value())
        return *selectionRange;

    return loopEnabled ? loop : full;
}

juce::String MixdownExportDialog::applyWildcards (juce::String pattern) const
{
    pattern = pattern.replace ("$project", projectName);
    pattern = pattern.replace ("$date", juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S"));

    auto b = computeBounds();
    auto boundsStr = juce::String::formatted ("%.3fs-%.3fs", b.getStart().inSeconds(), b.getEnd().inSeconds());
    pattern = pattern.replace ("$bounds", boundsStr);

    return pattern;
}

juce::File MixdownExportDialog::expandedOutputFile() const
{
    auto dir = juce::File (dirEdit.getText());
    if (! dir.isDirectory())
        dir = defaultExportDir();

    auto base = applyWildcards (nameEdit.getText().trim());
    if (base.isEmpty())
        base = "render";

    juce::String ext = ".wav";
    if (auto* fmt = engine.getAudioFileFormatManager().getNamedFormat (formatBox.getText()))
    {
        auto exts = fmt->getFileExtensions();
        if (exts.size() > 0)
            ext = exts[0];
    }

    return dir.getChildFile (base).withFileExtension (ext);
}

void MixdownExportDialog::startPreviewRender()
{
    juce::Logger::writeToLog ("MixdownExportDialog: startPreviewRender begin");
    cancelPreview();

    auto bounds = computeBounds();
    auto outfile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aerion_preview_render_" + juce::String ((int) juce::Time::getMillisecondCounter()) + ".wav");

    int numChannels = channelsBox.getSelectedId();
    double sampleRate = sampleRateBox.getSelectedId();
    int tailMsVal = tailToggle.getToggleState() ? (int) tailMs.getValue() : 0;

    previewJob = std::make_unique<MixdownExportJob> (engine, edit, outfile, bounds, sampleRate, numChannels, tailMsVal, /*useEditCopy*/ false);
    previewFile = outfile;

    if (! previewJob->isValid())
    {
        juce::Logger::writeToLog ("MixdownExportDialog: preview job invalid: " + previewJob->getInitError());
        waveform->setThumbnail (nullptr);
        return;
    }

    previewJob->addListener (this);

    waveform->setThumbnail (nullptr);

    juce::Logger::writeToLog ("MixdownExportDialog: starting preview render");
    previewJob->start();
    juce::Logger::writeToLog ("MixdownExportDialog: startPreviewRender ok");
}

void MixdownExportDialog::startExportRender()
{
    juce::Logger::writeToLog ("MixdownExportDialog: startExportRender begin");
    cancelExport();

    // CRITICAL: Wait for preview to fully finish before starting export.
    // Tracktion pools render jobs by parameters; if preview is still running when we create
    // export with the same params but deleteEdit=true, Tracktion reuses the job and deletes
    // the Edit while preview is still rendering → crash.
    cancelPreview();

    juce::Logger::writeToLog ("MixdownExportDialog: preview fully cancelled, proceeding with export");

    // Rendering and realtime playback can fight over the playback context on some setups.
    // Stop transport and free the playback context before starting an offline render.
    try
    {
        edit.getTransport().stop (/*discardRecordings*/ false, /*clearDevices*/ true);
        tracktion::freePlaybackContextIfNotRecording (edit.getTransport());
        juce::Logger::writeToLog ("MixdownExportDialog: transport stopped + context freed");
    }
    catch (...)
    {
        juce::Logger::writeToLog ("MixdownExportDialog: transport stop/context free threw");
    }

    auto bounds = computeBounds();
    auto outfile = expandedOutputFile();
    juce::Logger::writeToLog ("MixdownExportDialog: export outfile=" + outfile.getFullPathName());

    outfile.getParentDirectory().createDirectory();

    int numChannels = channelsBox.getSelectedId();
    double sampleRate = sampleRateBox.getSelectedId();
    int tailMsVal = tailToggle.getToggleState() ? (int) tailMs.getValue() : 0;
    juce::Logger::writeToLog ("MixdownExportDialog: export sr=" + juce::String (sampleRate)
                             + " ch=" + juce::String (numChannels)
                             + " tailMs=" + juce::String (tailMsVal));

    exportJob = std::make_unique<MixdownExportJob> (engine, edit, outfile, bounds, sampleRate, numChannels, tailMsVal);
    exportJob->addListener (this);

    // Check job validity before starting
    if (! exportJob->isValid())
    {
        juce::Logger::writeToLog ("MixdownExportDialog: export job invalid: " + exportJob->getInitError());
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                               "Export Mixdown",
                                               exportJob->getInitError());
        waveform->setThumbnail (nullptr);
        exportJob.reset();
        return;
    }

    waveform->setThumbnail (nullptr);

    juce::Logger::writeToLog ("MixdownExportDialog: starting export render");
    exportJob->start();
    juce::Logger::writeToLog ("MixdownExportDialog: startExportRender end (valid="
                             + juce::String (exportJob->isValid() ? "true" : "false") + ")");
}

void MixdownExportDialog::cancelRenders()
{
    cancelPreview();
    cancelExport();
    waveform->setThumbnail (nullptr);
}

void MixdownExportDialog::cancelPreview()
{
    if (previewJob != nullptr)
    {
        juce::Logger::writeToLog ("MixdownExportDialog: cancelPreview() waiting for job to finish");
        previewJob->waitForCancel();
    }

    // Prevent waveform from referencing a thumbnail owned by previewJob after it's destroyed.
    waveform->setThumbnail (nullptr);
    previewJob.reset();
    juce::Logger::writeToLog ("MixdownExportDialog: cancelPreview() complete");
}

void MixdownExportDialog::cancelExport()
{
    if (exportJob != nullptr)
    {
        juce::Logger::writeToLog ("MixdownExportDialog: cancelExport() waiting for job to finish");
        exportJob->waitForCancel();
    }
    exportJob.reset();
    juce::Logger::writeToLog ("MixdownExportDialog: cancelExport() complete");
}

void MixdownExportDialog::showPresetsMenu()
{
    auto presets = loadPresets();

    juce::PopupMenu m;
    m.addItem (1, "Save preset…");
    if (presets.getNumChildren() > 0)
    {
        m.addSeparator();
        for (int i = 0; i < presets.getNumChildren(); ++i)
        {
            auto p = presets.getChild (i);
            auto name = p.getProperty ("name").toString();
            if (name.isNotEmpty())
                m.addItem (100 + i, "Load: " + name);
        }
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetsBtn),
                     [this, presets] (int r) mutable
                     {
                         if (r == 1)
                         {
                             auto* aw = new juce::AlertWindow ("Save preset", "Preset name:", juce::AlertWindow::NoIcon);
                             aw->addTextEditor ("name", "My Preset");
                             aw->addButton ("Save", 1);
                             aw->addButton ("Cancel", 0);

                             juce::Component::SafePointer<MixdownExportDialog> safe (this);
                             aw->enterModalState (true,
                                 juce::ModalCallbackFunction::create ([safe, aw, presets] (int result) mutable
                                 {
                                     std::unique_ptr<juce::AlertWindow> owned (aw);
                                     if (safe == nullptr) return;
                                     if (result != 1) return;

                                     auto name = owned->getTextEditorContents ("name").trim();
                                     if (name.isEmpty())
                                         return;

                                     auto& self = *safe;
                                     juce::ValueTree p ("Preset");
                                     p.setProperty ("name", name, nullptr);
                                     p.setProperty ("boundsId", self.boundsBox.getSelectedId(), nullptr);
                                     p.setProperty ("tailEnabled", self.tailToggle.getToggleState(), nullptr);
                                     p.setProperty ("tailMs", (int) self.tailMs.getValue(), nullptr);
                                     p.setProperty ("dir", self.dirEdit.getText(), nullptr);
                                     p.setProperty ("file", self.nameEdit.getText(), nullptr);
                                     p.setProperty ("format", self.formatBox.getText(), nullptr);
                                     p.setProperty ("sr", self.sampleRateBox.getSelectedId(), nullptr);
                                     p.setProperty ("ch", self.channelsBox.getSelectedId(), nullptr);

                                     for (int i = presets.getNumChildren(); --i >= 0;)
                                         if (presets.getChild (i).getProperty ("name").toString() == name)
                                             presets.removeChild (i, nullptr);

                                     presets.appendChild (p, nullptr);
                                     savePresets (presets);
                                 }),
                                 true);
                             return;
                         }

                         if (r >= 100)
                         {
                             const int idx = r - 100;
                             if (idx < 0 || idx >= presets.getNumChildren()) return;
                             auto p = presets.getChild (idx);

                             boundsBox.setSelectedId ((int) p.getProperty ("boundsId", 1), juce::sendNotification);
                             tailToggle.setToggleState ((bool) p.getProperty ("tailEnabled", true), juce::sendNotification);
                             tailMs.setValue ((int) p.getProperty ("tailMs", 1000), juce::sendNotification);
                             dirEdit.setText (p.getProperty ("dir").toString(), true);
                             nameEdit.setText (p.getProperty ("file").toString(), true);

                             // format is text-based; find item with matching name
                             auto fmtName = p.getProperty ("format").toString();
                             if (fmtName.isNotEmpty())
                                 formatBox.setText (fmtName, juce::sendNotification);

                             sampleRateBox.setSelectedId ((int) p.getProperty ("sr", 44100), juce::sendNotification);
                             channelsBox.setSelectedId ((int) p.getProperty ("ch", 2), juce::sendNotification);
                         }
                     });
}

void MixdownExportDialog::showThemedAlert (const juce::String& title, const juce::String& message, bool isError)
{
    class ThemedAlertWindow : public juce::DialogWindow
    {
    public:
        ThemedAlertWindow (const juce::String& t, const juce::String& m, bool error)
            : juce::DialogWindow (t, Theme::bgPanel, true)
        {
            setContentOwned (new ContentComponent (m, error), true);
            setResizable (false, false);
            setSize (420, 220);
            centreAroundComponent (nullptr, 420, 220);
        }

        void closeButtonPressed() override
        {
            exitModalState (0);
        }

    private:
        class ContentComponent : public juce::Component
        {
        public:
            ContentComponent (const juce::String& msg, bool error)
                : message (msg), isError (error)
            {
                okBtn.setColour (juce::TextButton::buttonColourId, Theme::surface);
                okBtn.setColour (juce::TextButton::textColourOffId, Theme::textMain);
                okBtn.onClick = [this] {
                    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                        window->closeButtonPressed();
                };
                addAndMakeVisible (okBtn);
            }

            void paint (juce::Graphics& g) override
            {
                g.fillAll (Theme::bgPanel);

                // Icon area (simple colored bar on left)
                auto iconColour = isError ? Theme::meterRed : Theme::accent;
                g.setColour (iconColour);
                g.fillRect (0, 0, 4, getHeight());

                // Message text
                g.setColour (Theme::textMain);
                auto font = Theme::uiSize (11.0f);
                g.setFont (font);
                auto textArea = getLocalBounds().reduced (16, 16);
                textArea.removeFromLeft (12); // Account for colored bar
                g.drawMultiLineText (message, textArea.getX(), textArea.getY() + 20, textArea.getWidth(),
                                   juce::Justification::topLeft);
            }

            void resized() override
            {
                auto b = getLocalBounds();
                auto btnArea = b.removeFromBottom (50).reduced (16, 8);
                okBtn.setBounds (btnArea.removeFromRight (100));
            }

            juce::TextButton okBtn { "OK" };
            juce::String message;
            bool isError;
        };
    };

    auto* alert = new ThemedAlertWindow (title, message, isError);
    alert->enterModalState (true, nullptr, true);
}
