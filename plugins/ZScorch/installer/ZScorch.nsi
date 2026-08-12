; ZScorch v1.0.0 Windows Installer
; NSIS 3.x — Unicode
; Build: makensis /DVERSION=1.0.0 ZScorch.nsi

!ifndef VERSION
  !define VERSION "1.0.0"
!endif

; --- Metadata ---
Name "ZScorch ${VERSION}"
OutFile "ZScorch-v${VERSION}-Windows-Installer.exe"
Unicode True
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES\Zarlino Audio\ZScorch"

; --- UI ---
!include "MUI2.nsh"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; --- Installer Sections ---
Section "ZScorch Plugin" SecMain
  SetOutPath "$INSTDIR"

  ; --- Standalone executable ---
  SetOutPath "$INSTDIR"
  File /nonfatal "..\build\plugins\ZScorch\ZScorch_artefacts\Release\Standalone\ZScorch.exe"

  ; --- VST3 plugin ---
  SetOutPath "$COMMONFILES\VST3"
  File /nonfatal /r "..\build\plugins\ZScorch\ZScorch_artefacts\Release\VST3\ZScorch.vst3\Contents\x86_64-win\*.vst3"

  SetOutPath "$COMMONFILES\VST3\ZScorch.vst3\Contents"
  File /nonfatal "..\build\plugins\ZScorch\ZScorch_artefacts\Release\VST3\ZScorch.vst3\Contents\moduleinfo.json"

  ; --- Documentation ---
  SetOutPath "$INSTDIR"
  File /nonfatal "..\README.md"

  ; --- Start Menu shortcuts ---
  CreateDirectory "$SMPROGRAMS\Zarlino Audio\ZScorch"
  CreateShortcut "$SMPROGRAMS\Zarlino Audio\ZScorch\ZScorch.lnk" "$INSTDIR\ZScorch.exe"
  CreateShortcut "$SMPROGRAMS\Zarlino Audio\ZScorch\Uninstall ZScorch.lnk" "$INSTDIR\Uninstall.exe"

  ; --- Write uninstaller ---
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; --- Registry for Add/Remove Programs ---
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioZScorch" \
                   "DisplayName" "ZScorch — Adaptive Harmonic Processor"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioZScorch" \
                   "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioZScorch" \
                   "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioZScorch" \
                   "Publisher" "Zarlino Audio"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioZScorch" \
                   "URLInfoAbout" "https://zarlinoaudio.com"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioZScorch" \
                     "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioZScorch" \
                     "NoRepair" 1
SectionEnd

; --- Uninstaller Section ---
Section "Uninstall"
  ; Standalone
  Delete "$INSTDIR\ZScorch.exe"
  Delete "$INSTDIR\README.md"

  ; VST3
  Delete "$COMMONFILES\VST3\ZScorch.vst3"
  RMDir /r "$COMMONFILES\VST3\ZScorch.vst3\Contents"

  ; Shortcuts
  Delete "$SMPROGRAMS\Zarlino Audio\ZScorch\ZScorch.lnk"
  Delete "$SMPROGRAMS\Zarlino Audio\ZScorch\Uninstall ZScorch.lnk"
  RMDir "$SMPROGRAMS\Zarlino Audio\ZScorch"
  RMDir "$SMPROGRAMS\Zarlino Audio"

  ; Uninstaller
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  ; Registry
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZarlinoAudioZScorch"
SectionEnd
