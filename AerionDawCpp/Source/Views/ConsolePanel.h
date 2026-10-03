#pragma once
#include <JuceHeader.h>

class ConsolePanel : public juce::Component, public juce::Logger, private juce::Timer
{
public:
    ConsolePanel()
    {
        addAndMakeVisible (editor);
        editor.setMultiLine (true);
        editor.setReadOnly (true);
        editor.setScrollbarsShown (true);
        editor.setCaretVisible (false);
        editor.setReturnKeyStartsNewLine (true);
        
        // Use a monospaced font
        editor.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        
        // Chain in front of the existing logger (the aerion.log FileLogger) so the
        // log file keeps receiving every message while the console is open.
        previousLogger = juce::Logger::getCurrentLogger();
        juce::Logger::setCurrentLogger (this);
        startTimerHz (10); // Batch GUI updates for logs
    }

    ~ConsolePanel() override
    {
        if (juce::Logger::getCurrentLogger() == this)
            juce::Logger::setCurrentLogger (previousLogger);
    }

    void resized() override
    {
        editor.setBounds (getLocalBounds());
    }

    void logMessage (const juce::String& message) override
    {
        {
            const juce::ScopedLock sl (lock);
            pendingLogs.add (message);
        }

        if (previousLogger != nullptr)
            ForwardTo::log (*previousLogger, message);
        else
            juce::Logger::outputDebugString (message);
    }

    void timerCallback() override
    {
        juce::StringArray logsToPrint;
        {
            const juce::ScopedLock sl (lock);
            if (pendingLogs.isEmpty())
                return;
            
            logsToPrint = pendingLogs;
            pendingLogs.clear();
        }

        // Add to editor
        juce::String textToAppend = logsToPrint.joinIntoString ("\n") + "\n";
        editor.moveCaretToEnd();
        editor.insertTextAtCaret (textToAppend);
        
        // Truncate if too long (e.g. keep last 50000 chars)
        const int maxLength = 50000;
        if (editor.getTotalNumChars() > maxLength)
        {
            juce::String currentText = editor.getText();
            currentText = currentText.substring (currentText.length() - maxLength);
            editor.setText (currentText, false);
            editor.moveCaretToEnd();
        }
    }

private:
    // Logger::logMessage is protected; naming it through a derived class that
    // does not override it yields a Logger member pointer we may call.
    struct ForwardTo : juce::Logger
    {
        static void log (juce::Logger& target, const juce::String& message)
        {
            (target.*(&ForwardTo::logMessage)) (message);
        }
    };

    juce::Logger* previousLogger = nullptr;
    juce::TextEditor editor;
    juce::StringArray pendingLogs;
    juce::CriticalSection lock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsolePanel)
};
