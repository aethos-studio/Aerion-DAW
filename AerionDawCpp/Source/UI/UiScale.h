#pragma once
#include <JuceHeader.h>

//==============================================================================
// View -> UI Size: how large Aerion draws its whole interface, on top of the
// operating system's display scaling.
//
// Aerion's layout is in fixed logical pixels, sized for a 1080p display at
// 100 % Windows scaling. On a 2560 x 1440 display at 100 % the same layout
// is physically much smaller and the text hard to read. JUCE's global scale
// factor scales every window uniformly (layout, text, hit-testing), so one
// setting fixes all of it without touching each view's layout code.
//
// Auto picks a size from the main display's height in OS-scaled pixels: a
// display that already runs at 150 % OS scaling gets no extra zoom from Aerion
// unless it is very tall.
//==============================================================================

namespace UiScale
{
    inline constexpr const char* settingsKey = "uiSize";

    // Sizes offered in the menu, in percent. A saved 0 means Auto.
    inline constexpr int kSizes[] = { 100, 125, 150, 175, 200 };
    inline constexpr int kMinPercent = 100, kMaxPercent = 200;

    // Auto never picks a size at which the main display holds less than this
    // much of Aerion's layout (logical pixels), so nothing gets crowded out.
    inline constexpr int kMinLogicalW = 1280, kMinLogicalH = 720;

    /** The size Auto picks for a display whose full area, in OS-scaled pixels
        (what Windows reports with Aerion's own UI size taken out), is `area`. */
    inline int autoPercentFor (juce::Rectangle<int> area)
    {
        const int h = area.getHeight();
        int percent = h >= 2100 ? 150 : h >= 1400 ? 125 : 100;

        while (percent > kMinPercent
               && (area.getWidth()  * 100 < kMinLogicalW * percent
                || area.getHeight() * 100 < kMinLogicalH * percent))
            percent -= 25;

        return percent;
    }

    /** The main display's area in OS-scaled pixels, without Aerion's UI size. */
    inline juce::Rectangle<int> mainDisplayArea()
    {
        auto& desktop = juce::Desktop::getInstance();
        if (auto* d = desktop.getDisplays().getPrimaryDisplay())
            return (d->totalArea.toFloat() * desktop.getGlobalScaleFactor()).toNearestInt();
        return { 1920, 1080 };
    }

    /** The percent a saved choice (0 = Auto, or a percent) resolves to. */
    inline int percentFor (int choice)
    {
        if (choice <= 0)
            return autoPercentFor (mainDisplayArea());
        return juce::jlimit (kMinPercent, kMaxPercent, choice);
    }

    inline int currentPercent()
    {
        return juce::roundToInt (juce::Desktop::getInstance().getGlobalScaleFactor() * 100.0f);
    }

    /** Scales every window. Open windows keep their size on screen and show
        their content larger or smaller inside it. */
    inline void apply (int percent)
    {
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) juce::jlimit (kMinPercent, kMaxPercent, percent) / 100.0f);

        for (int i = juce::ComponentPeer::getNumPeers(); --i >= 0;)
            if (auto* peer = juce::ComponentPeer::getPeer (i))
                peer->getComponent().repaint();
    }
}
