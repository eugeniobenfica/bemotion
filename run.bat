@echo off
rem Runs bemotion from this folder without a release build:
rem   run.bat                   rebuild what changed (seconds after the first time), then open bemotion
rem   run.bat project.bemotion  ... and open that project
rem   run.bat --no-build        just open the last build
rem   run.bat --build-only      rebuild, do not open
rem Needs MSYS2 in C:\msys64 with the packages listed in the README, and Skia built once
rem (tools\skia\build-skia.sh): looked for in SKIA_ROOT, third_party\skia, then %USERPROFILE%\bemotion-deps\skia.
setlocal EnableDelayedExpansion
set "HERE=%~dp0"
set "BUILD=%HERE%build"
set "MSYS=C:\msys64\ucrt64\bin"
if not exist "%MSYS%\cmake.exe" (
  echo MSYS2 UCRT64 with cmake not found in C:\msys64 - see "Getting started" in README.md
  pause
  exit /b 1
)
rem Qt, FFmpeg and the compiler come from MSYS2.
set "PATH=%MSYS%;%PATH%"

set "BUILDNOW=1"
set "OPEN=1"
set "ARGS="
for %%A in (%*) do (
  if /i "%%~A"=="--no-build" (set "BUILDNOW=0") else if /i "%%~A"=="--build-only" (set "OPEN=0") else set "ARGS=!ARGS! "%%~A""
)

if "%BUILDNOW%"=="1" (
  rem A build folder made for another location of the sources (the folder was moved): start it over.
  set "SRC=%HERE:~0,-1%"
  set "SRC=!SRC:\=/!"
  if exist "%BUILD%\CMakeCache.txt" (
    findstr /x /c:"CMAKE_HOME_DIRECTORY:INTERNAL=!SRC!" "%BUILD%\CMakeCache.txt" >nul || (
      echo The build folder belongs to an older location of the sources: configuring it again.
      rmdir /s /q "%BUILD%"
    )
  )
  if not exist "%BUILD%\build.ninja" (
    set "SKIA="
    for %%S in ("%SKIA_ROOT%" "%HERE%third_party\skia" "%USERPROFILE%\bemotion-deps\skia") do (
      if not defined SKIA if exist "%%~S\lib\libskia.a" set "SKIA=%%~S"
    )
    if not defined SKIA (
      echo Skia not found: build it once with tools\skia\build-skia.sh, or set SKIA_ROOT.
      pause
      exit /b 1
    )
    set "SKIA=!SKIA:\=/!"
    echo Configuring build ^(Skia: !SKIA!^)...
    cmake -S "%HERE:~0,-1%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSKIA_ROOT="!SKIA!" >nul || (
      echo Configuring failed.
      pause
      exit /b 1
    )
  )
  echo Building what changed...
  cmake --build "%BUILD%" || (
    echo The build failed - see the messages above.
    pause
    exit /b 1
  )
)

if "%OPEN%"=="1" (
  if not exist "%BUILD%\bemotion.exe" (
    echo No build yet: run run.bat without --no-build first.
    pause
    exit /b 1
  )
  start "" "%BUILD%\bemotion.exe" %ARGS%
)
exit /b 0
