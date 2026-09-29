#pragma once

// Dialog asking how to place several dropped files.

#include "ViewShared.h"

//==============================================================================
class InsertMultipleMediaDialog : public juce::Component
{
public:
    enum class InsertMode
    {
        separateTracks = 0,     // One file per track, same time position
        sequentialSingleTrack,  // All files on one track, sequential time positions
        fixedLanes              // All files on one track, same time position (side by side)
    };

    std::function<void (InsertMode)> onModeSelected;

    InsertMultipleMediaDialog() : selectedMode (InsertMode::separateTracks)
    {
        setOpaque (true);
        setSize (360, 200);

        // Title
        addAndMakeVisible (titleLabel);
        titleLabel.setText ("Insert Multiple Media Items", juce::dontSendNotification);
        titleLabel.setFont (Theme::uiSize (14.0f).boldened());
        titleLabel.setColour (juce::Label::textColourId, Theme::textMain);

        // Radio buttons
        addAndMakeVisible (separateTracksButton);
        separateTracksButton.setButtonText ("Same time position on separate tracks");
        separateTracksButton.onClick = [this] { selectedMode = InsertMode::separateTracks; updateButtonStates(); };

        addAndMakeVisible (sequentialButton);
        sequentialButton.setButtonText ("Sequential time positions on a single track");
        sequentialButton.onClick = [this] { selectedMode = InsertMode::sequentialSingleTrack; updateButtonStates(); };

        addAndMakeVisible (fixedLanesButton);
        fixedLanesButton.setButtonText ("Same time position in fixed lanes on a single track");
        fixedLanesButton.onClick = [this] { selectedMode = InsertMode::fixedLanes; updateButtonStates(); };

        // Buttons
        addAndMakeVisible (okButton);
        okButton.setButtonText ("OK");
        okButton.onClick = [this] { if (onModeSelected) onModeSelected (selectedMode); };

        addAndMakeVisible (cancelButton);
        cancelButton.setButtonText ("Cancel");
        cancelButton.onClick = [this] {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->closeButtonPressed();
        };

        updateButtonStates();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bgPanel);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (16);

        auto titleArea = b.removeFromTop (30);
        titleLabel.setBounds (titleArea);
        b.removeFromTop (12);

        auto buttonH = 24;
        auto spacing = 8;

        auto row1 = b.removeFromTop (buttonH);
        separateTracksButton.setBounds (row1);
        b.removeFromTop (spacing);

        auto row2 = b.removeFromTop (buttonH);
        sequentialButton.setBounds (row2);
        b.removeFromTop (spacing);

        auto row3 = b.removeFromTop (buttonH);
        fixedLanesButton.setBounds (row3);
        b.removeFromTop (12);

        auto buttonArea = b.removeFromBottom (32);
        auto okArea = buttonArea.removeFromRight (80);
        cancelButton.setBounds (okArea.withX (buttonArea.getRight() - 168));
        okButton.setBounds (okArea.withX (buttonArea.getRight() - 80));
    }

    static void launch (std::function<void (InsertMode)> callback)
    {
        auto* dialog = new InsertMultipleMediaDialog();
        dialog->onModeSelected = callback;

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (dialog);
        opts.dialogTitle = "Insert Multiple Media Items";
        opts.dialogBackgroundColour = Theme::bgPanel;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = false;
        opts.launchAsync();
    }

private:
    InsertMode selectedMode;

    juce::Label titleLabel;
    juce::ToggleButton separateTracksButton { "separateTracks" };
    juce::ToggleButton sequentialButton { "sequential" };
    juce::ToggleButton fixedLanesButton { "fixedLanes" };
    juce::TextButton okButton;
    juce::TextButton cancelButton;

    void updateButtonStates()
    {
        separateTracksButton.setToggleState (selectedMode == InsertMode::separateTracks, juce::dontSendNotification);
        sequentialButton.setToggleState (selectedMode == InsertMode::sequentialSingleTrack, juce::dontSendNotification);
        fixedLanesButton.setToggleState (selectedMode == InsertMode::fixedLanes, juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsertMultipleMediaDialog)
};
