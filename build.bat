@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
pushd "%ROOT%" >nul || exit /b 1

set "QT_ROOT=C:\work\qt"

if exist "C:\Program Files\CMake\bin\cmake.exe" set "PATH=C:\Program Files\CMake\bin;%PATH%"
if exist "%QT_ROOT%\Tools\CMake_64\bin\cmake.exe" set "PATH=%QT_ROOT%\Tools\CMake_64\bin;%PATH%"
if exist "%QT_ROOT%\Tools\Ninja\ninja.exe" set "PATH=%QT_ROOT%\Tools\Ninja;%PATH%"
if exist "C:\work\tools\ninja.exe" set "PATH=C:\work\tools;%PATH%"

if "%QT_DIR%"=="" call :find_qt
if "%QT_DIR%"=="" (
    echo ERROR: QT_DIR is not set and no Qt kit was found.
    echo Expected installed kit examples:
    echo   C:\work\qt\6.11.1\mingw_64
    echo   C:\Qt\6.10.2\msvc2022_64
    echo.
    echo If Qt is installed somewhere else, run:
    echo   set QT_DIR=C:\path\to\qt\kit
    echo   build.bat
    popd >nul
    exit /b 1
)

if exist "%QT_DIR%\configure.bat" if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo ERROR: QT_DIR appears to point at Qt source, not an installed Qt kit:
    echo   %QT_DIR%
    echo.
    echo This app needs a built/installed Qt prefix containing:
    echo   bin\windeployqt.exe
    echo   lib\cmake\Qt6\Qt6Config.cmake
    popd >nul
    exit /b 1
)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo ERROR: windeployqt.exe was not found under:
    echo   %QT_DIR%\bin
    echo Check QT_DIR.
    popd >nul
    exit /b 1
)

if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo ERROR: Qt6Config.cmake was not found under:
    echo   %QT_DIR%\lib\cmake\Qt6
    echo Check QT_DIR.
    popd >nul
    exit /b 1
)

set "USING_MINGW="
if exist "%QT_DIR%\mkspecs\win32-g++" set "USING_MINGW=1"
if not "%QT_DIR:mingw=%"=="%QT_DIR%" set "USING_MINGW=1"

set "COMPILER_ARGS="
if defined USING_MINGW (
    call :setup_mingw
) else (
    if not defined VSCMD_ARG_TGT_ARCH call :setup_msvc
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake was not found on PATH.
    popd >nul
    exit /b 1
)

if defined USING_MINGW (
    set "BUILD_DIR=%ROOT%\build-win-mingw"
) else (
    set "BUILD_DIR=%ROOT%\build-win-msvc"
)
set "DIST_DIR=%ROOT%\dist-win"
set "GENERATOR=Ninja"
set "CONFIG_ARGS=-DCMAKE_BUILD_TYPE=Release"
rem A bare "build.bat" builds EVERY optional feature (Lua scripting + the native
rem OS speller). Opt out with one or more flags, in any order:
rem   build.bat noterm           - skip the script terminal / BBS UI (vanilla IRC)
rem   build.bat noosspell        - skip the native OS speller (Hunspell only)
rem   build.bat noterm noosspell - leanest build
rem (Lua scripting is a core dependency and always built.)
set "MAXCHAT_TERMINAL=ON"
set "MAXCHAT_OS_SPELL=ON"
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="noterm"    set "MAXCHAT_TERMINAL=OFF"
if /I "%~1"=="noosspell" set "MAXCHAT_OS_SPELL=OFF"
rem Back-compat: "osspell" used to be the opt-in; it is now the default, so
rem accept it as a harmless no-op rather than erroring on old muscle memory.
if /I "%~1"=="osspell"   set "MAXCHAT_OS_SPELL=ON"
shift
goto parse_args
:args_done
set "CONFIG_ARGS=%CONFIG_ARGS% -DMAXCHAT_TERMINAL=%MAXCHAT_TERMINAL%"
echo Terminal/BBS UI: %MAXCHAT_TERMINAL%
set "CONFIG_ARGS=%CONFIG_ARGS% -DMAXCHAT_OS_SPELL=%MAXCHAT_OS_SPELL%"
echo OS spell engine: %MAXCHAT_OS_SPELL%
set "BUILD_ARGS="
set "CTEST_ARGS="
set "EXE_PATH=%BUILD_DIR%\maxchat-c.exe"

