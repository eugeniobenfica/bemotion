; bemotion installer for Windows (NSIS 3). Built by tools/package-windows.sh:
;   makensis /DSOURCE=<deployed folder> /DVERSION=0.1.0 /DFULLVERSION=0.1.0-beta.2 /DOUTFILE=<setup.exe> bemotion.nsi
;
; bemotion's updater runs it as `setup.exe /S /RELAUNCH`: quietly, waiting for bemotion to
; close, then opening it again.
;
; Installs for the current user (no administrator rights needed) into
; %LOCALAPPDATA%\Programs\bemotion, adds Start Menu (and optionally desktop)
; shortcuts, opens .bemotion files with bemotion, and registers an uninstaller
; in Settings > Apps. Settings and projects are never touched by the uninstaller.

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
ManifestDPIAware true

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef SOURCE
  !error "Pass /DSOURCE=<folder made by tools/deploy-windows.sh>"
!endif
!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef FULLVERSION ; with its pre-release part (0.1.0-beta.2); VERSION stays numeric for Windows
  !define FULLVERSION "${VERSION}"
!endif
!ifndef OUTFILE
  !define OUTFILE "bemotion-${FULLVERSION}-setup.exe"
!endif

!define APP "bemotion"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"
!define PROGID "bemotion.project"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "Software\${APP}" "InstallDir"
BrandingText "${APP} ${FULLVERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "ProductVersion" "${FULLVERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "LegalCopyright" "The bemotion contributors (MIT License)"

!define MUI_ICON "..\..\assets\branding\bemotion.ico"
!define MUI_UNICON "..\..\assets\branding\bemotion.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\bemotion.exe"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME_TEXT "$(DesktopShortcut)"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION CreateDesktopShortcut

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "PortugueseBR"

LangString DesktopShortcut ${LANG_ENGLISH} "Create a desktop shortcut"
LangString DesktopShortcut ${LANG_PORTUGUESEBR} "Criar atalho na área de trabalho"
LangString StillRunning ${LANG_ENGLISH} "bemotion is running. Close it, then click OK to continue."
LangString StillRunning ${LANG_PORTUGUESEBR} "O bemotion está aberto. Feche-o e clique em OK para continuar."
LangString ProjectType ${LANG_ENGLISH} "bemotion Project"
LangString ProjectType ${LANG_PORTUGUESEBR} "Projeto do bemotion"

; The program's files can't be replaced while it runs. An update starts while bemotion is
; closing: give it a few seconds before asking.
!macro WaitUntilClosed
  StrCpy $1 0
  retry:
    IfFileExists "$INSTDIR\bemotion.exe" 0 closed
    ClearErrors
    FileOpen $0 "$INSTDIR\bemotion.exe" a
    IfErrors locked
    FileClose $0
    Goto closed
  locked:
    IntOp $1 $1 + 1
    IntCmp $1 20 ask 0 ask
    Sleep 500
    Goto retry
  ask:
    MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION "$(StillRunning)" IDOK retry
    Abort
  closed:
!macroend

; The language follows Windows (English or Brazilian Portuguese).

; After an update from bemotion (`/RELAUNCH`), bemotion opens again.
Function .onInstSuccess
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/RELAUNCH" $1
  IfErrors +2
    Exec '"$INSTDIR\bemotion.exe"'
FunctionEnd

Function CreateDesktopShortcut
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\bemotion.exe"
FunctionEnd

Section "bemotion" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  !insertmacro WaitUntilClosed
  ; An update replaces the program completely (files of older versions do not linger).
  IfFileExists "$INSTDIR\bemotion.exe" 0 +3
    RMDir /r "$INSTDIR\assets"
    Delete "$INSTDIR\*.dll"
  File /r "${SOURCE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\bemotion.exe"

  ; Double-clicking a .bemotion file opens it in bemotion.
  WriteRegStr HKCU "Software\Classes\.bemotion" "" "${PROGID}"
  WriteRegStr HKCU "Software\Classes\${PROGID}" "" "$(ProjectType)"
  WriteRegStr HKCU "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\bemotion.exe,0"
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\bemotion.exe" "%1"'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  ; Settings > Apps.
  WriteRegStr HKCU "Software\${APP}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${FULLVERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "The bemotion contributors"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/eugeniobenfica/bemotion"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\bemotion.exe,0"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Uninstall"
  !insertmacro WaitUntilClosed
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$DESKTOP\${APP}.lnk"
  ; Only the program: settings (%APPDATA%\bemotion) and projects stay.
  IfFileExists "$INSTDIR\bemotion.exe" 0 +2
    RMDir /r "$INSTDIR"

  ReadRegStr $0 HKCU "Software\Classes\.bemotion" ""
  StrCmp $0 "${PROGID}" 0 +2
    DeleteRegKey HKCU "Software\Classes\.bemotion"
  DeleteRegKey HKCU "Software\Classes\${PROGID}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "Software\${APP}"
SectionEnd
