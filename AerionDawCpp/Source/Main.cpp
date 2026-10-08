#include <JuceHeader.h>
#include "MainComponent.h"
#include "SplashWindow.h"
#include "UIComponents.h"
#include "CrashReporter.h"
#include "UI/UiScale.h"
#include "Updates/UpdateChecker.h"

class AerionDawApplication  : public juce::JUCEApplication
{
public:
    AerionDawApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // A plugin-scan child process (see canScanPluginsOutOfProcess): it scans
        // and quits, and must not touch the log or the crash reports.
        if (tracktion::PluginManager::startChildProcessPluginScan (commandLine))
            return;

        // Logging must be initialised after JUCE startup (not at global init).
        {
            auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile ("AerionDAW");
            dir.createDirectory();
            logFile = dir.getChildFile ("aerion.log");
            appLogger = std::make_unique<juce::FileLogger> (logFile, "Aerion DAW log", 0);
            juce::Logger::setCurrentLogger (appLogger.get());
            juce::Logger::writeToLog ("=== Aerion starting ===");
            juce::Logger::writeToLog ("Log file: " + logFile.getFullPathName());

            // Before anything else can crash. The report copies this session's
            // log, which the next launch would otherwise delete.
            CrashReporter::install (CrashReporter::getDefaultReportsFolder(), logFile);
            startupStartedMs = juce::Time::getMillisecondCounterHiRes();
        }

        // View -> UI Size, before the first window opens so every window,
        // the splash included, comes up at that size.
        {
            juce::PropertiesFile settings (AudioEngineManager::userSettingsOptions());
            const int choice = settings.getIntValue (UiScale::settingsKey, 0);
            UiScale::apply (UiScale::percentFor (choice));
            juce::Logger::writeToLog ("UI size: " + (choice <= 0 ? juce::String ("Auto") : juce::String (choice) + " %")
                                      + " -> " + juce::String (UiScale::currentPercent()) + " %");
        }

        // Show splash immediately. Its onFinished callback reveals the main window
        // once the animation has played through and the fade is done.
        splashWindow = std::make_unique<SplashWindow> ([this]
        {
            if (mainWindow != nullptr)
            {
                mainWindow->centreWithSize (mainWindow->getWidth(), mainWindow->getHeight());
                mainWindow->setVisible (true);
                mainWindow->toFront (true);
            }

            // Now that the main window is visible, we can remove the splash.
            splashWindow.reset();
        });

        splashWindow->setStatus ("Starting up...");

        // IMPORTANT:
        // Creating the main window (and therefore MainComponent / Tracktion Engine) can
        // briefly block the message thread. If that happens before the splash intro has
        // revealed the logo and text, the animation visibly freezes or skips frames.
        // Keep construction behind the full intro, then hold/fade once the DAW is ready.
        constexpr int kSplashIntroMs = 3000; // matches SplashWindow's 180-frame intro at 60 Hz

        juce::Timer::callAfterDelay (350, [this]
        {
            if (splashWindow != nullptr)
                splashWindow->setStatus ("Loading audio engine...");
        });

        juce::Timer::callAfterDelay (1150, [this]
        {
            if (splashWindow != nullptr)
                splashWindow->setStatus ("Preparing UI...");
        });

        juce::Timer::callAfterDelay (2050, [this]
        {
            if (splashWindow != nullptr)
                splashWindow->setStatus ("Initialising modules...");
        });

