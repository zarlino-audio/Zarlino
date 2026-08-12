; ZTame Windows Installer — NSIS Script
; Packages VST3 plugin + Standalone application
;
; Usage: makensis /DVERSION=1.0.0 ZTame.nsi
; Build artifacts must exist at:
;   build/plugins/ZTame/ZTame_artefacts/Release/VST3/ZTame.vst3/
;   build/plugins/ZTame/ZTame_artefacts/Release/Standalone/ZTame.exe

!include "MUI2.nsh"
!include "FileFunc.nsh"

;-------------------------------------------------------------------------------
; Defaults
;-------------------------------------------------------------------------------
!ifndef VERSION
  !define VERSION "1.0.0"
!endif

!define PRODUCT_NAME "ZTame"
!define PUBLISHER "Zarlino Audio"
!define PUBLISHER_URL "https://zarlinoaudio.local"
!define INSTALL_DIR "$PROGRAMFILES\${PUBLISHER}\${PRODUCT_NAME}"
!define VST3_DIR "$COMMONFILES\VST3"
!define REG_KEY "Software\${PUBLISHER}\${PRODUCT_NAME}"

Name "${PRODUCT_NAME} v${VERSION}"
OutFile "ZTame-v${VERSION}-Windows-Installer.exe"
InstallDir "${INSTALL_DIR}"
RequestExecutionLevel admin

;-------------------------------------------------------------------------------
; Interface
;-------------------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${PRODUCT_NAME}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_NAME} Standalone"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;-------------------------------------------------------------------------------
; Artifact path helper — from CI or local build
;-------------------------------------------------------------------------------
!define ARTEFACTS "..\build\plugins\ZTame\ZTame_artefacts\Release"

;-------------------------------------------------------------------------------
; Install
;-------------------------------------------------------------------------------
Section "Install"

  ; --- VST3 plugin ---
  CreateDirectory "${VST3_DIR}\${PRODUCT_NAME}.vst3\Contents\x86_64-win"
  SetOutPath "${VST3_DIR}\${PRODUCT_NAME}.vst3\Contents\x86_64-win"
  File /nonfatal "${ARTEFACTS}\VST3\${PRODUCT_NAME}.vst3\Contents\x86_64-win\*.vst3"

  SetOutPath "${VST3_DIR}\${PRODUCT_NAME}.vst3\Contents"
  File /nonfatal "${ARTEFACTS}\VST3\${PRODUCT_NAME}.vst3\Contents\moduleinfo.json"

  ; --- Standalone application ---
  SetOutPath "$INSTDIR"
  File /nonfatal "${ARTEFACTS}\Standalone\${PRODUCT_NAME}.exe"

  ; --- Documentation ---
  File /nonfatal "..\README.md"

  ; --- Write uninstaller ---
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; --- Registry ---
  WriteRegStr HKLM "${REG_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${REG_KEY}" "Version" "${VERSION}"
  WriteRegStr HKLM "${REG_KEY}" "Publisher" "${PUBLISHER}"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
    "DisplayName" "${PRODUCT_NAME} v${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
    "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
    "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
    "URLInfoAbout" "${PUBLISHER_URL}"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
    "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
    "NoRepair" 1

  ; --- Start Menu ---
  CreateDirectory "$SMPROGRAMS\${PUBLISHER}"
  CreateShortCut "$SMPROGRAMS\${PUBLISHER}\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_NAME}.exe"
  CreateShortCut "$SMPROGRAMS\${PUBLISHER}\Uninstall ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  ; --- Estimated size ---
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
    "EstimatedSize" "$0"

SectionEnd

;-------------------------------------------------------------------------------
; Uninstall
;-------------------------------------------------------------------------------
Section "Uninstall"

  RMDir /r "${VST3_DIR}\${PRODUCT_NAME}.vst3"

  Delete "$INSTDIR\${PRODUCT_NAME}.exe"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${PUBLISHER}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PUBLISHER}\Uninstall ${PRODUCT_NAME}.lnk"
  RMDir "$SMPROGRAMS\${PUBLISHER}"

  DeleteRegKey HKLM "${REG_KEY}"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

SectionEnd
