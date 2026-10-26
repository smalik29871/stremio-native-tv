; Cuelight for Windows: the installer (tools/windows.sh builds it with NSIS).
;   makensis -DVERSION=0.10.80 -DAPPDIR=build-win/Cuelight -DOUTFILE=Cuelight-0.10.80-windows-setup.exe installer.nsi
; It installs for the person running it, under their own folder (no administrator asked):
; a Start menu entry, a desktop shortcut, and an entry in Settings > Apps to remove it.
; The app updates itself by running it as  setup.exe /S /RELAUNCH : it waits for the app to
; close, puts the new version in place and opens it again. Sign-in, settings and progress
; are in %APPDATA%\stremio\nativetv, not here, so an update or an uninstall leaves them.

Target amd64-unicode   ; the app is 64-bit only, so its installer is too
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifndef VERSION
  !error "VERSION, APPDIR and OUTFILE are needed (see the top of this file)"
!endif
!ifndef APPDIR
  !error "VERSION, APPDIR and OUTFILE are needed (see the top of this file)"
!endif
!ifndef OUTFILE
  !error "VERSION, APPDIR and OUTFILE are needed (see the top of this file)"
!endif
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Cuelight"

Name "Cuelight"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Cuelight"
InstallDirRegKey HKCU "Software\Cuelight" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "Cuelight ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Cuelight"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "Cuelight setup"
VIAddVersionKey "LegalCopyright" "All rights reserved"

!define MUI_ICON "cuelight.ico"   ; makensis works in this folder
!define MUI_UNICON "cuelight.ico"   ; makensis works in this folder
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\Cuelight.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Cuelight"
!insertmacro MUI_PAGE_LICENSE "${APPDIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var Relaunch

Function .onInit
  StrCpy $Relaunch 0
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/RELAUNCH" $1
  ${IfNot} ${Errors}
    StrCpy $Relaunch 1
  ${EndIf}
FunctionEnd

; The app that started an update closes itself: wait for its file to be free (up to 30 s).
Function WaitForApp
  StrCpy $2 0
  ${DoWhile} ${FileExists} "$INSTDIR\Cuelight.exe"
    ClearErrors
    FileOpen $3 "$INSTDIR\Cuelight.exe" a
    ${IfNot} ${Errors}
      FileClose $3
      ${Break}
    ${EndIf}
    IntOp $2 $2 + 1
    ${If} $2 >= 60
      ${Break}
    ${EndIf}
    Sleep 500
  ${Loop}
FunctionEnd

Section "Cuelight"
  SetOutPath "$INSTDIR"
  Call WaitForApp
  RMDir /r "$INSTDIR\assets"   ; the old version's, so nothing stale stays
  File /r "${APPDIR}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateShortCut "$SMPROGRAMS\Cuelight.lnk" "$INSTDIR\Cuelight.exe"
  ; the desktop shortcut on a first install; an update keeps it only if it is still there
  ${IfNot} ${Silent}
  ${OrIf} ${FileExists} "$DESKTOP\Cuelight.lnk"
    CreateShortCut "$DESKTOP\Cuelight.lnk" "$INSTDIR\Cuelight.exe"
  ${EndIf}

  WriteRegStr HKCU "Software\Cuelight" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "Cuelight"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "Cuelight"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\Cuelight.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" $0

  ${If} $Relaunch == 1
    Exec '"$INSTDIR\Cuelight.exe"'
  ${EndIf}
SectionEnd

; Only what the installer put there: a folder chosen by hand may hold other things.
Section "Uninstall"
  Delete "$INSTDIR\Cuelight.exe"
  Delete "$INSTDIR\torrentd.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\THIRD-PARTY-NOTICES.md"
  RMDir /r "$INSTDIR\assets"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\Cuelight.lnk"
  Delete "$DESKTOP\Cuelight.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\Cuelight"
SectionEnd
