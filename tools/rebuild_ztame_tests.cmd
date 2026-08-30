@echo off
rem Rebuild ZTame tests with the MSVC environment loaded.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
"C:\Users\khrit\AppData\Local\Python\pythoncore-3.14-64\Scripts\cmake.exe" --build "C:\Users\khrit\Desktop\plugin\build" --target ZTame_Tests --config Release
