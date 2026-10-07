#pragma once

// The window that offers a newer release: what changed, then download, check
// and install. The download runs on its own thread; a timer on the message
// thread follows it, so closing the window mid-download simply cancels it.

#include "../Views/ViewShared.h"
#include "UpdateChecker.h"

class UpdateDialog : public juce::Component,
                     private juce::Timer
{
public:
    /** The user chose Skip This Version. */
    std::function<void (const Updates::Release&)> onSkip;

    /** The installer is downloaded and checked. installNow: the user wants
        Aerion to close now, rather than install whenever it next closes. */
    std::function<void (const juce::File& installer, bool installNow)> onInstallerReady;

    explicit UpdateDialog (Updates::Release r)
        : release (std::move (r))
    {
        heading.setText ("Aerion DAW " + release.version.toString() + " is available", juce::dontSendNotification);
        heading.setFont (Theme::uiSize (17.0f).boldened());
        heading.setColour (juce::Label::textColourId, Theme::textMain);
        addAndMakeVisible (heading);

        subheading.setText ("You have " + Updates::currentVersion().toString()
                                + (release.preRelease ? ". This is a pre-release." : "."),
                            juce::dontSendNotification);
        subheading.setFont (Theme::uiSize (12.5f));
        subheading.setColour (juce::Label::textColourId, Theme::textMuted);
        addAndMakeVisible (subheading);

        notes.setMultiLine (true, true);
        notes.setReadOnly (true);
        notes.setScrollbarsShown (true);
        notes.setCaretVisible (false);
        notes.setFont (Theme::uiSize (12.5f));
        notes.setColour (juce::TextEditor::backgroundColourId, Theme::bgBase);
        notes.setColour (juce::TextEditor::textColourId,       Theme::textMain);
        notes.setColour (juce::TextEditor::outlineColourId,    Theme::border);
        notes.setText (plainNotes (release.notes).isNotEmpty() ? plainNotes (release.notes)
                                                               : juce::String ("No release notes."));
        addAndMakeVisible (notes);

        releasePage.setButtonText ("Open the release page");
        releasePage.setURL (juce::URL (release.pageUrl));
        releasePage.setFont (Theme::uiSize (12.0f), false, juce::Justification::centredLeft);
        releasePage.setColour (juce::HyperlinkButton::textColourId, Theme::accent);
        releasePage.setVisible (release.pageUrl.startsWithIgnoreCase ("https://"));
        addChildComponent (releasePage);

        status.setFont (Theme::uiSize (12.5f));
        status.setColour (juce::Label::textColourId, Theme::textMuted);
        addAndMakeVisible (status);
        addChildComponent (progressBar);

        for (auto* b : { &primary, &secondary, &tertiary })
            addAndMakeVisible (b);

        setSize (560, 480);
        showOffer();
    }

    ~UpdateDialog() override
    {
        stopTimer();
        if (worker != nullptr)
            worker->stopThread (10000);
    }

    void paint (juce::Graphics& g) override   { g.fillAll (Theme::bgPanel); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        heading.setBounds (r.removeFromTop (26));
        subheading.setBounds (r.removeFromTop (20));
        r.removeFromTop (10);

        auto footer = r.removeFromBottom (30);
        r.removeFromBottom (10);
        auto statusRow = r.removeFromBottom (22);
        r.removeFromBottom (6);
        releasePage.setBounds (r.removeFromBottom (20).withWidth (220));
        r.removeFromBottom (6);
        notes.setBounds (r);

        progressBar.setBounds (statusRow.removeFromRight (180).reduced (0, 2));
        status.setBounds (statusRow);

        constexpr int gap = 8;
        for (auto* b : { &primary, &secondary, &tertiary })
        {
            if (! b->isVisible())
                continue;
            const int w = juce::jmax (110, b->getBestWidthForHeight (footer.getHeight()) + 16);
            b->setBounds (footer.removeFromRight (w));
            footer.removeFromRight (gap);
        }
    }

    /** Opens the dialog in its own window. Returns it so the caller can tell
        whether one is already open. */
    static juce::Component::SafePointer<UpdateDialog> launch (Updates::Release release,
                                                              std::function<void (const Updates::Release&)> onSkip,
                                                              std::function<void (const juce::File&, bool)> onInstallerReady)
    {
        auto* dialog = new UpdateDialog (std::move (release));
        dialog->onSkip = std::move (onSkip);
        dialog->onInstallerReady = std::move (onInstallerReady);

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (dialog);
        o.dialogTitle                  = "Update Available";
        o.dialogBackgroundColour       = Theme::bgPanel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar            = true;
        o.resizable                    = false;
        o.launchAsync();
        return dialog;
    }

    /** Release notes are Markdown; show them as plain text. */
    static juce::String plainNotes (const juce::String& markdown)
    {
        juce::StringArray lines;
        for (auto line : juce::StringArray::fromLines (markdown))
        {
            line = line.trimEnd();
            if (line.startsWith ("#"))
                line = line.trimCharactersAtStart ("#").trimStart();
            if (line == "---")
                line = {};
            lines.add (line.replace ("**", "").replace ("`", ""));
        }
        return lines.joinIntoString ("\n").trim();
    }

private:
    class DownloadThread : public juce::Thread
    {
    public:
        DownloadThread (const Updates::Release& r, juce::File t)
            : juce::Thread ("Aerion update download"), release (r), target (std::move (t)) {}

        void run() override
        {
            error = Updates::download (release, target, [this] (double p)
            {
                progress = p;
                return ! threadShouldExit();
            });
            done = true;
        }

        const Updates::Release release;
        const juce::File target;
        std::atomic<double> progress { 0.0 };
        std::atomic<bool> done { false };
        juce::String error;   // read only once done is true
    };

    void setButtons (const juce::String& p, const juce::String& s, const juce::String& t)
    {
        primary.setButtonText (p);
        secondary.setButtonText (s);
        tertiary.setButtonText (t);
        primary.setVisible (p.isNotEmpty());
        secondary.setVisible (s.isNotEmpty());
        tertiary.setVisible (t.isNotEmpty());
        resized();
    }

    void showOffer (const juce::String& message = {})
    {
        status.setText (message.isNotEmpty() ? message
                                              : "Download size " + juce::File::descriptionOfSizeInBytes (release.assetSize) + ".",
                        juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, message.isNotEmpty() ? Theme::meterRed : Theme::textMuted);
        progressBar.setVisible (false);

        setButtons (message.isNotEmpty() ? "Try Again" : "Download and Install", "Skip This Version", "Later");
        primary.onClick   = [this] { startDownload(); };
        secondary.onClick = [this]
        {
            if (onSkip != nullptr)
                onSkip (release);
            close();
        };
        tertiary.onClick  = [this] { close(); };
    }

    void startDownload()
    {
        const auto target = Updates::getDownloadFolder().getChildFile (juce::File::createLegalFileName (release.assetName));
        worker = std::make_unique<DownloadThread> (release, target);
        worker->startThread();

        shownProgress = 0.0;
        progressBar.setVisible (true);
        status.setText ("Downloading " + release.assetName + "...", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, Theme::textMuted);

        setButtons ("Cancel", {}, {});
        primary.onClick = [this]
        {
            stopTimer();
            worker->stopThread (10000);
            worker.reset();
            showOffer();
        };
        startTimerHz (10);
    }

    void timerCallback() override
    {
        if (worker == nullptr)
            return stopTimer();

        shownProgress = worker->progress.load();

        if (! worker->done)
            return;

        stopTimer();
        worker->stopThread (1000);
        const auto error  = worker->error;
        const auto target = worker->target;
        worker.reset();

        if (error.isNotEmpty())
        {
            juce::Logger::writeToLog ("Updates: download of " + release.assetName + " failed: " + error);
            showOffer (error);
            return;
        }

        juce::Logger::writeToLog ("Updates: downloaded and checked " + target.getFullPathName());
        progressBar.setVisible (false);
        status.setText ("Ready to install. Aerion closes first, asking to save your project.", juce::dontSendNotification);

        setButtons ("Close Aerion and Install", "Install When Aerion Closes", {});
        primary.onClick   = [this, target] { finish (target, true); };
        secondary.onClick = [this, target] { finish (target, false); };
    }

    void finish (const juce::File& installer, bool installNow)
    {
        // Close first: closing deletes this component, so copy the callback.
        auto callback = onInstallerReady;
        close();
        if (callback != nullptr)
            callback (installer, installNow);
    }

    void close()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    Updates::Release release;

    juce::Label heading, subheading, status;
    juce::TextEditor notes;
    juce::HyperlinkButton releasePage;
    double shownProgress = 0.0;
    juce::ProgressBar progressBar { shownProgress };
    juce::TextButton primary, secondary, tertiary;

    std::unique_ptr<DownloadThread> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateDialog)
};
