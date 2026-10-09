@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "C:\Users\khrit\Desktop\plugin\build"
"C:\Users\khrit\AppData\Local\Python\pythoncore-3.14-64\Scripts\cmake.exe" -G Ninja -DCMAKE_BUILD_TYPE=Release . >nul 2>&1
"C:\Users\khrit\AppData\Local\Python\pythoncore-3.14-64\Scripts\ninja.exe" %*
echo NINJA_EXIT=%ERRORLEVEL%
popd
