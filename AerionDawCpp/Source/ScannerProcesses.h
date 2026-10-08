#pragma once
#include <JuceHeader.h>

/** The plugin scanner is a second copy of this program that Tracktion starts as
    a child process (tracktion_PluginScanHelpers.h) and does not let anyone else
    reach. */
namespace ScannerProcesses
{
    /** Ends every child process of this one that runs `programName` (this
        program's own file name if empty), without waiting for it. On macOS it
        ends every child process, since they are all scanners. */
    void endChildren (const juce::String& programName = {});
}
