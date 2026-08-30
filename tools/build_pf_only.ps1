#
# Helper: build PresenceField Standalone + VST3 (and the AU/VST3 targets via _All)
# Imports MSVC env then builds, without packaging. Used for the Air LFO zipper test.
#
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

$pythonScripts = 'C:\Users\khrit\AppData\Local\Python\pythoncore-3.14-64\Scripts'
$cmake        = Join-Path $pythonScripts 'cmake.exe'
$vcvars       = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'

function Import-VsVars {
    param([string]$VcvarsPath)
    $lines = & $env:ComSpec /c "call `"$VcvarsPath`" >nul 2>&1 && set"
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}
Import-VsVars $vcvars

# Configure once (reuses cache)
& $cmake -S $root -B (Join-Path $root 'build') -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_TESTING=ON `
    "-DZARLINO_LICENSE_PUBLIC_KEY_FILE=$(Join-Path $root 'tools\keys\zarlino_license_public.txt')"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

Write-Host '==> Building PresenceField_All (Standalone + VST3)'
& $cmake --build (Join-Path $root 'build') --target PresenceField_All --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed for PresenceField' }

Write-Host '==> BUILT OK'
