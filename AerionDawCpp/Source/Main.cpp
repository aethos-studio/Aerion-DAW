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

        // Show splash immediately. Its onFinished callback reveals the main window
        // once the animation has played through and the fade is done.
        splashWindow = std::make_unique<SplashWindow> ([this]
        {
            splashWindow.reset();
            if (mainWindow != nullptr)
            {
                mainWindow->centreWithSize (mainWindow->getWidth(), mainWindow->getHeight());
                mainWindow->setVisible (true);
                mainWindow->toFront (true);
            }
        });

        splashWindow->setStatus ("Loading audio engine...");

        // Build the main window hidden on the next message pump tick so the
        // splash gets at least one frame on screen before the heavy ctor blocks
        // the message thread.
        juce::MessageManager::callAsync ([this]
        {
            mainWindow = std::make_unique<MainWindow> (getApplicationName());

            // Tell the splash the app is ready. It will hold until its animation
            // has played through, then fade and fire onFinished (above).
            if (splashWindow != nullptr)
                splashWindow->setReady();
        });
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        splashWindow = nullptr;
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
};

START_JUCE_APPLICATION (AerionDawApplication)
