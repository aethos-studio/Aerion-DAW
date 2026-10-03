#include <JuceHeader.h>
#include "../CrashReporter.h"

//==============================================================================
// CrashReporter: a crash leaves a report with the session's log, and the next
// launch finds it once.
//
// The last test crashes a copy of this test app on purpose (see crashInto()
// in TestsMain.cpp) and checks the report the real crash handler wrote.
//==============================================================================

namespace
{
    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getNonexistentChildFile ("AerionCrashTest", {}, false);
        ~TempFolder() { folder.deleteRecursively(); }
    };

    CrashReporter::Details makeDetails (juce::Time when, const juce::String& reason = "invalid memory access")
    {
        CrashReporter::Details d;
        d.reason = reason;
        d.thread = "message thread";
        d.stack  = "0: somewhere\n1: somewhere else";
        d.time   = when;
        return d;
    }
}

class CrashReporterTests final : public juce::UnitTest
{
public:
    CrashReporterTests() : juce::UnitTest ("CrashReporter", "Aerion") {}

    void runTest() override
    {
        const juce::Time t0 (2026, 9, 3, 14, 30, 0, 0, true);

        beginTest ("a report holds the reason, thread, stack and the session's log");
        {
            TempFolder temp;
            auto log = temp.folder.getChildFile ("aerion.log");
            temp.folder.createDirectory();
            log.replaceWithText ("Startup: something\nPlugin fault: X");

            auto report = CrashReporter::writeReport (temp.folder.getChildFile ("Crashes"), makeDetails (t0), log);
            expect (report.isDirectory());

            const auto text = report.getChildFile ("report.txt").loadFileAsString();
            expect (text.contains ("Version: " + juce::String (ProjectInfo::versionString)));
            expect (text.contains ("Reason: invalid memory access"));
            expect (text.contains ("Thread: message thread"));
            expect (text.contains ("1: somewhere else"));
            expectEquals (report.getChildFile ("aerion.log").loadFileAsString(), log.loadFileAsString());
            expectEquals (CrashReporter::readReason (report), juce::String ("invalid memory access"));
        }

        beginTest ("two crashes in the same second get separate reports");
        {
            TempFolder temp;
            auto a = CrashReporter::writeReport (temp.folder, makeDetails (t0, "first"), {});
            auto b = CrashReporter::writeReport (temp.folder, makeDetails (t0, "second"), {});
            expect (a != b);
            expectEquals (CrashReporter::readReason (a), juce::String ("first"));
            expectEquals (CrashReporter::readReason (b), juce::String ("second"));
        }

        beginTest ("the newest unseen report is found once");
        {
            TempFolder temp;
            expect (CrashReporter::findUnseenReport (temp.folder) == juce::File(), "no folder, no report");

            auto older = CrashReporter::writeReport (temp.folder, makeDetails (t0), {});
            auto newer = CrashReporter::writeReport (temp.folder, makeDetails (t0 + juce::RelativeTime::hours (1)), {});

            expect (CrashReporter::findUnseenReport (temp.folder) == newer);
            CrashReporter::markSeen (newer);
            expect (CrashReporter::findUnseenReport (temp.folder) == older,
                    "an older unseen report is still reported");
            CrashReporter::markSeen (older);
            expect (CrashReporter::findUnseenReport (temp.folder) == juce::File());
        }

        beginTest ("old reports are pruned, newest kept");
        {
            TempFolder temp;
            juce::Array<juce::File> reports;

            for (int i = 0; i < 5; ++i)
                reports.add (CrashReporter::writeReport (temp.folder, makeDetails (t0 + juce::RelativeTime::days (i)), {}));

            CrashReporter::pruneOldReports (temp.folder, 2);
            expect (! reports[0].exists() && ! reports[1].exists() && ! reports[2].exists());
            expect (reports[3].exists() && reports[4].exists());
        }

        beginTest ("a real crash writes a report");
        {
            TempFolder temp;
            temp.folder.createDirectory();
            auto log = temp.folder.getChildFile ("aerion.log");
            log.replaceWithText ("last words");
            auto reports = temp.folder.getChildFile ("Crashes");

            juce::ChildProcess child;
            const juce::StringArray command { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                              "--crash-into", reports.getFullPathName(), log.getFullPathName() };
            expect (child.start (command, 0));
            expect (child.waitForProcessToFinish (60000), "crashing child must exit");

            auto report = CrashReporter::findUnseenReport (reports);
            expect (report.isDirectory(), "crash handler must write a report");
            expect (CrashReporter::readReason (report).startsWith ("invalid memory access"),
                    "reason: " + CrashReporter::readReason (report));
            expectEquals (report.getChildFile ("aerion.log").loadFileAsString(), juce::String ("last words"));
           #if JUCE_WINDOWS
            expect (report.getChildFile ("minidump.dmp").getSize() > 0, "Windows writes a minidump");
           #endif
        }
    }
};

static CrashReporterTests crashReporterTests;