where ninja >nul 2>nul
if errorlevel 1 (
    if defined USING_MINGW (
        echo ERROR: Ninja was not found, and the MinGW Qt kit needs Ninja here.
        echo Expected:
        echo   C:\work\qt\Tools\Ninja\ninja.exe
        echo or:
        echo   C:\work\tools\ninja.exe
        popd >nul
        exit /b 1
    )
    set "GENERATOR=Visual Studio 17 2022"
    set "CONFIG_ARGS=-A x64"
    set "BUILD_ARGS=--config Release"
    set "CTEST_ARGS=-C Release"
    set "EXE_PATH=%BUILD_DIR%\Release\maxchat-c.exe"
)

echo Using Qt: %QT_DIR%
if defined USING_MINGW echo Using MinGW: %MINGW_DIR%
echo Using generator: %GENERATOR%

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" %CONFIG_ARGS% -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_INSTALL_PREFIX="%DIST_DIR%" %COMPILER_ARGS%
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    popd >nul
    exit /b 1
)

rem Capture build output to a log (no native tee on cmd) so failures are
rem inspectable after the fact; print it, preserving the real exit code.
cmake --build "%BUILD_DIR%" %BUILD_ARGS% > "%BUILD_DIR%\build-output.log" 2>&1
set "BUILD_RC=%errorlevel%"
type "%BUILD_DIR%\build-output.log"
if not "%BUILD_RC%"=="0" (
    echo ERROR: Build failed. Full log: %BUILD_DIR%\build-output.log
    popd >nul
    exit /b 1
)

if /I "%RUN_TESTS%"=="1" (
    ctest --test-dir "%BUILD_DIR%" %CTEST_ARGS% --output-on-failure
    if errorlevel 1 (
        echo ERROR: Tests failed.
        popd >nul
        exit /b 1
    )
)

if not exist "%EXE_PATH%" (
    echo ERROR: Built executable was not found:
    echo   %EXE_PATH%
    popd >nul
    exit /b 1
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
copy /Y "%EXE_PATH%" "%DIST_DIR%\maxchat-c.exe" >nul
if errorlevel 1 (
    echo ERROR: Could not copy executable to dist-win.
    popd >nul
    exit /b 1
)

"%QT_DIR%\bin\windeployqt.exe" --release --compiler-runtime "%DIST_DIR%\maxchat-c.exe"
if errorlevel 1 (
    echo ERROR: windeployqt failed.
    popd >nul
    exit /b 1
)

call :copy_notices
if errorlevel 1 (
    echo ERROR: Could not copy license notices.
    popd >nul
    exit /b 1
)

call :copy_assets
if errorlevel 1 (
    echo ERROR: Could not copy runtime assets.
    popd >nul
    exit /b 1
)

echo.
echo Windows build complete:
echo   %DIST_DIR%\maxchat-c.exe
popd >nul
exit /b 0

:find_qt
for %%Q in (
    "C:\work\qt\6.11.1\mingw_64"
    "C:\work\qt\6.11.0\mingw_64"
    "C:\work\qt\6.10.2\mingw_64"
    "C:\work\qt\6.10.1\mingw_64"
    "C:\work\qt\6.9.3\mingw_64"
    "C:\work\qt\6.8.3\mingw_64"
    "C:\work\qt\6.11.1\msvc2022_64"
    "C:\work\qt\6.10.2\msvc2022_64"
    "C:\work\qt\6.10.1\msvc2022_64"
    "C:\work\qt\6.9.3\msvc2022_64"
    "C:\work\qt\6.8.3\msvc2022_64"
    "C:\Qt\6.10.2\msvc2022_64"
    "C:\Qt\6.10.1\msvc2022_64"
    "C:\Qt\6.9.3\msvc2022_64"
    "C:\Qt\6.8.3\msvc2022_64"
) do (
    if exist "%%~Q\bin\windeployqt.exe" if exist "%%~Q\lib\cmake\Qt6\Qt6Config.cmake" (
        set "QT_DIR=%%~Q"
        exit /b 0
    )
)
exit /b 0

:setup_mingw
if "%MINGW_DIR%"=="" if exist "%QT_ROOT%\Tools\mingw1310_64\bin\g++.exe" set "MINGW_DIR=%QT_ROOT%\Tools\mingw1310_64"
if "%MINGW_DIR%"=="" if exist "C:\work\qt\Tools\mingw1310_64\bin\g++.exe" set "MINGW_DIR=C:\work\qt\Tools\mingw1310_64"
if "%MINGW_DIR%"=="" (
    echo ERROR: MinGW compiler was not found.
    echo Expected:
    echo   C:\work\qt\Tools\mingw1310_64\bin\g++.exe
    popd >nul
    exit /b 1
)
if not exist "%MINGW_DIR%\bin\g++.exe" (
    echo ERROR: g++.exe was not found under:
    echo   %MINGW_DIR%\bin
    popd >nul
    exit /b 1
)
set "PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%"
set "COMPILER_ARGS=-DCMAKE_C_COMPILER=%MINGW_DIR%\bin\gcc.exe -DCMAKE_CXX_COMPILER=%MINGW_DIR%\bin\g++.exe"
exit /b 0

:setup_msvc
where cl >nul 2>nul
if not errorlevel 1 exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: MSVC compiler was not found and vswhere.exe is missing.
    echo Install Visual Studio 2022 Build Tools with Desktop development with C++.
    popd >nul
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALL=%%I"
)

