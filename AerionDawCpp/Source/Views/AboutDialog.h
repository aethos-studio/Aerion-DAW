#pragma once

// About dialog.

#include "ViewShared.h"

//==============================================================================
// "About Aerion DAW" dialog. Hosts the vertical Aerion logo, version + credits,
// and (when ASIO is compiled in) the official Steinberg "ASIO compatible" logo
// + trademark attribution as required by Steinberg's brand guidelines.
class AboutDialog : public juce::Component
{
public:
    AboutDialog()
    {
        if (auto x = juce::XmlDocument::parse (juce::String::fromUTF8 (
                BinaryData::aerion_logo_ui_svg,
                BinaryData::aerion_logo_ui_svgSize)))
            aerionLogo = juce::Drawable::createFromSVG (*x);

       #if JUCE_WINDOWS && JUCE_ASIO
        if (auto x = juce::XmlDocument::parse (juce::String::fromUTF8 (
                BinaryData::asio_compatible_logo_svg,
                BinaryData::asio_compatible_logo_svgSize)))
            asioLogo = juce::Drawable::createFromSVG (*x);
       #endif

        setOpaque (true);
        setSize (440, 460);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0c0f14));

        auto b = getLocalBounds().toFloat().reduced (24.0f);

        if (aerionLogo != nullptr)
        {
            auto logoArea = b.removeFromTop (160.0f);
            aerionLogo->drawWithin (g, logoArea, juce::RectanglePlacement::centred, 1.0f);
        }

        b.removeFromTop (8.0f);

        const auto regTF  = ThemeTypefaces::cinzelRegular();
        const auto boldTF = ThemeTypefaces::cinzelBold();

        juce::Font titleFont (boldTF != nullptr
            ? juce::FontOptions (boldTF).withHeight (22.0f)
            : juce::FontOptions().withHeight (22.0f).withStyle ("Bold"));
        titleFont.setExtraKerningFactor (0.10f);
        g.setFont (titleFont);
        g.setColour (juce::Colour (0xffebf8ff));
        {
            auto titleRow = b.removeFromTop (28.0f);
            g.drawText ("AERION DAW", titleRow.toNearestInt(),
                        juce::Justification::centred, false);
        }

        juce::Font versionFont (regTF != nullptr
            ? juce::FontOptions (regTF).withHeight (12.0f)
            : juce::FontOptions().withHeight (12.0f));
        versionFont.setExtraKerningFactor (0.18f);
        g.setFont (versionFont);
        g.setColour (juce::Colour (0xff63b3ed));
        {
            auto versionRow = b.removeFromTop (18.0f);
            g.drawText ("v" + juce::String (ProjectInfo::versionString)
                            + juce::String::fromUTF8 (u8"   \u2014   AETHOS STUDIO LTD."),
                        versionRow.toNearestInt(),
                        juce::Justification::centred, false);
        }

        b.removeFromTop (16.0f);

        juce::Font bodyFont (regTF != nullptr
            ? juce::FontOptions (regTF).withHeight (12.5f)
            : juce::FontOptions().withHeight (12.5f));
        bodyFont.setExtraKerningFactor (0.06f);
        g.setFont (bodyFont);
        g.setColour (juce::Colour (0xffaecbe0));
        {
            auto creditRow = b.removeFromTop (60.0f);
            g.drawMultiLineText (
                "Built with JUCE and Tracktion Engine.\n"
                "Audio I/O routed through ASIO, WASAPI, DirectSound,\n"
                "CoreAudio, ALSA, JACK and WinRT MIDI.",
                creditRow.getX(), (int) creditRow.getY() + 12,
                (int) creditRow.getWidth(), juce::Justification::centred);
        }

       #if JUCE_WINDOWS && JUCE_ASIO
        if (asioLogo != nullptr)
        {
            auto footer = getLocalBounds().toFloat().removeFromBottom (76.0f).reduced (24.0f, 8.0f);

            const float logoH = 30.0f;
            const float logoW = 88.0f;
            juce::Rectangle<float> logoBounds (footer.getCentreX() - logoW * 0.5f,
                                               footer.getY(),
                                               logoW, logoH);
            asioLogo->drawWithin (g, logoBounds,
                                  juce::RectanglePlacement::centred, 0.95f);

            juce::Font footerFont (regTF != nullptr
                ? juce::FontOptions (regTF).withHeight (10.0f)
                : juce::FontOptions().withHeight (10.0f));
            footerFont.setExtraKerningFactor (0.18f);
            g.setFont (footerFont);
            g.setColour (juce::Colour (0xff9ecfeb).withAlpha (0.65f));
            g.drawText (juce::String::fromUTF8 (
                            u8"ASIO\u2122 - \u00A9 Steinberg Media Technologies GmbH"),
                        (int) footer.getX(),
                        (int) (logoBounds.getBottom() + 4.0f),
                        (int) footer.getWidth(), 16,
                        juce::Justification::centred);
        }
       #endif
    }

    // Show as a modal floating window. Closes when the user clicks outside or
    // presses Escape.
    static void launch()
    {
        auto* dialog = new AboutDialog();

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (dialog);
        opts.dialogTitle = "About Aerion DAW";
        opts.dialogBackgroundColour = juce::Colour (0xff0c0f14);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = false;
        opts.launchAsync();
    }

private:
    std::unique_ptr<juce::Drawable> aerionLogo;
   #if JUCE_WINDOWS && JUCE_ASIO
    std::unique_ptr<juce::Drawable> asioLogo;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutDialog)
};
