; Separate installer for the OPTIONAL Windows virtual camera component.
;
; Installs Softcam (https://github.com/tshino/softcam, MIT license), a
; user-mode DirectShow source filter (not a kernel driver), registers it with
; regsvr32 and records its path for Bad Connection Studio. It has its own
; uninstaller and Add/Remove Programs entry.
;
; CI only builds this installer when softcam.dll has been Authenticode-signed
; (see .github/workflows/windows.yml); it is never bundled into or run by
; the main application installer.
;
; makensis /DSOFTCAM_DLL=path\to\signed\softcam.dll /DVERSION=1.x softcam-component.nsi

!include "MUI2.nsh"
!include "x64.nsh"

!ifndef SOFTCAM_DLL
  !error "Pass /DSOFTCAM_DLL=<signed softcam.dll>"
!endif
!ifndef VERSION
  !define VERSION "0.0.0"
!endif

Name "Bad Connection Studio Virtual Camera"
OutFile "BadConnectionStudio-VirtualCamera-${VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES64\Bad Connection Studio Virtual Camera"
RequestExecutionLevel admin
Unicode true

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BadConnectionStudioVirtualCamera"
!define APP_KEY "Software\BadConnectionStudio\VirtualCamera"

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
  SetOutPath "$INSTDIR"
  File "${SOFTCAM_DLL}"
  File "softcam-LICENSE.txt"
  ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\softcam.dll"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Registering softcam.dll failed (code $0). Nothing was changed."
    Delete "$INSTDIR\softcam.dll"
    Abort
  ${EndIf}
  WriteRegStr HKLM "${APP_KEY}" "DllPath" "$INSTDIR\softcam.dll"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "Bad Connection Studio Virtual Camera"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "Bad Connection Studio contributors"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "Uninstall"
  ExecWait '"$SYSDIR\regsvr32.exe" /s /u "$INSTDIR\softcam.dll"'
  Delete "$INSTDIR\softcam.dll"
  Delete "$INSTDIR\softcam-LICENSE.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${APP_KEY}"
  DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
