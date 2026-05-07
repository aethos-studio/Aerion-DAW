#include <JuceHeader.h>
#include "MainComponent.h"
#include "SplashWindow.h"
#include "UIComponents.h"

class AerionDawApplication  : public juce::JUCEApplication
{
public:
    AerionDawApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::ignoreUnused (commandLine);

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
        // briefly block the message thread. If we do that immediately, the splash screen's
        // timer won't tick and you'll only see its first frame (solid background).
        //
        // So we let the splash animation run its intro first, then construct the main
        // window. The splash will keep animating until setReady() is called.
        constexpr int kSplashIntroMs = 3200; // ~180 frames @ 60 Hz + a little slack

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
            mainWindow = std::make_unique<MainWindow> (getApplicationName());

            // Show the main window *behind* the splash first so there is no
            // visible "gap" between splash closing and the DAW appearing.
            mainWindow->centreWithSize (mainWindow->getWidth(), mainWindow->getHeight());
            mainWindow->setVisible (true);

            // Only close the splash once the DAW has an actual native peer
            // (i.e. it has really been created and is ready to paint).
            auto tryCloseSplash = std::make_shared<std::function<void()>>();
            *tryCloseSplash = [this, tryCloseSplash]
            {
                if (splashWindow == nullptr || mainWindow == nullptr)
                    return;

                if (mainWindow->getPeer() != nullptr && mainWindow->isShowing())
                {
                    splashWindow->setStatus ("Ready");
                    splashWindow->setReady();
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

        if (appLogger != nullptr)
            juce::Logger::writeToLog ("=== Aerion shutting down ===");
        juce::Logger::setCurrentLogger (nullptr);
        appLogger.reset();
    }

    void systemRequestedQuit() override
    {
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
            setUsingNativeTitleBar (false);
            setContentOwned (new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
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
};

START_JUCE_APPLICATION (AerionDawApplication)
