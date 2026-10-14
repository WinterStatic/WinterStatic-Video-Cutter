@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "APP_VERSION=0.3.0"

set "RELEASE_MODE=0"
if /I "%~1"=="release" set "RELEASE_MODE=1"
if not "%~1"=="" if "%RELEASE_MODE%"=="0" goto :bad_argument
title WinterStatic Video Cutter - Native Build %APP_VERSION%

echo.
echo ==========================================================
echo   WinterStatic Video Cutter - Native Win32 Build %APP_VERSION%
echo ==========================================================
echo.
echo It compiles the MPV-based cutter frontend with MSVC and packages libmpv.
if "%RELEASE_MODE%"=="1" echo Release mode: matching MSYS2 runtime source will also be collected.
echo.

where cl.exe >nul 2>nul
if not errorlevel 1 goto :compiler_ready

set "VSINSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
)
if not defined VSINSTALL if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
if not defined VSINSTALL goto :no_msvc

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :env_failed

:compiler_ready
where cl.exe >nul 2>nul
if errorlevel 1 goto :no_msvc
where rc.exe >nul 2>nul
if errorlevel 1 goto :no_rc

if not exist "C:\msys64\usr\bin\bash.exe" goto :no_msys
if not exist "C:\msys64\mingw64\bin\libmpv-2.dll" goto :no_libmpv

rem Build into a fresh per-run workspace instead of overwriting the same
rem app.res/EXE in the source directory. This avoids intermittent Windows,
rem antivirus, or stale-process file locks breaking otherwise valid builds.
set "BUILD_WORK=%TEMP%\WinterStatic-Cutter-build-%RANDOM%-%RANDOM%"
mkdir "%BUILD_WORK%" >nul 2>nul
if errorlevel 1 goto :build_workspace_failed
if not exist "%BUILD_WORK%\" goto :build_workspace_failed

set "BUILD_RES=%BUILD_WORK%\app.res"
set "BUILD_OBJ=%BUILD_WORK%\main.obj"
set "BUILD_SUBTITLE_EXTRACT_OBJ=%BUILD_WORK%\subtitle_extract.obj"
set "BUILD_SUBTITLE_SYNC_OBJ=%BUILD_WORK%\subtitle_sync_core.obj"
set "BUILD_SUBTITLE_ENCODING_OBJ=%BUILD_WORK%\subtitle_encoding.obj"
set "BUILD_PGS_SYNC_OBJ=%BUILD_WORK%\pgs_sync.obj"
set "BUILD_VOBSUB_SYNC_OBJ=%BUILD_WORK%\vobsub_sync.obj"
set "BUILD_EXE=%BUILD_WORK%\WinterStatic-Video-Cutter.exe"

echo [1/4] Compiling resources...
rc /nologo /fo "%BUILD_RES%" resource.rc
if errorlevel 1 goto :build_failed

echo [2/4] Compiling native frontend and subtitle modules...
cl /nologo /std:c++17 /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /c /Fo"%BUILD_OBJ%" main.cpp
if errorlevel 1 goto :build_failed
cl /nologo /std:c++17 /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /c /Fo"%BUILD_SUBTITLE_EXTRACT_OBJ%" subtitle_extract.cpp
if errorlevel 1 goto :build_failed
cl /nologo /std:c++17 /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /c /Fo"%BUILD_SUBTITLE_SYNC_OBJ%" subtitle_sync_core.cpp
if errorlevel 1 goto :build_failed
cl /nologo /std:c++17 /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /c /Fo"%BUILD_SUBTITLE_ENCODING_OBJ%" subtitle_encoding.cpp
if errorlevel 1 goto :build_failed
cl /nologo /std:c++17 /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /c /Fo"%BUILD_PGS_SYNC_OBJ%" pgs_sync.cpp
if errorlevel 1 goto :build_failed
cl /nologo /std:c++17 /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /c /Fo"%BUILD_VOBSUB_SYNC_OBJ%" vobsub_sync.cpp
if errorlevel 1 goto :build_failed
cl /nologo /Fe"%BUILD_EXE%" "%BUILD_OBJ%" "%BUILD_SUBTITLE_EXTRACT_OBJ%" "%BUILD_SUBTITLE_SYNC_OBJ%" "%BUILD_SUBTITLE_ENCODING_OBJ%" "%BUILD_PGS_SYNC_OBJ%" "%BUILD_VOBSUB_SYNC_OBJ%" "%BUILD_RES%" ^
  /link /SUBSYSTEM:WINDOWS ^
  User32.lib Gdi32.lib Dwmapi.lib UxTheme.lib Comdlg32.lib Shell32.lib Shlwapi.lib Ole32.lib Uuid.lib
if errorlevel 1 goto :build_failed
if not exist "%BUILD_EXE%" goto :build_failed

