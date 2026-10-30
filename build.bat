@echo off
setlocal

:: Keep VERSION in sync with version.h and with AppVersion in setup.iss
set VERSION=0.8
set CONFIG=Release
set PLATFORMS=Win32 x64

:: The ARM64 build needs the "MSVC ARM64 build tools" component installed.
:: Set RECAPS_ARM64=1 to build it as well.
if "%RECAPS_ARM64%"=="1" set PLATFORMS=%PLATFORMS% ARM64

:: locate MSBuild through vswhere (ships with every VS 2017 and later)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto nomsbuild
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD goto nomsbuild

:: clean up
if exist Output rd /s /q Output
if exist bin rd /s /q bin
if exist obj rd /s /q obj

:: build binaries
for %%p in (%PLATFORMS%) do (
    echo === Building %CONFIG% ^| %%p ===
    "%MSBUILD%" recaps.sln /t:Rebuild /p:Configuration=%CONFIG% /p:Platform=%%p /v:minimal /nologo
    if errorlevel 1 goto error
)

:: build binary distribution using Inno Setup
iscc /DMyAppVersion=%VERSION% /F"recaps-%VERSION%-setup" setup.iss
if errorlevel 1 goto error

:: copy the standalone exes next to the installer
for %%p in (%PLATFORMS%) do (
    copy "bin\%%p\%CONFIG%\recaps.exe" "Output\recaps-%VERSION%-%%p.exe" >nul
    if errorlevel 1 goto error
)

:: build source distribution
git archive --format=zip --prefix=recaps-%VERSION%/ -o "Output\recaps-%VERSION%-src.zip" HEAD
if errorlevel 1 echo WARNING: source archive was not created.

echo.
echo Done. The artifacts are in the Output directory.
goto end

:nomsbuild
echo ERROR: MSBuild was not found. Install Visual Studio with the
echo        "Desktop development with C++" workload.
exit /b 1

:error
echo.
echo BUILD FAILED
exit /b 1

:end
endlocal
