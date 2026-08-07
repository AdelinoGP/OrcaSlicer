; NSIS spec for OrcaSlicer (pnp_gui) — driven by `xmake pack -f nsis`
; (xpack("OrcaSlicer") in xmake.lua, via set_specfile).
;
; WHY A PROJECT-OWNED TEMPLATE INSTEAD OF xmake's STOCK ONE
; (xmake/scripts/xpack/nsis/makensis.nsi):
;   * stock has no CreateShortCut at all; CPack shipped a Start Menu entry
;     (CPACK_PACKAGE_EXECUTABLES) and a desktop link (CPACK_CREATE_DESKTOP_LINKS)
;   * stock has an unconditional "Add to PATH" section; CPack deliberately left
;     CPACK_NSIS_MODIFY_PATH commented out
;   * stock is per-user (RequestExecutionLevel user) and !includes UAC.nsh, a
;     third-party plugin that stock NSIS does not ship; CPack installs per-machine
;   * stock has no uninstall-before-install (CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL)
;   * stock sets MUI_ICON only; CPack set MUI_UNIICON too
;   * stock writes Publisher from the copyright string; CPack wrote the vendor
;   * stock emits VIProductVersion "<PACKAGE_VERSION>.0" — with our 2.5.0-pnp
;     version that is "2.5.0-pnp.0", which makensis rejects outright (the field
;     must be four numeric components). CPack already split these: numeric
;     MAJOR/MINOR/PATCH for the version resources, the full "2.5.0-pnp" only in
;     the output filename. This file mirrors that split.
;
; The helper functions (TrimQuote / RM*IfExists / RMEmptyParentDirs) are kept
; verbatim from the stock template because the nsis backend GENERATES calls to
; them: plugins/pack/nsis/main.lua:132-143 turns every batchcmds `rm`/`rmdir`
; into RMFileIfExists / unRMDirIfExists / RMEmptyParentDirs macro calls.
; Removing them breaks the generated uninstall commands.
;
; CAUTION: xmake substitutes PACKAGE_* variables by plain text scan
; (main.lua:283-305) with NO awareness of NSIS syntax — a mention inside a `;`
; comment is expanded just the same. Never write a live PACKAGE_* name in
; braces in prose here; use angle brackets, as above. Names xmake does not know
; are left untouched (main.lua:302-305 returns nil), so NSIS's own defines and
; LogicLib macros pass through safely.

!include "MUI2.nsh"
!include "WordFunc.nsh"
!include "WinMessages.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

; ---------------------------------------------------------------- identity

!define VERSION      "${PACKAGE_VERSION}"
!if "${PACKAGE_VERSION_BUILD}" != ""
!define VERSION_FULL "${PACKAGE_VERSION}-${PACKAGE_VERSION_BUILD}"
!else
!define VERSION_FULL "${PACKAGE_VERSION}"
!endif

; CPACK_PACKAGE_INSTALL_DIRECTORY was CPACK_PACKAGE_NAME, i.e. "OrcaSlicer"
!define INSTALL_DIRNAME "${PACKAGE_NAME}"
; CPACK_PACKAGE_INSTALL_REGISTRY_KEY "OrcaSlicer"
!define RegUninstall "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PACKAGE_NAME}"
; CPACK_PACKAGE_EXECUTABLES "orca-slicer;OrcaSlicer" / CPACK_CREATE_DESKTOP_LINKS
!define APP_EXE  "orca-slicer.exe"
!define APP_LINK "OrcaSlicer"

Name "${PACKAGE_NAME} - v${VERSION_FULL}"
OutFile "${PACKAGE_OUTPUTFILE}"
!cd "${PACKAGE_WORKDIR}"

Unicode true
SetCompressor /FINAL /SOLID lzma
SetCompressorDictSize 64
SetDatablockOptimize ON

; Per-machine install under Program Files, as CPack's NSIS generator does.
RequestExecutionLevel admin
ManifestDPIAware true

; makensis emits a 32-bit installer even for a 64-bit payload, so plain HKLM
; writes land under WOW6432Node unless the registry view is switched. Verified:
; without the SetRegView below, the uninstall entry is written to
; HKLM\SOFTWARE\WOW6432Node\... instead of the 64-bit view where a 64-bit
; application belongs. The stock xmake template avoided this by using the
; HKLM64 alias from x64.nsh.
!if "${PACKAGE_ARCH}" == "x64"
  !define PROGRAMFILES $PROGRAMFILES64
  !define REGVIEW 64
!else
  !define PROGRAMFILES $PROGRAMFILES
  !define REGVIEW 32
!endif

InstallDir "${PROGRAMFILES}\${INSTALL_DIRNAME}"
; NOT InstallDirRegKey: it is evaluated before .onInit, i.e. before SetRegView,
; so on x64 it would read the 32-bit view we do not write to. .onInit does the
; equivalent lookup below, after the view is switched.

; CPACK_NSIS_MUI_ICON / CPACK_NSIS_MUI_UNIICON both pointed at OrcaSlicer.ico
!if "${PACKAGE_ICONFILE}" != ""
  !define MUI_ICON   "${PACKAGE_ICONFILE}"
  !define MUI_UNICON "${PACKAGE_ICONFILE}"
