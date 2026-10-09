@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d "C:\Users\khrit\Desktop\plugin\build"
cmake -S "C:\Users\khrit\Desktop\plugin" -B . -G Ninja
if errorlevel 1 exit /b 1
cmake --build . --target PresenceField --config Release
if errorlevel 1 exit /b 1
cmake --build . --target PresenceField_OfflineRender --config Release
exit /b %errorlevel%