        juce::Timer::callAfterDelay (kSplashIntroMs, [this]
        {
            const auto constructStart = juce::Time::getMillisecondCounterHiRes();
            mainWindow = std::make_unique<MainWindow> (getApplicationName());
            juce::Logger::writeToLog ("Startup: MainWindow constructed in "
                                      + juce::String (juce::Time::getMillisecondCounterHiRes() - constructStart, 1)
                                      + " ms");

            // The startup plugin scan runs in the background; the splash shows its
            // progress while it is up but never waits for it.
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                mc->getAudioEngine().onScanProgress = [this] (juce::String pluginName)
                {
                    if (splashWindow == nullptr)
                        return;
                    if (juce::File::isAbsolutePath (pluginName))
                        pluginName = juce::File (pluginName).getFileNameWithoutExtension();
                    splashWindow->setStatus ("Scanning plugins: " + pluginName);
                };

            // Show the main window *behind* the splash first so there is no
            // visible "gap" between splash closing and the DAW appearing.
            mainWindow->centreWithSize (mainWindow->getWidth(), mainWindow->getHeight());
            mainWindow->setVisible (true);

            // Only close the splash once the DAW has an actual native peer (i.e. it
            // has really been created and is ready to paint) and the audio devices
            // are open. Opening devices blocks the message thread for about half a
            // second and cannot move to another thread (Tracktion and ASIO need the
            // message thread), so it happens here, behind a splash at rest, rather
            // than freezing the splash fade or the freshly shown main window.
            const auto waitStartMs = juce::Time::getMillisecondCounterHiRes();
            auto tryCloseSplash = std::make_shared<std::function<void()>>();
            *tryCloseSplash = [this, tryCloseSplash, waitStartMs]
            {
                if (splashWindow == nullptr || mainWindow == nullptr)
                    return;

                auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
                const bool devicesReady = mc == nullptr || mc->getAudioEngine().areAudioDevicesConnected()
                                       // Never let a slow or missing driver keep the app hidden.
                                       || juce::Time::getMillisecondCounterHiRes() - waitStartMs > 4000.0;

                if (! devicesReady)
                    splashWindow->setStatus ("Starting audio devices...");

                if (devicesReady && mainWindow->getPeer() != nullptr && mainWindow->isShowing())
                {
                    splashWindow->setStatus ("Ready");
                    splashWindow->setReady();
                    juce::Logger::writeToLog ("Startup: main window ready after "
                                              + juce::String (juce::Time::getMillisecondCounterHiRes() - startupStartedMs, 1)
                                              + " ms");
                    return;
                }

                juce::Timer::callAfterDelay (50, [this, tryCloseSplash]
                {
                    if (splashWindow == nullptr || mainWindow == nullptr)
                        return;
                    (*tryCloseSplash)();
                });
            };

            (*tryCloseSplash)();
        });
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        splashWindow = nullptr;

        // Help › Check for Updates downloaded an installer to run once Aerion has closed.
        Updates::launchPendingInstaller();

        if (appLogger != nullptr)
            juce::Logger::writeToLog ("=== Aerion shutting down ===");
        juce::Logger::setCurrentLogger (nullptr);
        appLogger.reset();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow && mainWindow->getContentComponent())
        {
            if (auto* mc = dynamic_cast<MainComponent*>(mainWindow->getContentComponent()))
            {
                mc->requestQuit();
                return;
            }
        }
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        juce::ignoreUnused (commandLine);
    }

    class MainWindow    : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              Theme::bgBase,
                              DocumentWindow::allButtons)
        {
            const auto startMs = juce::Time::getMillisecondCounterHiRes();
            // macOS: the system title bar, with its window buttons and full
            // screen. Windows: Aerion's own, in its colours.
           #if JUCE_MAC
            setUsingNativeTitleBar (true);
           #else
            setUsingNativeTitleBar (false);
           #endif
            setContentOwned (new MainComponent(), true);
            juce::Logger::writeToLog ("Startup: MainComponent attached in "
                                      + juce::String (juce::Time::getMillisecondCounterHiRes() - startMs, 1)
                                      + " ms");

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);

            // A large UI size can make the default layout bigger than the screen.
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                setSize (juce::jmin (getWidth(),  display->userArea.getWidth()),
                         juce::jmin (getHeight(), display->userArea.getHeight()));

            centreWithSize (getWidth(), getHeight());
           #endif

            // Stay hidden until the splash fades — revealed via splashWindow's
            // onFinished callback in initialise().
            setVisible (false);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow>   mainWindow;
    std::unique_ptr<SplashWindow> splashWindow;
    std::unique_ptr<juce::FileLogger> appLogger;
    juce::File logFile;
    double startupStartedMs = 0.0;
};

START_JUCE_APPLICATION (AerionDawApplication)