!endif

; ------------------------------------------------------------------- pages

!define MUI_FINISHPAGE_RUN "$InstDir\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APP_LINK}"

!insertmacro MUI_PAGE_WELCOME
!if "${PACKAGE_LICENSEFILE}" != ""
  !insertmacro MUI_PAGE_LICENSE "${PACKAGE_LICENSEFILE}"
!endif
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; --------------------------------------------------------- version resource

; Strictly numeric: makensis rejects anything else here. See header note.
VIProductVersion                         "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_ALTER}.0"
VIFileVersion                            "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_ALTER}.0"
VIAddVersionKey /LANG=0 ProductName      "${PACKAGE_NAME}"
VIAddVersionKey /LANG=0 Comments         "${PACKAGE_DESCRIPTION}"
VIAddVersionKey /LANG=0 CompanyName      "${PACKAGE_COMPANY}"
VIAddVersionKey /LANG=0 LegalCopyright   "${PACKAGE_COPYRIGHT}"
VIAddVersionKey /LANG=0 FileDescription  "${PACKAGE_NAME} Installer - v${VERSION_FULL}"
VIAddVersionKey /LANG=0 OriginalFilename "${PACKAGE_FILENAME}"
VIAddVersionKey /LANG=0 FileVersion      "${VERSION_FULL}"
VIAddVersionKey /LANG=0 ProductVersion   "${VERSION_FULL}"

; -------------------------------------------------------- helper functions
; Required by the generated uninstall commands — see the CAUTION in the header.

Function TrimQuote
  Exch $R1
  Push $R2
Loop:
  StrCpy $R2 "$R1" 1
  StrCmp "$R2" "'"   TrimLeft
  StrCmp "$R2" "$\"" TrimLeft
  StrCmp "$R2" "$\r" TrimLeft
  StrCmp "$R2" "$\n" TrimLeft
  StrCmp "$R2" "$\t" TrimLeft
  StrCmp "$R2" " "   TrimLeft
  GoTo Loop2
TrimLeft:
  StrCpy $R1 "$R1" "" 1
  Goto Loop
Loop2:
  StrCpy $R2 "$R1" 1 -1
  StrCmp "$R2" "'"   TrimRight
  StrCmp "$R2" "$\"" TrimRight
  StrCmp "$R2" "$\r" TrimRight
  StrCmp "$R2" "$\n" TrimRight
  StrCmp "$R2" "$\t" TrimRight
  StrCmp "$R2" " "   TrimRight
  GoTo Done
TrimRight:
  StrCpy $R1 "$R1" -1
  Goto Loop2
Done:
  Pop $R2
  Exch $R1
FunctionEnd

Function RMDirIfExists
!define RMDirIfExists '!insertmacro RMDirIfExistsCall'
!macro RMDirIfExistsCall _PATH
  push '${_PATH}'
  Call RMDirIfExists
!macroend
  Exch $0
  IfFileExists "$0" 0 fileDoesNotExist
  RMDir /r "$0"
  fileDoesNotExist:
FunctionEnd

Function un.RMDirIfExists
!define unRMDirIfExists '!insertmacro unRMDirIfExistsCall'
!macro unRMDirIfExistsCall _PATH
  push '${_PATH}'
  Call un.RMDirIfExists
!macroend
  Exch $0
  IfFileExists "$0" 0 fileDoesNotExist
  RMDir /r "$0"
  fileDoesNotExist:
FunctionEnd

Function RMFileIfExists
!define RMFileIfExists '!insertmacro RMFileIfExistsCall'
!macro RMFileIfExistsCall _PATH
  push '${_PATH}'
  Call RMFileIfExists
!macroend
  Exch $0
  IfFileExists "$0" 0 fileDoesNotExist
  Delete "$0"
  fileDoesNotExist:
FunctionEnd

Function un.RMFileIfExists
!define unRMFileIfExists '!insertmacro unRMFileIfExistsCall'
!macro unRMFileIfExistsCall _PATH
  push '${_PATH}'
  Call un.RMFileIfExists
!macroend
  Exch $0
  IfFileExists "$0" 0 fileDoesNotExist
  Delete "$0"
  fileDoesNotExist:
FunctionEnd

Function RMEmptyParentDirs
!define RMEmptyParentDirs '!insertmacro RMEmptyParentDirsCall'
!macro RMEmptyParentDirsCall _PATH
  push '${_PATH}'
  Call RMEmptyParentDirs
!macroend
  ClearErrors
  Exch $0
  RMDir "$0\.."
  IfErrors Skip
  ${RMEmptyParentDirs} "$0\.."
  Skip:
  Pop $0
FunctionEnd

Function un.RMEmptyParentDirs
!define unRMEmptyParentDirs '!insertmacro unRMEmptyParentDirsCall'
!macro unRMEmptyParentDirsCall _PATH
  push '${_PATH}'
  Call un.RMEmptyParentDirs
