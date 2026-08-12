; VocalToneShaper v0.1.0 Windows Installer
; NSIS 3.x — Unicode
; Build: makensis /DVERSION=0.1.0 VocalToneShaper.nsi

!ifndef VERSION
  !define VERSION "0.1.0"
!endif

Name "VocalToneShaper ${VERSION}"
OutFile "VocalToneShaper-v${VERSION}-Windows-Installer.exe"
Unicode True
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES\Zarlino Audio\VocalToneShaper"

!include "MUI2.nsh"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "VocalToneShaper Plugin" SecMain
  SetOutPath "$INSTDIR"

  ; --- Standalone executable ---
  File /nonfatal "..\build\plugins\VocalToneShaper\VocalToneShaper_artefacts\Release\Standalone\VocalToneShaper.exe"

  ; --- VST3 plugin ---
  SetOutPath "$COMMONFILES\VST3"
  File /nonfatal /r "..\build\plugins\VocalToneShaper\VocalToneShaper_artefacts\Release\VST3\VocalToneShaper.vst3\Contents\x86_64-win\*.vst3"

  SetOutPath "$COMMONFILES\VST3\VocalToneShaper.vst3\Contents"
  File /nonfatal "..\build\plugins\VocalToneShaper\VocalToneShaper_artefacts\Release\VST3\VocalToneShaper.vst3\Contents\moduleinfo.json"

  ; --- Start Menu shortcuts ---
  CreateDirectory "$SMPROGRAMS\Zarlino Audio\VocalToneShaper"
  CreateShortcut "$SMPROGRAMS\Zarlino Audio\VocalToneShaper\VocalToneShaper.lnk" "$INSTDIR\VocalToneShaper.exe"
  CreateShortcut "$SMPROGRAMS\Zarlino Audio\VocalToneShaper\Uninstall VocalToneShaper.lnk" "$INSTDIR\Uninstall.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioVocalToneShaper" \
                   "DisplayName" "VocalToneShaper — 6-Stage Vocal Processing"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioVocalToneShaper" \
                   "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioVocalToneShaper" \
                   "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioVocalToneShaper" \
                   "Publisher" "Zarlino Audio"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioVocalToneShaper" \
                   "URLInfoAbout" "https://zarlinoaudio.com"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioVocalToneShaper" \
                     "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioVocalToneShaper" \
                     "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\VocalToneShaper.exe"
  Delete "$COMMONFILES\VST3\VocalToneShaper.vst3"
  RMDir /r "$COMMONFILES\VST3\VocalToneShaper.vst3\Contents"

  Delete "$SMPROGRAMS\Zarlino Audio\VocalToneShaper\VocalToneShaper.lnk"
  Delete "$SMPROGRAMS\Zarlino Audio\VocalToneShaper\Uninstall VocalToneShaper.lnk"
  RMDir "$SMPROGRAMS\Zarlino Audio\VocalToneShaper"
  RMDir "$SMPROGRAMS\Zarlino Audio"

  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioVocalToneShaper"
SectionEnd