set "BASE_DIST=%CD%\WinterStatic-Video-Cutter-%APP_VERSION%-portable"
set "DIST=%BASE_DIST%"
if not exist "%DIST%" goto :dist_ready
rmdir /s /q "%DIST%" >nul 2>nul
if not exist "%DIST%" goto :dist_ready
set /a SUFFIX=2
:find_dist
set "DIST=%BASE_DIST%-%SUFFIX%"
if not exist "%DIST%" goto :dist_ready
set /a SUFFIX+=1
goto :find_dist

:dist_ready
mkdir "%DIST%"
if errorlevel 1 goto :package_failed
if not exist "%DIST%\" goto :package_failed

rem The newly linked EXE can occasionally be held for a moment by Windows or
rem antivirus scanning. Retry the package copy briefly instead of failing the
rem entire build on a transient sharing violation.
call :copy_with_retry "%BUILD_EXE%" "%DIST%\WinterStatic-Video-Cutter.exe"
if errorlevel 1 goto :package_failed
copy /y "mpv.conf" "%DIST%\mpv.conf" >nul
if errorlevel 1 goto :package_failed

mkdir "%DIST%\libmpv"
if errorlevel 1 goto :package_failed

copy /y "README.md" "%DIST%\README.md" >nul
if errorlevel 1 goto :package_failed
copy /y "ACKNOWLEDGEMENTS.md" "%DIST%\ACKNOWLEDGEMENTS.md" >nul
if errorlevel 1 goto :package_failed
copy /y "LICENSE" "%DIST%\LICENSE" >nul
if errorlevel 1 goto :package_failed
copy /y "THIRD-PARTY-NOTICE.txt" "%DIST%\libmpv\THIRD-PARTY-NOTICE.txt" >nul
if errorlevel 1 goto :package_failed

echo [3/4] Collecting isolated libmpv runtime, provenance, and licenses...
set "MSYSTEM=MINGW64"
set "CHERE_INVOKING=1"
set "ROOT_FWD=%CD:\=/%"
set "LIBMPV_FWD=%DIST:\=/%/libmpv"
"C:\msys64\usr\bin\bash.exe" -lc "export PATH=/mingw64/bin:/usr/bin:$PATH; cd '%ROOT_FWD%'; ./collect-runtime.sh '%LIBMPV_FWD%'"
if errorlevel 1 goto :package_failed


> "%DIST%\settings.ini" (
  echo [General]
  echo ClickDelayMs=64
  echo Volume=70
  echo Muted=0
  echo AutoPlayNextFile=0
  echo ExitFullscreenAtEnd=1
  echo HideWindowedCursor=1
  echo TaskbarPlaylistControls=0
  echo ShowStopButton=0
  echo ShowPlaylistButtons=0
  echo InstanceOpenBehavior=0
  echo OsdFontSize=72
  echo OsdPosition=1
  echo PlaylistStartOsd=0
  echo.
  echo [Video]
  echo GpuApi=0
  echo.
  echo [Subtitles]
  echo FontSize=55
  echo.
  echo [Tracks]
  echo AudioLanguage=
  echo SubtitleLanguage=
  echo LastAudioValid=0
  echo LastAudioLanguage=
  echo LastAudioTitle=
  echo LastSubtitleValid=0
  echo LastSubtitleOff=0
  echo LastSubtitleLanguage=
  echo LastSubtitleTitle=
  echo.
  echo [Window]
  echo Maximized=1
  echo.
  echo [Cutter]
  echo Format=3
  echo Resolution=Source
  echo TargetKB=
  echo AudioOffsetMs=0
  echo CRF=32
  echo AudioKbpsOverride=
  echo ShowPowerShell=1
  echo ClosePowerShellWhenDone=1
  echo Overwrite=0
)
if errorlevel 1 goto :package_failed
if not exist "%DIST%\settings.ini" goto :package_failed

type nul > "%DIST%\resume.ini"
if errorlevel 1 goto :package_failed

> "%DIST%\BUILD-INFO.txt" (
  echo WinterStatic Video Cutter
  echo Version %APP_VERSION%
  echo.
  echo Created by WinterStatic
  echo GitHub: https://github.com/WinterStatic
  echo Playback engine: libmpv ^(isolated in libmpv\^)
  echo Editing engine: external FFmpeg/ffprobe ^(local ffmpeg folder, MSYS2, or PATH^)
  echo Frontend license: GPL-3.0-or-later
  echo Acknowledgements: ACKNOWLEDGEMENTS.md
  echo.
  echo Root user files: settings.ini, resume.ini, mpv.conf
  echo Application information: README.md, ACKNOWLEDGEMENTS.md, LICENSE, BUILD-INFO.txt
  echo Replaceable playback runtime: libmpv\
  echo Runtime provenance: libmpv\RUNTIME-MANIFEST.txt
  echo Runtime source mapping: libmpv\RUNTIME-PACKAGES.tsv
  echo Runtime license files: libmpv\licenses\
  echo.
  echo This folder is designed to be copied to another 64-bit Windows 10/11 PC.
)
if errorlevel 1 goto :package_failed
if not exist "%DIST%\BUILD-INFO.txt" goto :package_failed

