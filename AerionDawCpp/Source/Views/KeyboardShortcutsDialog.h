#pragma once

// Keyboard shortcut editor and its dialog.

#include "ViewShared.h"

//==============================================================================
// Editable keyboard-shortcut editor. Lists every catalog action, lets the user
// click a row to capture a new key, detects conflicts, and supports
// reset-to-defaults plus import/export of keymap files.
class KeyboardShortcutsPanel : public juce::Component,
                               public juce::ListBoxModel
{
public:
    KeyboardShortcutsPanel (AerionKeymap& km, juce::PropertiesFile* settings)
        : keymap (km), props (settings)
    {
        rebuildRows();

        list.setModel (this);
        list.setRowHeight (26);
        list.setWantsKeyboardFocus (false);
        list.setColour (juce::ListBox::backgroundColourId, Theme::bgPanel);
        addAndMakeVisible (list);

        auto initBtn = [this] (juce::TextButton& b, const juce::String& text)
        {
            b.setButtonText (text);
            addAndMakeVisible (b);
        };
        initBtn (resetBtn,  "Reset to Defaults");
        initBtn (importBtn, "Import...");
        initBtn (exportBtn, "Export...");
        initBtn (closeBtn,  "Close");

        resetBtn.onClick  = [this]
        {
            keymap.resetToDefaults();
            persist();
            cancelCapture();
        };
        importBtn.onClick = [this] { runImport(); };
        exportBtn.onClick = [this] { runExport(); };
        closeBtn.onClick  = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->closeButtonPressed();
        };

        setWantsKeyboardFocus (true);
        setSize (460, 540);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto footer = r.removeFromBottom (34);
        r.removeFromBottom (8);
        list.setBounds (r);

        const int gap = 6;
        const int w = (footer.getWidth() - gap * 3) / 4;
        resetBtn .setBounds (footer.removeFromLeft (w + 24)); footer.removeFromLeft (gap);
        importBtn.setBounds (footer.removeFromLeft (w - 8));  footer.removeFromLeft (gap);
        exportBtn.setBounds (footer.removeFromLeft (w - 8));  footer.removeFromLeft (gap);
        closeBtn .setBounds (footer);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Theme::bgPanel); }

    //== ListBoxModel ==========================================================
    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g,
                           int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, (int) rows.size()))
            return;

        const auto& row = rows[(size_t) rowNumber];
        auto b = juce::Rectangle<int> (0, 0, width, height);

        if (row.isHeader)
        {
            g.setColour (Theme::surface);
            g.fillRect (b);
            g.setColour (Theme::accent);
            g.setFont (Theme::uiSize (10.5f).withStyle (juce::Font::bold));
            g.drawText (row.text.toUpperCase(), b.reduced (8, 0),
                        juce::Justification::centredLeft, false);
            return;
        }

        const bool capturing = (capturingId == row.id);
        if (capturing)
        {
            g.setColour (Theme::accent.withAlpha (0.18f));
            g.fillRect (b);
        }

        g.setColour (Theme::textMuted);
        g.setFont (Theme::uiSize (11.5f));
        g.drawText (row.name, b.reduced (16, 0).removeFromLeft (width - 170),
                    juce::Justification::centredLeft, true);

        auto keyArea = b.removeFromRight (160).reduced (4, 4);
        juce::String keyText;
        juce::Colour keyColour;

        if (capturing)
        {
            keyText   = "Press a key (Esc cancels)";
            keyColour = Theme::accent;
        }
        else if (! row.rebindable)
        {
            keyText   = row.defaultKey;
            keyColour = Theme::textMuted.withAlpha (0.6f);
        }
        else
        {
            keyText   = AerionKeymap::describe (keymap.get (row.id));
            keyColour = keymap.get (row.id).isValid() ? Theme::textMain : Theme::meterYellow;
        }

        g.setColour (row.rebindable ? Theme::surface : Theme::bgPanel);
        g.fillRoundedRectangle (keyArea.toFloat(), 3.0f);
        g.setColour (keyColour);
        g.setFont (Theme::uiSize (10.5f).withStyle (juce::Font::bold));
        g.drawText (keyText, keyArea, juce::Justification::centred, true);
    }

    void listBoxItemClicked (int rowNumber, const juce::MouseEvent&) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, (int) rows.size()))
            return;

        const auto& row = rows[(size_t) rowNumber];
        if (row.isHeader || ! row.rebindable)
            return;

        capturingId = row.id;
        list.repaint();
        grabKeyboardFocus();
    }

    //== key capture ===========================================================
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (capturingId.isEmpty())
            return false;

        if (key == juce::KeyPress::escapeKey)
        {
            cancelCapture();
            return true;
        }

        const auto id = capturingId;
        auto conflictIds = keymap.conflicts (key, id);

        if (conflictIds.isEmpty())
        {
            keymap.set (id, key);
            persist();
            cancelCapture();
            return true;
        }

        juce::StringArray names;
        for (auto& cid : conflictIds)
            names.add (actionName (cid));

        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Shortcut Conflict",
            juce::String (key.getTextDescription())
                + " is already used by: " + names.joinIntoString (", ")
                + ".\n\nReassign it? The other action(s) will be left unassigned.",
            "Reassign", "Cancel", this,
            juce::ModalCallbackFunction::create ([this, key, id, conflictIds] (int result)
            {
                if (result == 1)
                {
                    for (auto& cid : conflictIds)
                        keymap.set (cid, juce::KeyPress());
                    keymap.set (id, key);
                    persist();
                }
                cancelCapture();
            }));
        return true;
    }

private:
    struct Row
    {
        bool isHeader = false;
        juce::String text;        // header label
        juce::String id, name, defaultKey;
        bool rebindable = true;
    };

    void rebuildRows()
    {
        rows.clear();
        juce::String section;
        for (auto& a : AerionActionCatalog::actions())
        {
            if (a.section != section)
            {
                section = a.section;
                rows.push_back ({ true, section, {}, {}, {}, false });
            }
            rows.push_back ({ false, {}, a.id, a.name, a.platformDefaultKey(), a.rebindable });
        }
        list.updateContent();
    }

    void cancelCapture()
    {
        capturingId = {};
        rebuildRows();
        repaint();
    }

    void persist() { keymap.saveTo (props); }

    static juce::String actionName (const juce::String& id)
    {
        for (auto& a : AerionActionCatalog::actions())
            if (a.id == id)
                return a.name;
        return id;
    }

    void runExport()
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Export Keymap", juce::File(), "*.aerionkeys");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f != juce::File())
                    keymap.exportToFile (f.withFileExtension ("aerionkeys"));
            });
    }

    void runImport()
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Import Keymap", juce::File(), "*.aerionkeys");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile() && keymap.importFromFile (f))
                {
                    persist();
                    cancelCapture();
                }
            });
    }

    AerionKeymap& keymap;
    juce::PropertiesFile* props = nullptr;
    std::vector<Row> rows;
    juce::String capturingId;

    juce::ListBox list;
    juce::TextButton resetBtn, importBtn, exportBtn, closeBtn;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardShortcutsPanel)
};

class KeyboardShortcutsDialog
{
public:
    static void launch (AerionKeymap& keymap, juce::PropertiesFile* settings)
    {
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new KeyboardShortcutsPanel (keymap, settings));
        o.dialogTitle                  = "Keyboard Shortcuts";
        o.dialogBackgroundColour       = Theme::bgPanel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar            = true;
        o.resizable                    = false;
        o.launchAsync();
    }
};
