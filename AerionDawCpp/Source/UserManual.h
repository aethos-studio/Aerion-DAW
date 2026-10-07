#pragma once

// Help › User Manual: the manual ships inside the app as one self-contained
// HTML file. Opening it writes that file next to Aerion's settings and hands
// it to the system, which opens it in the default browser.

#include <JuceHeader.h>

namespace UserManual
{
    inline constexpr const char* fileName = "Aerion-DAW-Manual.html";

    /** Where the manual is written: the same folder as Aerion's crash reports. */
    inline juce::File getFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("AerionDAW")
                   .getChildFile (fileName);
    }

    /** Writes the built-in manual to `file`, unless an identical copy is
        already there (an update rewrites it). False if there is no built-in
        manual or it could not be written. */
    inline bool install (const juce::File& file)
    {
        const void* data = nullptr;
        int size = 0;

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (juce::String (BinaryData::originalFilenames[i]) == fileName)
                data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);

        if (data == nullptr)
            return false;

        const juce::MemoryBlock manual (data, (size_t) size);
        juce::MemoryBlock existing;

        if (file.existsAsFile() && file.loadFileAsData (existing) && existing == manual)
            return true;

        return file.getParentDirectory().createDirectory() && file.replaceWithData (data, (size_t) size);
    }

    /** Writes the manual and opens it in the default browser. */
    inline bool open()
    {
        const auto file = getFile();
        return install (file) && file.startAsProcess();
    }
}
