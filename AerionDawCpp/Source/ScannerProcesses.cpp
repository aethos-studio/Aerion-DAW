#include "ScannerProcesses.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <tlhelp32.h>
#elif JUCE_MAC
 #include <unistd.h>
#endif

void ScannerProcesses::endChildren (const juce::String& programName)
{
   #if JUCE_WINDOWS
    const auto name = programName.isNotEmpty()
                          ? programName
                          : juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFileName();
    const DWORD self = GetCurrentProcessId();

    const HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof (entry);

    for (BOOL more = Process32FirstW (snapshot, &entry); more; more = Process32NextW (snapshot, &entry))
    {
        if (entry.th32ParentProcessID != self || ! juce::String (entry.szExeFile).equalsIgnoreCase (name))
            continue;

        if (const HANDLE process = OpenProcess (PROCESS_TERMINATE, FALSE, entry.th32ProcessID))
        {
            TerminateProcess (process, 0);
            CloseHandle (process);
        }
    }

    CloseHandle (snapshot);
   #elif JUCE_MAC
    juce::ignoreUnused (programName);

    juce::ChildProcess killer;
    if (killer.start (juce::StringArray { "/usr/bin/pkill", "-KILL", "-P", juce::String ((int) getpid()) }, 0))
        killer.waitForProcessToFinish (1000);
   #else
    juce::ignoreUnused (programName);
   #endif
}
