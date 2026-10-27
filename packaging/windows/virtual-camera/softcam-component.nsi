; Separate installer for the OPTIONAL Windows virtual camera component.
;
; Installs Softcam (https://github.com/tshino/softcam, MIT license), a
; user-mode DirectShow source filter (not a kernel driver), in 64-bit and
; 32-bit builds so both kinds of call apps see the camera. Registers both with
; the matching regsvr32 and records the 64-bit path for Bad Connection Studio.
; It has its own uninstaller and Add/Remove Programs entry, and is never
; bundled into or run by the main application installer.
;
; The DLLs are Authenticode-signed when CI has a certificate, and unsigned
; otherwise (see docs/WINDOWS_SETUP.md).
;
; Expects x64\softcam.dll, x86\softcam.dll and softcam-LICENSE.txt here.
;   makensis /DVERSION=1.x softcam-component.nsi

!include "MUI2.nsh"
!include "x64.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

Name "Bad Connection Studio Virtual Camera"
OutFile "BadConnectionStudio-VirtualCamera-windows-setup.exe"
InstallDir "$PROGRAMFILES64\Bad Connection Studio Virtual Camera"
RequestExecutionLevel admin
Unicode true

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BadConnectionStudioVirtualCamera"
!define APP_KEY "Software\BadConnectionStudio\VirtualCamera"
; Explicit regsvr32 per architecture (NSIS itself is a 32-bit process).
!define REGSVR64 "$WINDIR\Sysnative\regsvr32.exe"
!define REGSVR32 "$WINDIR\SysWOW64\regsvr32.exe"

!insertmacro MUI_PAGE_LICENSE "softcam-LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "This component requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "Virtual camera (Softcam DirectShow filter)"
  SetOutPath "$INSTDIR\x64"
  File "x64\softcam.dll"
  SetOutPath "$INSTDIR\x86"
  File "x86\softcam.dll"
  SetOutPath "$INSTDIR"
  File "softcam-LICENSE.txt"

  ExecWait '"${REGSVR64}" /s "$INSTDIR\x64\softcam.dll"' $0
  ExecWait '"${REGSVR32}" /s "$INSTDIR\x86\softcam.dll"' $1
  ${If} $0 != 0
  ${OrIf} $1 != 0
    ExecWait '"${REGSVR64}" /s /u "$INSTDIR\x64\softcam.dll"'
    ExecWait '"${REGSVR32}" /s /u "$INSTDIR\x86\softcam.dll"'
    MessageBox MB_ICONSTOP "Registering softcam.dll failed (64-bit: $0, 32-bit: $1). Nothing was changed."
    RMDir /r "$INSTDIR"
    Abort
  ${EndIf}

  WriteRegStr HKLM "${APP_KEY}" "DllPath" "$INSTDIR\x64\softcam.dll"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "Bad Connection Studio Virtual Camera"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "Bad Connection Studio contributors"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "Uninstall"
  ExecWait '"${REGSVR64}" /s /u "$INSTDIR\x64\softcam.dll"'
  ExecWait '"${REGSVR32}" /s /u "$INSTDIR\x86\softcam.dll"'
  Delete "$INSTDIR\x64\softcam.dll"
  Delete "$INSTDIR\x86\softcam.dll"
  RMDir "$INSTDIR\x64"
  RMDir "$INSTDIR\x86"
  Delete "$INSTDIR\softcam-LICENSE.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${APP_KEY}"
  DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
