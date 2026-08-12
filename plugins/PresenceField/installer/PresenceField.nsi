; PresenceField v0.1.0 Windows Installer
; NSIS 3.x — Unicode
; Build: makensis /DVERSION=0.1.0 PresenceField.nsi

!ifndef VERSION
  !define VERSION "0.1.0"
!endif

Name "PresenceField ${VERSION}"
OutFile "PresenceField-v${VERSION}-Windows-Installer.exe"
Unicode True
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES\Zarlino Audio\PresenceField"

!include "MUI2.nsh"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "PresenceField Plugin" SecMain
  SetOutPath "$INSTDIR"

  ; --- Standalone executable ---
  File /nonfatal "..\build\plugins\PresenceField\PresenceField_artefacts\Release\Standalone\PresenceField.exe"

  ; --- VST3 plugin ---
  SetOutPath "$COMMONFILES\VST3"
  File /nonfatal /r "..\build\plugins\PresenceField\PresenceField_artefacts\Release\VST3\PresenceField.vst3\Contents\x86_64-win\*.vst3"

  SetOutPath "$COMMONFILES\VST3\PresenceField.vst3\Contents"
  File /nonfatal "..\build\plugins\PresenceField\PresenceField_artefacts\Release\VST3\PresenceField.vst3\Contents\moduleinfo.json"

  ; --- Start Menu shortcuts ---
  CreateDirectory "$SMPROGRAMS\Zarlino Audio\PresenceField"
  CreateShortcut "$SMPROGRAMS\Zarlino Audio\PresenceField\PresenceField.lnk" "$INSTDIR\PresenceField.exe"
  CreateShortcut "$SMPROGRAMS\Zarlino Audio\PresenceField\Uninstall PresenceField.lnk" "$INSTDIR\Uninstall.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioPresenceField" \
                   "DisplayName" "PresenceField — Dual-Path Spatial Engine"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioPresenceField" \
                   "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioPresenceField" \
                   "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioPresenceField" \
                   "Publisher" "Zarlino Audio"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioPresenceField" \
                   "URLInfoAbout" "https://zarlinoaudio.com"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioPresenceField" \
                     "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioPresenceField" \
                     "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\PresenceField.exe"
  Delete "$COMMONFILES\VST3\PresenceField.vst3"
  RMDir /r "$COMMONFILES\VST3\PresenceField.vst3\Contents"

  Delete "$SMPROGRAMS\Zarlino Audio\PresenceField\PresenceField.lnk"
  Delete "$SMPROGRAMS\Zarlino Audio\PresenceField\Uninstall PresenceField.lnk"
  RMDir "$SMPROGRAMS\Zarlino Audio\PresenceField"
  RMDir "$SMPROGRAMS\Zarlino Audio"

  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioPresenceField"
SectionEnd