!macroend
  ClearErrors
  Exch $0
  RMDir "$0\.."
  IfErrors Skip
  ${unRMEmptyParentDirs} "$0\.."
  Skip:
  Pop $0
FunctionEnd

; --------------------------------------------------------------- installer

Function .onInit
  SetShellVarContext all
  SetRegView ${REGVIEW}

  ; CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON — run the previous
  ; uninstaller first so a stale tree can't shadow the new one. _?= keeps the
  ; uninstaller synchronous; the copy it spawns in $TEMP is cleaned up after.
  ReadRegStr $R0 HKLM "${RegUninstall}" "UninstallString"
  ${If} $R0 != ""
    ReadRegStr $R1 HKLM "${RegUninstall}" "InstallLocation"
    Push $R1
    Call TrimQuote
    Pop  $R1
    MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
      "${PACKAGE_NAME} is already installed. It will be uninstalled before continuing." \
      /SD IDOK IDOK uninst
    Abort
  uninst:
    ClearErrors
    ExecWait '$R0 /S _?=$R1'
    Delete "$R1\uninstall.exe"
    RMDir "$R1"

    ; Reinstall where the previous version lived, unless the caller picked a
    ; directory explicitly (/D= is applied before .onInit runs, so $InstDir
    ; still holding the compiled-in default means it was not overridden).
    ${If} $R1 != ""
    ${AndIf} $InstDir == "${PROGRAMFILES}\${INSTALL_DIRNAME}"
      StrCpy $InstDir $R1
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "${PACKAGE_NAME}" InstallExecutable

  SectionIn RO
  SetOutPath $InstDir

  ; File/SetOutPath commands generated from the xpack install batchcmds
  ${PACKAGE_INSTALLCMDS}

  ; shortcuts must be created after the payload lands
  SetOutPath $InstDir
  CreateDirectory "$SMPROGRAMS\${INSTALL_DIRNAME}"
  CreateShortCut "$SMPROGRAMS\${INSTALL_DIRNAME}\${APP_LINK}.lnk" "$InstDir\${APP_EXE}"
  CreateShortCut "$DESKTOP\${APP_LINK}.lnk" "$InstDir\${APP_EXE}"

  WriteUninstaller "$InstDir\uninstall.exe"

  WriteRegStr   HKLM "${RegUninstall}" "DisplayName"          "${PACKAGE_TITLE}"
  !if "${PACKAGE_NSIS_DISPLAY_ICON}" != ""
  WriteRegStr   HKLM "${RegUninstall}" "DisplayIcon"          '"${PACKAGE_NSIS_DISPLAY_ICON}"'
  !endif
  WriteRegStr   HKLM "${RegUninstall}" "Comments"             "${PACKAGE_DESCRIPTION}"
  ; CPack wrote CPACK_PACKAGE_VENDOR here, not the copyright string
  WriteRegStr   HKLM "${RegUninstall}" "Publisher"            "${PACKAGE_COMPANY}"
  WriteRegStr   HKLM "${RegUninstall}" "UninstallString"      '"$InstDir\uninstall.exe"'
  WriteRegStr   HKLM "${RegUninstall}" "QuietUninstallString" '"$InstDir\uninstall.exe" /S'
  WriteRegStr   HKLM "${RegUninstall}" "InstallLocation"      "$InstDir"
  WriteRegStr   HKLM "${RegUninstall}" "HelpLink"             "${PACKAGE_HOMEPAGE}"
  WriteRegStr   HKLM "${RegUninstall}" "URLInfoAbout"         "${PACKAGE_HOMEPAGE}"
  WriteRegStr   HKLM "${RegUninstall}" "URLUpdateInfo"        "${PACKAGE_HOMEPAGE}"
  WriteRegDWORD HKLM "${RegUninstall}" "VersionMajor"         ${PACKAGE_VERSION_MAJOR}
  WriteRegDWORD HKLM "${RegUninstall}" "VersionMinor"         ${PACKAGE_VERSION_MINOR}
  WriteRegStr   HKLM "${RegUninstall}" "DisplayVersion"       "${VERSION_FULL}"
  WriteRegDWORD HKLM "${RegUninstall}" "NoModify"             1
  WriteRegDWORD HKLM "${RegUninstall}" "NoRepair"             1

  ${GetSize} "$InstDir" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${RegUninstall}" "EstimatedSize" "$0"
SectionEnd

; ------------------------------------------------------------- uninstaller

Function un.onInit
  SetShellVarContext all
  SetRegView ${REGVIEW}
FunctionEnd

Section "Uninstall"

  Delete "$SMPROGRAMS\${INSTALL_DIRNAME}\${APP_LINK}.lnk"
  RMDir  "$SMPROGRAMS\${INSTALL_DIRNAME}"
  Delete "$DESKTOP\${APP_LINK}.lnk"

  ; rm/rmdir commands generated from the xpack uninstall batchcmds
  ${PACKAGE_UNINSTALLCMDS}

  DeleteRegKey HKLM "${RegUninstall}"

  ${unRMFileIfExists} "$InstDir\uninstall.exe"
  RMDir "$InstDir"
SectionEnd
