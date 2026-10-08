#include "CrashReporter.h"
#include "PluginFaultGuard.h"
#include "Updates/UpdateChecker.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dbghelp.h>
 #pragma comment (lib, "Dbghelp.lib")
#else
 #include <csignal>
#endif

namespace
{
    constexpr const char* reportFileName = "report.txt";
    constexpr const char* seenFileName   = "seen";
    constexpr const char* folderPrefix   = "crash-";
    constexpr const char* reasonPrefix   = "Reason: ";

    // Set by install(), read by the crash handler. Made before any crash so the
    // handler does not have to build them.
    juce::File& installedReportsFolder() { static juce::File f; return f; }
    juce::File& installedLogFile()       { static juce::File f; return f; }

    juce::Array<juce::File> reportFolders (const juce::File& reportsFolder)
    {
        juce::Array<juce::File> folders;

        for (const auto& entry : juce::RangedDirectoryIterator (reportsFolder, false, juce::String (folderPrefix) + "*",
                                                                juce::File::findDirectories))
            folders.add (entry.getFile());

        // Folder names are timestamps, so name order is time order (oldest first).
        struct ByName { static int compareElements (const juce::File& a, const juce::File& b)
                        { return a.getFileName().compare (b.getFileName()); } };
        ByName sorter;
        folders.sort (sorter);
        return folders;
    }

    juce::String currentThreadName()
    {
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
             && juce::MessageManager::existsAndIsCurrentThread())
            return "message thread";

        if (auto* t = juce::Thread::getCurrentThread())
            return "thread \"" + t->getThreadName() + "\"";

        return "another thread (audio device or plugin thread)";
    }

   #if JUCE_WINDOWS
    void writeMinidump (const juce::File& dest, EXCEPTION_POINTERS* ep)
    {
        auto file = CreateFileW (dest.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        MINIDUMP_EXCEPTION_INFORMATION info {};
        info.ThreadId          = GetCurrentThreadId();
        info.ExceptionPointers = ep;
        info.ClientPointers    = FALSE;

        MiniDumpWriteDump (GetCurrentProcess(), GetCurrentProcessId(), file,
                           (MINIDUMP_TYPE) (MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                           ep != nullptr ? &info : nullptr, nullptr, nullptr);
        CloseHandle (file);
    }

    juce::String describeException (EXCEPTION_POINTERS* ep)
    {
        if (ep == nullptr || ep->ExceptionRecord == nullptr)
            return "unknown crash";

        const auto& record = *ep->ExceptionRecord;
        auto reason = PluginFaultMonitor::describeFault ((unsigned int) record.ExceptionCode);

        if (record.ExceptionCode == EXCEPTION_STACK_OVERFLOW)
            reason = "stack overflow";

        return reason + " at 0x" + juce::String::toHexString ((juce::pointer_sized_int) record.ExceptionAddress);
    }
   #else
    juce::String describeSignal (int signum)
    {
        switch (signum)
        {
            case SIGSEGV: return "invalid memory access (SIGSEGV)";
            case SIGBUS:  return "invalid memory access (SIGBUS)";
            case SIGFPE:  return "arithmetic error (SIGFPE)";
            case SIGILL:  return "invalid instruction (SIGILL)";
            case SIGABRT: return "abort (SIGABRT)";
            default:      return "signal " + juce::String (signum);
        }
    }
   #endif

    void handleCrash (void* platformData)
    {
        CrashReporter::Details details;
        details.time   = juce::Time::getCurrentTime();
        details.thread = currentThreadName();

       #if JUCE_WINDOWS
        auto* ep = static_cast<EXCEPTION_POINTERS*> (platformData);
        details.reason = describeException (ep);

        // A stack overflow leaves no stack to walk the stack with.
        const bool stackUsable = ep == nullptr || ep->ExceptionRecord == nullptr
                              || ep->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW;
       #else
        details.reason = describeSignal ((int) (juce::pointer_sized_int) platformData);
        const bool stackUsable = true;
       #endif

        if (stackUsable)
            details.stack = juce::SystemStats::getStackBacktrace();

        const auto folder = CrashReporter::writeReport (installedReportsFolder(), details, installedLogFile());

       #if JUCE_WINDOWS
        if (folder != juce::File())
            writeMinidump (folder.getChildFile ("minidump.dmp"), ep);
       #else
        juce::ignoreUnused (folder);
       #endif
    }
}

namespace CrashReporter
{
    juce::File getDefaultReportsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("AerionDAW")
                   .getChildFile ("Crashes");
    }

    void install (const juce::File& reportsFolder, const juce::File& logFile, int maxKept)
    {
        installedReportsFolder() = reportsFolder;
        installedLogFile()       = logFile;

        reportsFolder.createDirectory();
        pruneOldReports (reportsFolder, maxKept);

        juce::SystemStats::setApplicationCrashHandler (handleCrash);
    }

    juce::File writeReport (const juce::File& reportsFolder, const Details& details, const juce::File& logFile)
    {
        if (! reportsFolder.createDirectory())
            return {};

        const auto baseName = folderPrefix + details.time.formatted ("%Y-%m-%d_%H-%M-%S");
        auto folder = reportsFolder.getChildFile (baseName);

        for (int n = 2; folder.exists(); ++n)
            folder = reportsFolder.getChildFile (baseName + "_" + juce::String (n));

        if (! folder.createDirectory())
            return {};

        juce::String text;
        text << "Aerion DAW crash report" << juce::newLine
             << juce::newLine
             << "Version: " << Updates::buildVersionText() << juce::newLine
             << "Time: " << details.time.toISO8601 (true) << juce::newLine
             << "OS: " << juce::SystemStats::getOperatingSystemName()
             << (juce::SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : "") << juce::newLine
             << "CPU: " << juce::SystemStats::getCpuModel()
             << ", " << juce::SystemStats::getNumCpus() << " cores" << juce::newLine
             << "Memory: " << juce::SystemStats::getMemorySizeInMegabytes() << " MB" << juce::newLine
             << juce::newLine
             << reasonPrefix << details.reason << juce::newLine
             << "Thread: " << details.thread << juce::newLine
             << juce::newLine
             << "Stack:" << juce::newLine
             << (details.stack.isNotEmpty() ? details.stack : juce::String ("(not available)")) << juce::newLine;

        folder.getChildFile (reportFileName).replaceWithText (text);

        if (logFile.existsAsFile())
            logFile.copyFileTo (folder.getChildFile (logFile.getFileName()));

        return folder;
    }

    juce::File findUnseenReport (const juce::File& reportsFolder)
    {
        const auto folders = reportFolders (reportsFolder);

        for (int i = folders.size(); --i >= 0;)
        {
            const auto& folder = folders.getReference (i);

            if (folder.getChildFile (reportFileName).existsAsFile()
                 && ! folder.getChildFile (seenFileName).exists())
                return folder;
        }

        return {};
    }

    void markSeen (const juce::File& reportFolder)
    {
        reportFolder.getChildFile (seenFileName).create();
    }

    juce::String readReason (const juce::File& reportFolder)
    {
        juce::StringArray lines;
        reportFolder.getChildFile (reportFileName).readLines (lines);

        for (const auto& line : lines)
            if (line.startsWith (reasonPrefix))
                return line.fromFirstOccurrenceOf (reasonPrefix, false, false).trim();

        return {};
    }

    void pruneOldReports (const juce::File& reportsFolder, int maxKept)
    {
        auto folders = reportFolders (reportsFolder);

        for (int i = 0; i < folders.size() - juce::jmax (0, maxKept); ++i)
            folders.getReference (i).deleteRecursively();
    }
}
