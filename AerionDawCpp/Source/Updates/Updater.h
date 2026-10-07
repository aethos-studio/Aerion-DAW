#pragma once

// Runs the update checks for MainComponent: a quiet one shortly after startup
// (unless Help › Check for Updates Automatically is off) and one on demand from
// Help › Check for Updates, which always reports what it found. The check runs
// on its own thread; a timer on the message thread picks up the answer.

#include "UpdateDialog.h"

class Updater : private juce::Timer
{
public:
    /** The user wants Aerion closed now so the downloaded installer can run. */
    std::function<void()> onInstallRequested;

    explicit Updater (juce::PropertiesFile* userSettings) : settings (userSettings) {}

    ~Updater() override
    {
        stopTimer();
        if (checker != nullptr)
            checker->stopThread (12000);
        if (dialog != nullptr)
            if (auto* window = dialog->findParentComponentOfClass<juce::DialogWindow>())
                delete window;
    }

    bool isAutoCheckEnabled() const
    {
        return settings == nullptr || settings->getBoolValue (Updates::autoCheckKey, true);
    }

    void setAutoCheckEnabled (bool shouldCheck)
    {
        if (settings != nullptr)
            settings->setValue (Updates::autoCheckKey, shouldCheck);
    }

    /** Clears out installers from earlier updates, then checks quietly after
        `delayMs`, if automatic checks are on. */
    void checkAfterStartup (int delayMs = 8000)
    {
        if (! Updates::getPendingInstaller().exists())
            Updates::getDownloadFolder().deleteRecursively();

        if (! isAutoCheckEnabled())
            return;

        startupCheckDueMs = juce::Time::getMillisecondCounter() + (juce::uint32) delayMs;
        startTimer (250);
    }

    /** Help › Check for Updates. */
    void checkNow()
    {
        if (dialog != nullptr)
        {
            dialog->getTopLevelComponent()->toFront (true);
            return;
        }

        reportResult = true;
        startCheck();
    }

private:
    class CheckThread : public juce::Thread
    {
    public:
        CheckThread() : juce::Thread ("Aerion update check") {}

        void run() override
        {
            result = Updates::checkForUpdate();
            done = true;
        }

        Updates::CheckResult result;   // read only once done is true
        std::atomic<bool> done { false };
    };

    void startCheck()
    {
        startupCheckDueMs = 0;
        if (checker == nullptr)
        {
            checker = std::make_unique<CheckThread>();
            checker->startThread();
        }
        startTimer (250);
    }

    void timerCallback() override
    {
        if (startupCheckDueMs != 0 && juce::Time::getMillisecondCounter() >= startupCheckDueMs)
            startCheck();

        if (checker == nullptr || ! checker->done)
            return;

        stopTimer();
        checker->stopThread (1000);
        const auto result = checker->result;
        checker.reset();

        handleResult (result, std::exchange (reportResult, false));
    }

    void handleResult (const Updates::CheckResult& result, bool report)
    {
        const auto current = Updates::currentVersion().toString();

        if (result.error.isNotEmpty())
        {
            juce::Logger::writeToLog ("Updates: check failed: " + result.error);
            if (report)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Check for Updates",
                                                        "Aerion could not check for updates.\n\n" + result.error);
            return;
        }

        if (! result.update.has_value())
        {
            juce::Logger::writeToLog ("Updates: " + current + " is up to date");
            if (report)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Check for Updates",
                                                        "Aerion DAW " + current + " is up to date.");
            return;
        }

        const auto& release = *result.update;
        const auto version = release.version.toString();
        juce::Logger::writeToLog ("Updates: " + version + " is available (" + release.assetName + ")");

        if (! report && settings != nullptr && settings->getValue (Updates::skippedVersionKey) == version)
            return;

        if (dialog != nullptr)
            return;

        dialog = UpdateDialog::launch (release,
            [this] (const Updates::Release& skipped)
            {
                juce::Logger::writeToLog ("Updates: skipping " + skipped.version.toString());
                if (settings != nullptr)
                    settings->setValue (Updates::skippedVersionKey, skipped.version.toString());
            },
            [this] (const juce::File& installer, bool installNow)
            {
                Updates::setPendingInstaller (installer);
                if (installNow && onInstallRequested != nullptr)
                    onInstallRequested();
            });
    }

    juce::PropertiesFile* settings = nullptr;
    std::unique_ptr<CheckThread> checker;
    juce::Component::SafePointer<UpdateDialog> dialog;
    juce::uint32 startupCheckDueMs = 0;
    bool reportResult = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Updater)
};
