@echo off
rem ===========================================================================
rem  Build ZTame targets from anywhere.
rem
rem      tools\build_ztame.cmd ZTame_All ZTame_Tests ZTame_UiShot ZTame_RenderLab
rem
rem  Absolute paths are used on purpose: the build must not depend on the shell's
rem  current directory, and the MSVC environment has to be present for every call.
rem ===========================================================================
setlocal

set "REPO=%~dp0.."
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=C:\Users\khrit\AppData\Local\Python\pythoncore-3.14-64\Scripts\cmake.exe"

call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    echo [build_ztame] failed to initialise the MSVC environment
    exit /b 1
)

pushd "%REPO%"
"%CMAKE%" --build build --target %* --parallel
set "RC=%ERRORLEVEL%"
popd

exit /b %RC%
