# ============================================================================
#  build-release-local.ps1 — reproducible local release build + packaging
#  ---------------------------------------------------------------------------
#  The CI release gates built this by hand; this script is the local equivalent:
#    1. ensures an MSVC (Ninja, Release) build tree exists
#    2. builds <Plugin> (+ <Plugin>_Tests when that target exists)
#    3. runs `ctest -L release-gate` when any labelled test exists
#    4. packages:  <P>-v<ver>-Windows-VST3.zip
#                  <P>-v<ver>-Windows-Standalone.zip
#                  <P>-v<ver>-Windows-Installer.exe      (when <P>.nsi + NSIS exist)
#    5. writes SHA256SUMS.txt + RELEASE_MANIFEST.txt into the output folder
#
#  Usage:
#    powershell -ExecutionPolicy Bypass -File tools\build-release-local.ps1
#    powershell -ExecutionPolicy Bypass -File tools\build-release-local.ps1 -Plugin ZScorch
#    powershell -ExecutionPolicy Bypass -File tools\build-release-local.ps1 -SkipTests -SkipInstaller
#
#  Output: artifacts\release-<P>-v<ver>\  (override with -OutDir)
#  Exit:   0 = ok, 1 = any failure (toolchain/build/test/package)
#
#  Notes:
#    * ZTame.nsi accepts /DBUILD_DIR=<absolute build dir> (defaults to .\build,
#      so CI is unaffected) — this script passes -BuildDir through to it.
#    * A dirty git tree is warned about and recorded in the manifest; it is
#      not fatal (local test releases are legitimate).
# ============================================================================
[CmdletBinding()]
param(
    [string]$Plugin   = 'ZTame',
    [string]$Version  = '',
    [string]$BuildDir = 'build-msvc',
    [string]$OutDir   = '',
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$SkipInstaller,
    [switch]$SkipPackaging,
    [switch]$ForceConfigure
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

function Fail([string]$msg) { Write-Host "FAIL: $msg" -ForegroundColor Red; exit 1 }
function Info([string]$msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

# --- Toolchain ----------------------------------------------------------------
$vcvarsCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\17\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\17\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) { Fail 'vcvars64.bat not found (expected VS18 or VS17 Community/BuildTools)' }

$pyScripts = 'C:\Users\khrit\AppData\Local\Python\pythoncore-3.14-64\Scripts'
$cmake = $null
foreach ($c in @((Join-Path $pyScripts 'cmake.exe'), (Get-Command cmake -ErrorAction SilentlyContinue | ForEach-Object { $_.Source }))) {
    if ($c -and (Test-Path $c)) { $cmake = $c; break }
}
if (-not $cmake) { Fail 'cmake.exe not found' }

$ninja = $null
foreach ($n in @((Join-Path $pyScripts 'ninja.exe'), (Get-Command ninja -ErrorAction SilentlyContinue | ForEach-Object { $_.Source }))) {
    if ($n -and (Test-Path $n)) { $ninja = $n; break }
}
if (-not $ninja) { Fail 'ninja.exe not found' }

$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path $ctest)) { Fail "ctest.exe not found next to cmake ($ctest)" }

$makensis = $null
foreach ($m in @((Get-Command makensis -ErrorAction SilentlyContinue | ForEach-Object { $_.Source }), 'C:\Program Files (x86)\NSIS\makensis.exe', 'C:\Program Files\NSIS\makensis.exe')) {
    if ($m -and (Test-Path $m)) { $makensis = $m; break }
}

