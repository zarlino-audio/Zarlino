@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
"C:\Users\khrit\AppData\Local\Python\pythoncore-3.14-64\Scripts\ninja.exe" -C "C:\Users\khrit\Desktop\plugin\build" %* 
echo NINJA_EXIT=%ERRORLEVEL%
