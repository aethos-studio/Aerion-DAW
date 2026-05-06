#pragma once
#include <JuceHeader.h>

/** Embedded fonts from Resources (see CMake juce_add_binary_data). */
namespace ThemeTypefaces
{
    /** Cinzel Regular (Resources/Cinzel-Regular.ttf). Primary UI sans. */
    juce::Typeface::Ptr cinzelRegular();

    /** Cinzel Bold (Resources/Cinzel-Bold.ttf). May be null if the binary slot is missing. */
    juce::Typeface::Ptr cinzelBold();
}