if not "%RELEASE_MODE%"=="1" goto :release_source_done
call :collect_release_source
if errorlevel 1 goto :source_package_failed
:release_source_done

rem Successful package is complete; the scratch build products are disposable.
rmdir /s /q "%BUILD_WORK%" >nul 2>nul

echo [4/4] Done.
echo.
echo ==========================================================
echo BUILD SUCCESSFUL
echo ==========================================================
echo.
echo Portable folder:
echo   %DIST%
echo.
for %%F in ("%DIST%") do set "DIST_NAME=%%~nxF"
echo Copy or move the ENTIRE folder named:
echo   %DIST_NAME%
echo.
echo to the PC or directory where you want to keep the cutter.
echo The libmpv playback engine is isolated in the libmpv folder.
echo User-owned settings remain in the root folder.
if "%RELEASE_MODE%"=="1" (
    echo.
    echo Matching runtime source bundle:
    echo   %SOURCE_ZIP%
    echo Source working folder:
    echo   %SOURCE_DIR%
)
echo.
echo Then open that folder and run:
echo   WinterStatic-Video-Cutter.exe
echo.
echo.
pause
exit /b 0

:collect_release_source
echo [release] Collecting exact MSYS2 runtime source archives...
set "SOURCE_DIR=%CD%\WinterStatic-Video-Cutter-%APP_VERSION%-runtime-source"
set "SOURCE_ZIP=%CD%\WinterStatic-Video-Cutter-%APP_VERSION%-Runtime-Source.zip"
set "SOURCE_DIR_FWD=%CD:\=/%/WinterStatic-Video-Cutter-%APP_VERSION%-runtime-source"
set "SOURCE_ZIP_FWD=%CD:\=/%/WinterStatic-Video-Cutter-%APP_VERSION%-Runtime-Source.zip"
"C:\msys64\usr\bin\bash.exe" -lc "export PATH=/mingw64/bin:/usr/bin:$PATH; cd '%ROOT_FWD%'; ./collect-runtime-source.sh '%LIBMPV_FWD%' '%SOURCE_DIR_FWD%' '%SOURCE_ZIP_FWD%'"
exit /b %ERRORLEVEL%

:copy_with_retry
set "COPY_SOURCE=%~1"
set "COPY_TARGET=%~2"
set /a COPY_ATTEMPT=1
:copy_retry_loop
copy /y "%COPY_SOURCE%" "%COPY_TARGET%" >nul 2>nul
if not errorlevel 1 exit /b 0
if %COPY_ATTEMPT% GEQ 5 exit /b 1
set /a COPY_ATTEMPT+=1
timeout /t 1 /nobreak >nul 2>nul
goto :copy_retry_loop

:no_msvc
echo.
echo ERROR: Visual Studio C++ Build Tools were not found.
echo Install the Desktop development with C++ workload.
goto :fail_pause

:no_rc
echo.
echo ERROR: rc.exe was not found. The Windows SDK component is missing.
goto :fail_pause

:no_msys
echo.
echo ERROR: C:\msys64\usr\bin\bash.exe was not found.
echo This build expects MSYS2 at C:\msys64.
goto :fail_pause

:no_libmpv
echo.
echo ERROR: C:\msys64\mingw64\bin\libmpv-2.dll was not found.
echo Install the MINGW64 mpv package in MSYS2 first.
goto :fail_pause

:env_failed
echo.
echo ERROR: Visual Studio was found but its x64 environment could not be loaded.
goto :fail_pause

:build_workspace_failed
echo.
echo ==========================================================
echo BUILD WORKSPACE FAILED
echo ==========================================================
echo Could not create a temporary build directory under:
echo   %TEMP%
goto :fail_pause

:bad_argument
echo.
echo ERROR: Unknown build argument: %~1
echo.
echo Normal build:
echo   build-native.bat
echo.
echo Public-release build with matching runtime source collection:
echo   build-native.bat release
goto :fail_pause

:source_package_failed
echo.
echo ==========================================================
echo RELEASE SOURCE COLLECTION FAILED
echo ==========================================================
echo The Full Portable folder was built, but one or more exact MSYS2 source
echo archives could not be collected. Do not publish this binary as the Full
echo Portable release until the matching source bundle completes successfully.
if defined SOURCE_DIR echo Partial source files remain at: %SOURCE_DIR%
if defined BUILD_WORK echo Temporary build files remain at: %BUILD_WORK%
goto :fail_pause

:package_failed
echo.
echo ==========================================================
echo RUNTIME PACKAGING FAILED
echo ==========================================================
echo The EXE compiled, but the isolated libmpv runtime folder could not be completed.
if defined BUILD_WORK echo Temporary build files remain at: %BUILD_WORK%
goto :fail_pause

:build_failed
echo.
echo ==========================================================
echo BUILD FAILED
echo ==========================================================
echo Review the compiler message above.
if defined BUILD_WORK echo Temporary build files remain at: %BUILD_WORK%

:fail_pause
echo.
pause
exit /b 1
