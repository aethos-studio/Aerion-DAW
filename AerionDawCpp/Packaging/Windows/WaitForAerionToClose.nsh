; Waits until no Aerion DAW is running from $INSTDIR.
;
; Windows does not let the installer replace (or the uninstaller delete)
; "Aerion DAW.exe" while Aerion runs from it, and NSIS then stops with "Error
; opening file for writing". That happens when the user installs with Aerion
; open, and every time Help > Check for Updates starts the installer, because
; Aerion is still shutting down at that moment.
;
; Opening the file for writing fails exactly while it is running, so that is
; the test. Wait up to 15 seconds for it to close, then ask the user to close
; it. CPack includes this file at the start of the install section
; (CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS) and of the uninstall section
; (CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS). Labels are local to each section.

  StrCpy $R9 0
aerion_check:
  IfFileExists "$INSTDIR\Aerion DAW.exe" 0 aerion_closed
  ClearErrors
  FileOpen $R8 "$INSTDIR\Aerion DAW.exe" a
  IfErrors aerion_running
  FileClose $R8
  Goto aerion_closed

aerion_running:
  IntCmp $R9 0 0 aerion_waited aerion_waited
  DetailPrint "Waiting for Aerion DAW to close..."
aerion_waited:
  IntCmp $R9 15 aerion_ask 0 aerion_ask
  Sleep 1000
  IntOp $R9 $R9 + 1
  Goto aerion_check

aerion_ask:
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
    "Aerion DAW is still running.$\n$\nSave your work and close Aerion DAW, then click Retry." \
    /SD IDCANCEL IDRETRY aerion_check
  Abort "Aerion DAW is still running."

aerion_closed:
