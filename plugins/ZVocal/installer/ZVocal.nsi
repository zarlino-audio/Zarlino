;==============================================================================
;  ZVocal - Windows installer (NSIS 3.x, 64-bit only)
;
;  Build from this directory:
;      makensis /DVERSION=0.2.0 ZVocal.nsi
;
;  Produces: ZVocal-v<VERSION>-Windows-Installer.exe
;
;  What it installs:
;    * VST3 bundle  -> %COMMONFILES64%\VST3\ZVocal.vst3   (64-bit hosts)
;    * Standalone   -> %PROGRAMFILES64%\ZVocal\ZVocal.exe
;    * Uninstaller, Start-menu entries, Add/Remove Programs entry
;
;  Notes for maintainers
;    * This file must be saved as UTF-8 *with* a BOM: NSIS 3.x only reads a
;      script as UTF-8 when the BOM is present (see README.md in the repo root).
;      It is kept deliberately ASCII-only as well, so a missing BOM cannot
;      corrupt anything in the installer UI.
;    * A previous ZVocal.vst3 bundle is deleted before the payload is written.
;      Leaving old files behind produces a mixed-version bundle, which hosts
;      such as Cubase and Studio One reject.
;    * The VST3 bundle is installed with `File /r` from the build tree, so the
;      installer follows whatever Contents/ layout JUCE generates instead of
;      hard-coding the module and moduleinfo.json paths.
;    * Build the ZVocal_All target before compiling this script - the checks
;      below fail the build with an explanatory message if the payload is
;      missing, rather than producing an installer that installs nothing.
;==============================================================================

!ifndef VERSION
    !define VERSION "0.2.0"
!endif

!define PRODUCT     "ZVocal"
!define PUBLISHER   "Zarlino Audio"
!define REGKEY      "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

; ---- Build tree (relative to this script) -----------------------------------
!define SRC_VST3    "..\..\..\build\plugins\ZVocal\ZVocal_artefacts\Release\VST3\ZVocal.vst3\Contents"
!define SRC_APP     "..\..\..\build\plugins\ZVocal\ZVocal_artefacts\Release\Standalone\ZVocal.exe"
!define SRC_README  "..\..\README.md"

; ---- Install targets --------------------------------------------------------
; The VST3 bundle path is also the path a stray module from an older, unpacked
; distribution would occupy, so removing it covers both cases.
!define VST3_BUNDLE "$COMMONFILES64\VST3\ZVocal.vst3"

Name "${PRODUCT} ${VERSION}"
OutFile "ZVocal-v${VERSION}-Windows-Installer.exe"
Unicode true
InstallDir "$PROGRAMFILES64\${PRODUCT}"
InstallDirRegKey HKLM "${REGKEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${PUBLISHER}"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_NOAUTOCLOSE

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;==============================================================================
;  Sanity: the payload has to be built before the installer can embed it.
;==============================================================================
!macro CheckPayload FILEPATH LABEL
    !if /FILEEXISTS "${FILEPATH}"
    !else
        !error "${LABEL} not found: ${FILEPATH} - build the ZVocal_All target first."
    !endif
!macroend

!insertmacro CheckPayload "${SRC_APP}"  "Standalone executable"
!insertmacro CheckPayload "${SRC_VST3}" "VST3 bundle contents"

;==============================================================================
Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "${PRODUCT} is a 64-bit plug-in and cannot be installed on 32-bit Windows."
        Abort
    ${EndIf}

    SetRegView 64
FunctionEnd

;==============================================================================
Section "VST3 plug-in (64-bit hosts)" SEC_VST3
    SectionIn RO

    ; Drop any previous bundle first: a partial overwrite leaves stale files in
    ; a folder that hosts scan as a single unit.
    RMDir /r "${VST3_BUNDLE}"

    SetOutPath "${VST3_BUNDLE}\Contents"
    File /r "${SRC_VST3}\*.*"
SectionEnd

;==============================================================================
Section "Standalone application" SEC_APP
    SectionIn RO

    SetOutPath "$INSTDIR"
    Delete "$INSTDIR\ZVocal.exe"
    File "/oname=ZVocal.exe" "${SRC_APP}"

    ; Ship the repository read-me when the working copy has one.
    !if /FILEEXISTS "${SRC_README}"
        Delete "$INSTDIR\README.md"
        File "/oname=README.md" "${SRC_README}"
    !endif

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; --- Add/Remove Programs ------------------------------------------------
    SetRegView 64
    WriteRegStr   HKLM "${REGKEY}" "DisplayName"     "${PRODUCT} ${VERSION}"
    WriteRegStr   HKLM "${REGKEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   HKLM "${REGKEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr   HKLM "${REGKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   HKLM "${REGKEY}" "DisplayIcon"     "$INSTDIR\ZVocal.exe"
    WriteRegStr   HKLM "${REGKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1
SectionEnd

;==============================================================================
Section "Start menu shortcuts" SEC_STARTMENU
    CreateDirectory "$SMPROGRAMS\${PUBLISHER}"
    CreateShortcut  "$SMPROGRAMS\${PUBLISHER}\${PRODUCT}.lnk" "$INSTDIR\ZVocal.exe"
    CreateShortcut  "$SMPROGRAMS\${PUBLISHER}\Uninstall ${PRODUCT}.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "Desktop shortcut" SEC_DESKTOP
    CreateShortcut "$DESKTOP\${PRODUCT}.lnk" "$INSTDIR\ZVocal.exe"
SectionEnd

;==============================================================================
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_VST3}      "Installs the ZVocal VST3 plug-in for 64-bit hosts (Cubase, Studio One, Reaper, Ableton Live, FL Studio, ...)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_APP}       "Installs the standalone ZVocal application."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_STARTMENU} "Adds ZVocal to the Start menu."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP}   "Adds a ZVocal shortcut to the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;==============================================================================
Function un.onInit
    SetRegView 64
FunctionEnd

Section "Uninstall"
    ; Remove the bundle whole: deleting the folder is the only way to be sure no
    ; module of another version is left behind.
    RMDir /r "${VST3_BUNDLE}"

    Delete "$INSTDIR\ZVocal.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir  "$INSTDIR"

    Delete "$SMPROGRAMS\${PUBLISHER}\${PRODUCT}.lnk"
    Delete "$SMPROGRAMS\${PUBLISHER}\Uninstall ${PRODUCT}.lnk"
    RMDir  "$SMPROGRAMS\${PUBLISHER}"
    Delete "$DESKTOP\${PRODUCT}.lnk"

    SetRegView 64
    DeleteRegKey HKLM "${REGKEY}"
SectionEnd