# --- Import the MSVC environment into this process -----------------------------
Info "MSVC env: $vcvars"
$envDump = & cmd /c "`"$vcvars`" >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0) { Fail 'vcvars64 import failed' }
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2] }
}

# --- Resolve plugin + version ---------------------------------------------------
if (-not (Test-Path (Join-Path $repo "plugins\$Plugin"))) { Fail "no plugins\$Plugin directory" }
if (-not $Version) {
    $cmText = Get-Content (Join-Path $repo "plugins\$Plugin\CMakeLists.txt") -Raw
    $re = '(' + $Plugin.ToUpper() + ')_VERSION="([0-9][^"]*)"'
    if ($cmText -match $re) { $Version = $Matches[2] }
    else { Fail "could not parse <PLUGIN>_VERSION from plugins\$Plugin\CMakeLists.txt — pass -Version" }
}
Info "Plugin: $Plugin  Version: $Version"

$gitHead = 'unknown'; $dirty = -1
try {
    $gitHead = (git rev-parse --short HEAD).Trim()
    $dirty = (git status --porcelain | Measure-Object).Count
} catch {}
if ($dirty -gt 0) { Write-Host ("WARN: working tree has {0} uncommitted changes — this release will not match any commit" -f $dirty) -ForegroundColor Yellow }

if (-not $OutDir) { $OutDir = Join-Path $repo ("artifacts\release-{0}-v{1}" -f $Plugin, $Version) }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Info "Output: $OutDir"

# --- Configure -------------------------------------------------------------------
$buildPath = Join-Path $repo $BuildDir
$cachePath = Join-Path $buildPath 'CMakeCache.txt'
if ($ForceConfigure -or -not (Test-Path $cachePath)) {
    Info "Configuring $BuildDir (Ninja, Release, BUILD_TESTING=ON)"
    & $cmake -S $repo -B $buildPath -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { Fail 'cmake configure failed' }
} else {
    $btOn = [bool](Select-String -Path $cachePath -Pattern '^BUILD_TESTING:BOOL=ON$' -Quiet)
    if (-not $btOn) {
        Info 'Enabling BUILD_TESTING in the existing cache (test registration needs it)'
        & $cmake -S $repo -B $buildPath -DBUILD_TESTING=ON
        if ($LASTEXITCODE -ne 0) { Fail 'cmake reconfigure failed' }
    }
}

# --- Build -----------------------------------------------------------------------
# NOTE: under Ninja the bare <Plugin> target can be a shared-code-only no-op —
# <Plugin>_All is what actually builds the VST3 + Standalone formats.
$oldEap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
$ninjaTargets = & $ninja -C $buildPath -t targets all 2>$null
$ErrorActionPreference = $oldEap
$hasAll   = [bool]($ninjaTargets | Select-String -Pattern ("^" + [regex]::Escape($Plugin) + "_All:") -Quiet)
$hasTests = [bool]($ninjaTargets | Select-String -Pattern ("^" + [regex]::Escape($Plugin) + "_Tests:") -Quiet)
$targets = @()
if ($hasAll) { $targets += ($Plugin + '_All') } else { $targets += $Plugin }
if ($hasTests) { $targets += ($Plugin + '_Tests') }

if (-not $SkipBuild) {
    Info ("Building targets: " + ($targets -join ', '))
    & $cmake --build $buildPath --config Release --target $targets --parallel
    if ($LASTEXITCODE -ne 0) { Fail 'build failed' }
}

# --- Tests --------------------------------------------------------------------------
if (-not $SkipTests -and $hasTests) {
    # Scope the gate to THIS plugin: the tree may contain other plugins' tests
    # whose executables were never built (they would report "Not Run" failures).
    $nameFilter = "^" + [regex]::Escape($Plugin) + "\."
    Info "ctest -L release-gate -R $nameFilter"
    $oldEap2 = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $listOut = & $ctest --test-dir $buildPath -C Release -N -L release-gate -R $nameFilter 2>&1
    $ErrorActionPreference = $oldEap2
    $count = ([regex]::Matches(($listOut | Out-String), 'Test\s+#\d+')).Count
    if ($count -eq 0) {
        Write-Host 'WARN: no tests carry the release-gate label — test step skipped' -ForegroundColor Yellow
    } else {
        $ErrorActionPreference = 'Continue'
        & $ctest --test-dir $buildPath -C Release --output-on-failure -L release-gate -R $nameFilter
        $rcTests = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($rcTests -ne 0) { Fail 'release-gate tests failed' }
    }
} elseif ($SkipTests) {
    Info 'tests skipped (-SkipTests)'
}

# --- Package -------------------------------------------------------------------------
$artBase   = Join-Path $buildPath ("plugins\{0}\{0}_artefacts\Release" -f $Plugin)
$vst3Bundle = Join-Path $artBase ("VST3\{0}.vst3" -f $Plugin)
$appExe     = Join-Path $artBase ("Standalone\{0}.exe" -f $Plugin)
$stage      = Join-Path $repo 'artifacts\_stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$produced = @()

if (-not $SkipPackaging) {
    $vst3Modules = @()
    if (Test-Path (Join-Path $vst3Bundle 'Contents')) {
        $vst3Modules = @(Get-ChildItem (Join-Path $vst3Bundle 'Contents') -Recurse -Filter '*.vst3' -File -ErrorAction SilentlyContinue)
    }
    if ($vst3Modules.Count -gt 0) {
        New-Item -ItemType Directory -Force -Path (Join-Path $stage 'vst3') | Out-Null
        Copy-Item -Recurse -Path $vst3Bundle -Destination (Join-Path $stage 'vst3') -Force
        $zipV = Join-Path $OutDir ("{0}-v{1}-Windows-VST3.zip" -f $Plugin, $Version)
        if (Test-Path $zipV) { Remove-Item $zipV -Force }
        Compress-Archive -Path (Join-Path $stage ("vst3\{0}.vst3" -f $Plugin)) -DestinationPath $zipV
        $produced += $zipV
        Info "packaged $zipV"
    } else {
        Fail "VST3 module missing under $vst3Bundle (was the ${Plugin}_All target built?)"
    }

    if (Test-Path $appExe) {
        New-Item -ItemType Directory -Force -Path (Join-Path $stage 'standalone') | Out-Null
        Copy-Item $appExe -Destination (Join-Path $stage 'standalone') -Force
        $readme = Join-Path $repo ("plugins\{0}\README.md" -f $Plugin)
        if (Test-Path $readme) { Copy-Item $readme -Destination (Join-Path $stage 'standalone') -Force }
        $zipS = Join-Path $OutDir ("{0}-v{1}-Windows-Standalone.zip" -f $Plugin, $Version)
        if (Test-Path $zipS) { Remove-Item $zipS -Force }
        Compress-Archive -Path (Join-Path $stage 'standalone\*') -DestinationPath $zipS
        $produced += $zipS
        Info "packaged $zipS"
    } else {
        Fail "standalone exe missing at $appExe (was the ${Plugin}_All target built?)"
    }
} else {
    Info 'packaging skipped (-SkipPackaging)'
}

# --- Installer (NSIS) -------------------------------------------------------------------
$installerDir = Join-Path $repo ("plugins\{0}\installer" -f $Plugin)
$nsi = Join-Path $installerDir ("{0}.nsi" -f $Plugin)
if ($SkipInstaller) {
    Info 'installer skipped (-SkipInstaller)'
} elseif (-not (Test-Path $nsi)) {
    Write-Host "WARN: no installer script (plugins\$Plugin\installer\$Plugin.nsi) — installer skipped" -ForegroundColor Yellow
} elseif (-not $makensis) {
    Write-Host 'WARN: makensis not found (install NSIS) — installer skipped' -ForegroundColor Yellow
} else {
    Info "NSIS installer via $makensis"
    Push-Location $installerDir
    try {
        & $makensis "/DVERSION=$Version" "/DBUILD_DIR=$buildPath" "$Plugin.nsi"
        if ($LASTEXITCODE -ne 0) { Fail 'makensis failed' }
    } finally { Pop-Location }

    $exeName = "{0}-v{1}-Windows-Installer.exe" -f $Plugin, $Version
    $exeSrc  = Join-Path $installerDir $exeName
    $exeDst  = Join-Path $OutDir $exeName
    if (Test-Path $exeSrc) {
        Move-Item -Force $exeSrc $exeDst
        $produced += $exeDst
        Info "packaged $exeDst"
    } else {
        Fail "installer exe not produced: $exeSrc"
    }
}

# --- Hashes + manifest -------------------------------------------------------------------
$sumFile = Join-Path $OutDir 'SHA256SUMS.txt'
$sumLines = @()
foreach ($f in ($produced | Sort-Object -Unique)) {
    if (Test-Path $f) {
        $h = (Get-FileHash -Algorithm SHA256 $f).Hash.ToLower()
        $sumLines += ("{0}  {1}" -f $h, (Split-Path -Leaf $f))
    }
}
$sumLines | Set-Content -Path $sumFile -Encoding ASCII

$manifest = @(
    "Plugin      : $Plugin",
    "Version     : $Version",
    "Built at    : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    "Git HEAD    : $gitHead (dirty entries: $dirty)",
    "Build dir   : $BuildDir",
    "CMake       : $cmake",
    "Ninja       : $ninja",
    "MSVC env    : $vcvars",
    "NSIS        : $(if ($makensis) { $makensis } else { 'not found' })",
    "",
    "Artifacts:"
) + ($sumLines | ForEach-Object { "  " + $_ })
$manifest | Set-Content -Path (Join-Path $OutDir 'RELEASE_MANIFEST.txt') -Encoding UTF8

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }

# --- Summary ------------------------------------------------------------------------------
Write-Host ''
Info 'RELEASE COMPLETE'
foreach ($f in ($produced | Sort-Object -Unique)) { Write-Host ("  " + $f) }
Write-Host ("  " + $sumFile)
exit 0
