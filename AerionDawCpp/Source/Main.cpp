#include <JuceHeader.h>
#include "MainComponent.h"
#include "SplashWindow.h"

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
        
        splashWindow.reset (new SplashWindow ([this] {
            splashWindow = nullptr;
            mainWindow.reset (new MainWindow (getApplicationName()));
        }));
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
                              juce::Colour::fromString ("#ff0e0e11"),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (false);
            setContentOwned (new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            
            // centerWithSize uses the window's current width/height. 
            // setContentOwned already set them based on MainComponent.
            centreWithSize (getWidth(), getHeight());
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<SplashWindow> splashWindow;
};

START_JUCE_APPLICATION (AerionDawApplication)
