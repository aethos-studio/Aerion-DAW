#include "ThemeTypefaces.h"

juce::Typeface::Ptr ThemeTypefaces::cinzelRegular()
{
    static juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (
        BinaryData::CinzelRegular_ttf, (size_t) BinaryData::CinzelRegular_ttfSize);
    return face;
}

juce::Typeface::Ptr ThemeTypefaces::cinzelBold()
{
    static juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (
        BinaryData::CinzelBold_ttf, (size_t) BinaryData::CinzelBold_ttfSize);
    return face;
}
