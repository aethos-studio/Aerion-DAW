#pragma once
#include <JuceHeader.h>
#include <functional>

// Question dialogs whose answers are named, not numbered.
//
// JUCE returns a button's number, and the numbering is easy to get wrong: with
// two buttons the first returns 1 and the second 0; with three, the first
// returns 1, the second 2 and the third 0. Escape also returns 0. So the last
// button must be the one that changes nothing (Cancel), and callers should
// never compare raw results themselves.
namespace Dialogs
{
    enum class SaveChoice { save, discard, cancel };

    /** Buttons left to right: save, discard, Cancel. */
    inline juce::MessageBoxOptions saveChangesOptions (const juce::String& title,
                                                       const juce::String& message,
                                                       const juce::String& saveText = "Save",
                                                       const juce::String& discardText = "Discard")
    {
        return juce::MessageBoxOptions()
                   .withIconType (juce::MessageBoxIconType::QuestionIcon)
                   .withTitle (title)
                   .withMessage (message)
                   .withButton (saveText)
                   .withButton (discardText)
                   .withButton ("Cancel");
    }

    inline SaveChoice saveChoiceFromResult (int result)
    {
        if (result == 1) return SaveChoice::save;
        if (result == 2) return SaveChoice::discard;
        return SaveChoice::cancel;
    }

    /** Asks whether to save changes before going on. */
    inline void askToSaveChanges (const juce::String& title, const juce::String& message,
                                  std::function<void (SaveChoice)> onChoice,
                                  const juce::String& saveText = "Save",
                                  const juce::String& discardText = "Discard")
    {
        juce::AlertWindow::showAsync (saveChangesOptions (title, message, saveText, discardText),
                                      [onChoice = std::move (onChoice)] (int result)
                                      {
                                          onChoice (saveChoiceFromResult (result));
                                      });
    }

    /** Buttons left to right: confirm, then the one that changes nothing. */
    inline juce::MessageBoxOptions confirmOptions (const juce::String& title, const juce::String& message,
                                                   const juce::String& confirmText, const juce::String& declineText)
    {
        return juce::MessageBoxOptions()
                   .withIconType (juce::MessageBoxIconType::QuestionIcon)
                   .withTitle (title)
                   .withMessage (message)
                   .withButton (confirmText)
                   .withButton (declineText);
    }

    inline bool confirmedFromResult (int result)   { return result == 1; }

    /** Asks a yes/no question; onAnswer receives true for confirmText. */
    inline void confirm (const juce::String& title, const juce::String& message,
                         const juce::String& confirmText, const juce::String& declineText,
                         std::function<void (bool)> onAnswer)
    {
        juce::AlertWindow::showAsync (confirmOptions (title, message, confirmText, declineText),
                                      [onAnswer = std::move (onAnswer)] (int result)
                                      {
                                          onAnswer (confirmedFromResult (result));
                                      });
    }
}
