@echo off
rem Builds the bemotion installer and portable zip into dist\ (see tools\build-release.sh).
rem Needs MSYS2 in C:\msys64 with the packages listed in the README.
if not exist C:\msys64\msys2_shell.cmd (
  echo MSYS2 not found in C:\msys64 - see "Getting started" in README.md
  pause
  exit /b 1
)
call C:\msys64\msys2_shell.cmd -ucrt64 -defterm -no-start -here -c "tools/build-release.sh"
pause
