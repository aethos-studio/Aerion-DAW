#pragma once
#include <JuceHeader.h>

/** Writes a crash report when the app crashes, and finds it on the next launch.

    install() hooks JUCE's crash handler. When the app crashes it writes a
    folder under the reports folder, named after the time of the crash:

      report.txt    version, OS, what went wrong, on which thread, stack trace
      aerion.log    the crashed session's log (the next launch deletes the
                    original, so it is copied here)
      minidump.dmp  Windows only: a minidump for a debugger

    On the next launch findUnseenReport() returns the newest report the user has
    not been shown yet; markSeen() records that it has been. */
namespace CrashReporter
{
    /** <app data>/AerionDAW/Crashes */
    juce::File getDefaultReportsFolder();

    /** Installs the crash handler and drops the oldest reports beyond maxKept. */
    void install (const juce::File& reportsFolder, const juce::File& logFile, int maxKept = 10);

    /** What a report says, apart from its folder. */
    struct Details
    {
        juce::String reason;     // e.g. "invalid memory access"
        juce::String thread;     // e.g. "message thread"
        juce::String stack;      // juce::SystemStats::getStackBacktrace()
        juce::Time   time;
    };

    /** Writes report.txt (and copies logFile, if it exists) into a new folder
        under reportsFolder. Returns the folder, or {} if it could not be made.
        Used by the crash handler; public for tests. */
    juce::File writeReport (const juce::File& reportsFolder, const Details&, const juce::File& logFile);

    /** The newest report folder not yet marked seen, or {} if there is none. */
    juce::File findUnseenReport (const juce::File& reportsFolder);

    /** Records that the user has been shown this report. */
    void markSeen (const juce::File& reportFolder);

    /** The report's "Reason:" line, for the dialog shown on the next launch. */
    juce::String readReason (const juce::File& reportFolder);

    /** Deletes the oldest report folders so at most maxKept remain. */
    void pruneOldReports (const juce::File& reportsFolder, int maxKept);
}
