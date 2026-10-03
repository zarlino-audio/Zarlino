;==============================================================================
;  ZTame — Windows installer (NSIS 3.x, 64-bit only)
;
;  Build from this directory:
;      makensis /DVERSION=1.2.0 ZTame.nsi
;
;  Produces: ZTame-v<VERSION>-Windows-Installer.exe
;  The release workflow (.github/workflows/ztame-release-gate.yml) builds and
;  signs exactly this script, so anything referenced here must exist after a
;  Release build of the ZTame and ZTame_All targets.
;
;  What it installs:
;    * VST3 bundle  -> %COMMONFILES%\VST3\ZTame.vst3   (64-bit hosts)
;    * Standalone   -> %PROGRAMFILES%\ZTame\ZTame.exe
;    * Uninstaller, Start-menu entries, Add/Remove Programs entry
;
;  Notes for maintainers
;    * Save this file as UTF-8 *with* a BOM: NSIS 3.x only reads a script as
;      UTF-8 when the BOM is present (see README.md in the repository root).
;    * A previous ZTame.vst3 bundle is deleted before the payload is written.
;      Leaving old files behind produces a mixed-version bundle, which hosts
;      such as Cubase and Studio One reject.
;    * The VST3 bundle is installed with `File /r` from the build tree, so the
;      installer follows whatever Contents/ layout JUCE generates instead of
;      hard-coding the module and moduleinfo.json paths.
;==============================================================================

!ifndef VERSION
    !define VERSION "1.2.0"
!endif

!define PRODUCT     "ZTame"
!define PUBLISHER   "Zarlino Audio"
!define REGKEY      "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

; ---- Build tree (relative to this script) -----------------------------------
!define SRC_VST3  "..\..\..\build\plugins\ZTame\ZTame_artefacts\Release\VST3\ZTame.vst3\Contents"
!define SRC_APP   "..\..\..\build\plugins\ZTame\ZTame_artefacts\Release\Standalone\ZTame.exe"
!define SRC_README "..\..\README.md"

; ---- Install targets --------------------------------------------------------
; The VST3 bundle path is also the path a stray module from an older, unpacked
; distribution would occupy, so removing it covers both cases.
!define VST3_BUNDLE  "$COMMONFILES64\VST3\ZTame.vst3"

Name "${PRODUCT} ${VERSION}"
OutFile "ZTame-v${VERSION}-Windows-Installer.exe"
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
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
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
        !error "${LABEL} not found: ${FILEPATH} — build the ZTame_All target first."
    !endif
!macroend

!insertmacro CheckPayload "${SRC_APP}"     "Standalone executable"
!insertmacro CheckPayload "${SRC_VST3}"    "VST3 bundle contents"

;==============================================================================
Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "${PRODUCT} is a 64-bit plugin and cannot be installed on 32-bit Windows."
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
    Delete "$INSTDIR\ZTame.exe"
    File "/oname=ZTame.exe" "${SRC_APP}"

    ; Ship the plugin's own read-me when the working copy has one.
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
    WriteRegStr   HKLM "${REGKEY}" "DisplayIcon"     "$INSTDIR\ZTame.exe"
    WriteRegStr   HKLM "${REGKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1
SectionEnd

;==============================================================================
Section "Start menu shortcuts" SEC_STARTMENU
    CreateDirectory "$SMPROGRAMS\${PUBLISHER}"
    CreateShortcut  "$SMPROGRAMS\${PUBLISHER}\${PRODUCT}.lnk" "$INSTDIR\ZTame.exe"
    CreateShortcut  "$SMPROGRAMS\${PUBLISHER}\Uninstall ${PRODUCT}.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "Desktop shortcut" SEC_DESKTOP
    CreateShortcut "$DESKTOP\${PRODUCT}.lnk" "$INSTDIR\ZTame.exe"
SectionEnd

;==============================================================================
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_VST3}      "Installs the ZTame VST3 plug-in for 64-bit hosts (Cubase, Studio One, Reaper, Ableton Live, FL Studio, ...)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_APP}       "Installs the standalone ZTame application."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_STARTMENU} "Adds ZTame to the Start menu."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP}   "Adds a ZTame shortcut to the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;==============================================================================
Function un.onInit
    SetRegView 64
FunctionEnd

Section "Uninstall"
    ; Remove the bundle whole: deleting the folder is the only way to be sure no
    ; module of another version is left behind.
    RMDir /r "${VST3_BUNDLE}"

    Delete "$INSTDIR\ZTame.exe"
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