if "%VSINSTALL%"=="" (
    echo ERROR: Visual Studio C++ tools were not found.
    echo Install the Desktop development with C++ workload.
    popd >nul
    exit /b 1
)

if not exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERROR: vcvars64.bat was not found under:
    echo   %VSINSTALL%
    popd >nul
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
exit /b 0

:copy_assets
rem Themes and wallpapers are read from disk at runtime (fonts/sounds/icons are
rem embedded via maxchat.qrc) - without assets\ next to the exe the app falls
rem back to the built-in Dark theme only.
if not exist "%ROOT%\assets\themes" (
    echo ERROR: assets\themes is missing from the source tree.
    exit /b 1
)
if not exist "%ROOT%\assets\wallpapers" (
    echo ERROR: assets\wallpapers is missing from the source tree.
    exit /b 1
)
if not exist "%DIST_DIR%\assets" mkdir "%DIST_DIR%\assets"
xcopy /E /I /Y "%ROOT%\assets\themes" "%DIST_DIR%\assets\themes" >nul
if errorlevel 1 exit /b 1
xcopy /E /I /Y "%ROOT%\assets\wallpapers" "%DIST_DIR%\assets\wallpapers" >nul
if errorlevel 1 exit /b 1
rem Spellcheck dictionaries (.aff/.dic) load from disk at runtime; ship the
rem bundled en_US so the internal engine works out of the box.
if exist "%ROOT%\assets\dictionaries" (
    xcopy /E /I /Y "%ROOT%\assets\dictionaries" "%DIST_DIR%\assets\dictionaries" >nul
    if errorlevel 1 exit /b 1
)
exit /b 0

:copy_notices
if exist "%ROOT%\LICENSE" copy /Y "%ROOT%\LICENSE" "%DIST_DIR%\LICENSE" >nul
if exist "%ROOT%\THIRD_PARTY_NOTICES.md" copy /Y "%ROOT%\THIRD_PARTY_NOTICES.md" "%DIST_DIR%\THIRD_PARTY_NOTICES.md" >nul

if not exist "%DIST_DIR%\licenses" mkdir "%DIST_DIR%\licenses"
if exist "%ROOT%\licenses\fonts" xcopy /E /I /Y "%ROOT%\licenses\fonts" "%DIST_DIR%\licenses\fonts" >nul
if exist "%QT_ROOT%\Licenses" xcopy /E /I /Y "%QT_ROOT%\Licenses" "%DIST_DIR%\licenses\qt" >nul

if defined USING_MINGW (
    if not exist "%DIST_DIR%\licenses\mingw" mkdir "%DIST_DIR%\licenses\mingw"
    if exist "%MINGW_DIR%\licenses\gcc" xcopy /E /I /Y "%MINGW_DIR%\licenses\gcc" "%DIST_DIR%\licenses\mingw\gcc" >nul
    if exist "%MINGW_DIR%\licenses\mingw-w64" xcopy /E /I /Y "%MINGW_DIR%\licenses\mingw-w64" "%DIST_DIR%\licenses\mingw\mingw-w64" >nul
    if exist "%MINGW_DIR%\licenses\winpthreads" xcopy /E /I /Y "%MINGW_DIR%\licenses\winpthreads" "%DIST_DIR%\licenses\mingw\winpthreads" >nul
)

exit /b 0
