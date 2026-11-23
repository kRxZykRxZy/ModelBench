!define APPNAME "ModelBench"
!define APPVER "0.1.0"
!define STAGE_DIR "dist\windows\ModelBench"

OutFile "dist\windows\ModelBench-Setup.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
RequestExecutionLevel admin

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*.*"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\ModelBench.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"
  Delete "$DESKTOP\${APPNAME}.lnk"
SectionEnd