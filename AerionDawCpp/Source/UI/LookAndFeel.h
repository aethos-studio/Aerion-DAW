#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include "ThemeTokens.h"

/** A custom LookAndFeel that implements the "Metal" arctic theme for all
    standard JUCE widgets, including window title bar buttons.
*/
class MetalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MetalLookAndFeel()
    {
        auto scheme = getDarkColourScheme();
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground, Theme::bgBase);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground, Theme::bgPanel);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuBackground, Theme::bgPanel);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::outline, Theme::border);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultText, Theme::textMain);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill, Theme::accent);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedFill, Theme::active);
        setColourScheme (scheme);

        // Body UI uses the OS sans (Segoe UI / etc.). Cinzel is far too thin at small sizes.
        if (auto face = Theme::defaultSansSerifFaceName(); face.isNotEmpty())
            setDefaultSansSerifTypefaceName (face);

        setColour (juce::TextButton::buttonColourId, Theme::surface);
        setColour (juce::TextButton::textColourOffId, Theme::textMain);
        setColour (juce::ListBox::backgroundColourId, Theme::bgPanel);
        setColour (juce::Label::textColourId, Theme::textMain);
    }

    // Custom button for window decorations
    class WindowButton : public juce::Button
    {
    public:
        WindowButton (const juce::String& name, juce::Colour c) : juce::Button (name), color (c) {}
        void paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown) override
        {
            // Use precise integer coordinates for maximum crispness
            auto b = getLocalBounds().toFloat();
            auto c = color;
            if (isMouseDown)      c = c.darker (0.3f);
            else if (isMouseOver) c = c.brighter (0.1f);

            // Subtle, sharp circle
            g.setColour (c.withAlpha (isMouseOver ? 0.95f : 0.75f));
            g.fillEllipse (b);

            // Hairline outline
            g.setColour (c.brighter (0.3f).withAlpha (0.4f));
            g.drawEllipse (b, 1.0f);

            // Modern, pixel-perfect icons
            g.setColour (Theme::bgBase.withAlpha (0.9f));
            auto iconArea = b.reduced (b.getWidth() * 0.32f);
            const float thickness = 1.0f; // Hairline thickness for elegance

            if (getName() == "close") {
                g.drawLine (iconArea.getX(), iconArea.getY(), iconArea.getRight(), iconArea.getBottom(), thickness);
                g.drawLine (iconArea.getRight(), iconArea.getY(), iconArea.getX(), iconArea.getBottom(), thickness);
            } else if (getName() == "min") {
                g.drawLine (iconArea.getX(), iconArea.getCentreY(), iconArea.getRight(), iconArea.getCentreY(), thickness);
            } else { // max
                g.drawRect (iconArea, thickness);
            }
        }
    private:
        juce::Colour color;
    };

    juce::Button* createDocumentWindowButton (int buttonType) override
    {
        if (buttonType == juce::DocumentWindow::closeButton)
            return new WindowButton ("close", Theme::accent); // Arctic Blue
        if (buttonType == juce::DocumentWindow::minimiseButton)
            return new WindowButton ("min", Theme::textMuted); // Steel Gray
        if (buttonType == juce::DocumentWindow::maximiseButton)
            return new WindowButton ("max", Theme::trackColours[3]); // Silver
        return nullptr;
    }

    void positionDocumentWindowButtons (juce::DocumentWindow&, int x, int y, int w, int h,
                                        juce::Button* minimiseButton,
                                        juce::Button* maximiseButton,
                                        juce::Button* closeButton,
                                        bool positionOnLeft) override
    {
        // Make buttons significantly smaller and more compact (14x14 instead of default ~24x24)
        const int size = 14;
        const int gap = 8;
        int curX = positionOnLeft ? x + 8 : x + w - size - 8;
        const int curY = y + (h - size) / 2;

        juce::Button* buttons[] = { closeButton, maximiseButton, minimiseButton };
        if (positionOnLeft) std::reverse (std::begin (buttons), std::end (buttons));

        for (auto* b : buttons)
        {
            if (b != nullptr)
            {
                b->setBounds (curX, curY, size, size);
                curX += positionOnLeft ? (size + gap) : -(size + gap);
            }
        }
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int buttonHeight) override
    {
        auto f = juce::LookAndFeel_V4::getTextButtonFont (b, buttonHeight);
        return f.withHeight (juce::jlimit (11.0f, 24.0f, f.getHeight() * Theme::kUiFontScale));
    }

    juce::Font getComboBoxFont (juce::ComboBox& c) override
    {
        auto f = juce::LookAndFeel_V4::getComboBoxFont (c);
        return f.withHeight (juce::jlimit (11.0f, 20.0f, f.getHeight() * Theme::kUiFontScale));
    }

    juce::Font getAlertWindowTitleFont() override
    {
        auto f = juce::LookAndFeel_V4::getAlertWindowTitleFont();
        return f.withHeight (f.getHeight() * Theme::kUiFontScale);
    }

    juce::Font getAlertWindowMessageFont() override
    {
        auto f = juce::LookAndFeel_V4::getAlertWindowMessageFont();
        return f.withHeight (f.getHeight() * Theme::kUiFontScale);
    }

    juce::Font getAlertWindowFont() override
    {
        auto f = juce::LookAndFeel_V4::getAlertWindowFont();
        return f.withHeight (f.getHeight() * Theme::kUiFontScale);
    }
};

