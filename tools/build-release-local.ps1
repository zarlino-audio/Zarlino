<#
.SYNOPSIS
  Local Windows release build + packaging for the Zarlino release-gate plugins.

.DESCRIPTION
  Mirrors what the per-plugin release-gate CI workflows do, but using this
  machine's local resources (so it is slower than CI - that is expected):

    1. Configure once into build/  (Ninja, Release, BUILD_TESTING=ON, license key)
    2. Build each plugin (VST3 + Standalone) and its test runner with -j <Jobs>
    3. Run the plugin's ctest tests (skipped if -SkipTests)
    4. Package VST3 + Standalone zips into artifacts/
    5. Build the NSIS installer and move it into artifacts/
    6. Print an artifact size summary (catches stub/empty installers)

  Plugin set / versions match the release gates:
      ZScorch       1.0.0
      ZTame         1.0.0
      ZVocals       0.1.0
      PresenceField 0.1.0   (no test target; product name "Presence Field")

.EXAMPLE
  pwsh -File tools/build-release-local.ps1
  pwsh -File tools/build-release-local.ps1 -Jobs 1
  pwsh -File tools/build-release-local.ps1 -Plugins ZScorch,ZTame -SkipTests
#>
param(
    [string[]]$Plugins = @('ZScorch', 'ZTame', 'ZVocals', 'PresenceField'),
    [string]$Version = '',
    [int]$Jobs = 2,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

# ---------------------------------------------------------------------------
# Tool resolution (these are NOT on PATH on this machine by default)
# ---------------------------------------------------------------------------
$pythonScripts = 'C:\Users\khrit\AppData\Local\Python\pythoncore-3.14-64\Scripts'
$cmake        = Join-Path $pythonScripts 'cmake.exe'
$ninja        = Join-Path $pythonScripts 'ninja.exe'
$makensis     = 'C:\Program Files (x86)\NSIS\makensis.exe'
$vcvars       = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'

foreach ($tool in @($cmake, $ninja, $makensis, $vcvars)) {
    if (-not (Test-Path $tool)) { throw "Missing required tool: $tool" }
}

# Import the MSVC x64 environment (INCLUDE/LIB/PATH) into this process so
# cmake --build (Ninja) can find cl.exe and the Windows SDK.
function Import-VsVars {
    param([string]$VcvarsPath)
    # Redirect the banner/errors to nul inside cmd; only `set` output is kept.
    # (PS 5.1 + $ErrorActionPreference='Stop' would promote any stderr to a
    #  terminating error otherwise - e.g. vcvarsall's benign vswhere lookup.)
    $lines = & $env:ComSpec /c "call `"$VcvarsPath`" >nul 2>&1 && set"
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}
Import-VsVars $vcvars

# ---------------------------------------------------------------------------
# Plugin metadata
# ---------------------------------------------------------------------------
$pluginDefs = [ordered]@{
    'ZScorch'       = @{ Version = '1.0.0'; HasTests = $true;  ProductName = 'ZScorch' }
    'ZTame'         = @{ Version = '1.0.0'; HasTests = $true;  ProductName = 'ZTame' }
    'ZVocals'       = @{ Version = '0.1.0'; HasTests = $true;  ProductName = 'ZVocals' }
    'PresenceField' = @{ Version = '0.1.0'; HasTests = $false; ProductName = 'Presence Field' }
}

$selected = @()
foreach ($p in $Plugins) {
    $pluginDir = Join-Path $root "plugins\$p"
    if (-not (Test-Path (Join-Path $pluginDir 'CMakeLists.txt'))) { throw "Unknown plugin '$p' (no plugins/$p/CMakeLists.txt). Valid: $($pluginDefs.Keys -join ', ')" }
    if (-not $pluginDefs.Contains($p)) {
        # Generic fallback for any plugin in the workspace.
        $hasTests = Test-Path (Join-Path $pluginDir 'Tests')
        $pluginDefs[$p] = @{ Version = if ($Version) { $Version } else { '0.1.0' }; HasTests = $hasTests; ProductName = $p }
        Write-Host "==> Generic plugin def: $p v$($pluginDefs[$p].Version) HasTests=$hasTests" -ForegroundColor DarkYellow
    }
    $selected += $p
}

$artifactsRoot = Join-Path $root 'artifacts'
New-Item -ItemType Directory -Force -Path $artifactsRoot | Out-Null

# ---------------------------------------------------------------------------
# Step 1 - Configure (reuses existing build/ cache; safe to run repeatedly)
# ---------------------------------------------------------------------------
Write-Host "==> Configuring build/ (Ninja, Release, BUILD_TESTING=ON)" -ForegroundColor Cyan
$licenseKeyFile = Join-Path $root 'tools\keys\zarlino_license_public.txt'
if (-not (Test-Path $licenseKeyFile)) {
    throw "License public key file not found: $licenseKeyFile (see tools/generate_license_keys.py)"
}
& $cmake -S $root -B (Join-Path $root 'build') -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_TESTING=ON `
    "-DZARLINO_LICENSE_PUBLIC_KEY_FILE=$licenseKeyFile"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

# ---------------------------------------------------------------------------
# Per-plugin: build -> test -> package
# ---------------------------------------------------------------------------
$summary = New-Object System.Collections.Generic.List[object]

foreach ($plugin in $selected) {
    $def    = $pluginDefs[$plugin]
    $ver    = $def.Version
    $prod   = $def.ProductName
    $buildDir = Join-Path $root "build"
    $artefactsDir = Join-Path $buildDir "plugins\$plugin\${plugin}_artefacts\Release"

    Write-Host "`n########## $plugin v$ver ##########" -ForegroundColor Magenta

    # --- Build plugin + test runner ---
    # JUCE 7+ splits formats into separate targets; <Plugin>_All builds the
    # VST3 + Standalone binaries (the bare <Plugin> target only builds the
    # SharedCode lib, so packaging would find no payloads).
    $targets = @("${plugin}_All")
    if ($def.HasTests -and -not $SkipTests) { $targets += "${plugin}_Tests" }

    Write-Host "==> Building: $($targets -join ' ')" -ForegroundColor Cyan
    & $cmake --build $buildDir --target $targets --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $plugin" }

    # --- Run tests ---
    if ($def.HasTests -and -not $SkipTests) {
        Write-Host "==> Running tests (ctest -R $plugin)" -ForegroundColor Cyan
        $ctest = Join-Path $pythonScripts 'ctest.exe'
        if (-not (Test-Path $ctest)) {
            $cmdCtest = Get-Command ctest -ErrorAction SilentlyContinue
            if ($cmdCtest) { $ctest = $cmdCtest.Source }
        }
        if (-not (Test-Path $ctest)) { Write-Warning "ctest not found - skipping tests for $plugin" }
        else {
            & $ctest --test-dir $buildDir -C Release --output-on-failure -R $plugin
            if ($LASTEXITCODE -ne 0) { throw "Tests failed for $plugin" }
        }
    } elseif (-not $def.HasTests) {
        Write-Host "==> No test target for $plugin - skipping tests" -ForegroundColor DarkGray
    }

    # --- Verify payloads exist before packaging ---
    $exeName  = "$prod.exe"
    $vst3Name = "$prod.vst3"
    $standaloneExe = Join-Path $artefactsDir "Standalone\$exeName"
    $vst3Bundle    = Join-Path $artefactsDir "VST3\$vst3Name"
    $moduleInfo    = Join-Path $vst3Bundle "Contents\Resources\moduleinfo.json"

    foreach ($required in @($standaloneExe, $vst3Bundle, $moduleInfo)) {
        if (-not (Test-Path $required)) {
            throw "Missing payload for $plugin : $required (is the build complete?)"
        }
    }

    # --- Package VST3 zip (contents of the .vst3 bundle) ---
    Write-Host "==> Packaging VST3 zip" -ForegroundColor Cyan
    $vst3Staging = Join-Path $artifactsRoot "${plugin}_vst3_staging"
    if (Test-Path $vst3Staging) { Remove-Item -Recurse -Force $vst3Staging }
    New-Item -ItemType Directory -Force -Path $vst3Staging | Out-Null
    Copy-Item -Recurse -Force "$vst3Bundle\*" -Destination $vst3Staging
    $vst3Zip = Join-Path $artifactsRoot "${plugin}-v$ver-Windows-VST3.zip"
    Compress-Archive -Path "$vst3Staging\*" -DestinationPath $vst3Zip -Force

    # --- Package Standalone zip (exe + README if present) ---
    Write-Host "==> Packaging Standalone zip" -ForegroundColor Cyan
    $saStaging = Join-Path $artifactsRoot "${plugin}_standalone_staging"
    if (Test-Path $saStaging) { Remove-Item -Recurse -Force $saStaging }
    New-Item -ItemType Directory -Force -Path $saStaging | Out-Null
    Copy-Item -Force $standaloneExe -Destination $saStaging
    $readme = Join-Path $root "plugins\$plugin\README.md"
    if (Test-Path $readme) { Copy-Item -Force $readme -Destination $saStaging }
    $saZip = Join-Path $artifactsRoot "${plugin}-v$ver-Windows-Standalone.zip"
    Compress-Archive -Path "$saStaging\*" -DestinationPath $saZip -Force

    # --- NSIS installer ---
    Write-Host "==> Building NSIS installer" -ForegroundColor Cyan
    $installerDir = Join-Path $root "plugins\$plugin\installer"
    $nsi = Join-Path $installerDir "$plugin.nsi"
    $installerOut = Join-Path $installerDir "${plugin}-v$ver-Windows-Installer.exe"
    if (Test-Path $installerOut) { Remove-Item -Force $installerOut }
    Push-Location $installerDir
    try {
        & $makensis "/DVERSION=$ver" "/DARTEFACTS=$artefactsDir" "$plugin.nsi"
        if ($LASTEXITCODE -ne 0) { throw "makensis failed for $plugin" }
    } finally {
        Pop-Location
    }
    if (-not (Test-Path $installerOut)) { throw "NSIS installer not produced: $installerOut" }
    $finalInstaller = Join-Path $artifactsRoot "${plugin}-v$ver-Windows-Installer.exe"
    Move-Item -Force $installerOut $finalInstaller

    # --- Clean staging ---
    Remove-Item -Recurse -Force $vst3Staging, $saStaging

    $summary.Add([pscustomobject]@{
        Plugin          = $plugin
        Version         = $ver
        InstallerKB     = [math]::Round((Get-Item $finalInstaller).Length / 1KB)
        VST3ZipKB       = [math]::Round((Get-Item $vst3Zip).Length / 1KB)
        StandaloneZipKB = [math]::Round((Get-Item $saZip).Length / 1KB)
    })
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host "`n=============================================================" -ForegroundColor Green
Write-Host "Artifact summary" -ForegroundColor Green
Write-Host "=============================================================" -ForegroundColor Green
$summary | Format-Table -AutoSize

Write-Host "All artifacts written to: $artifactsRoot" -ForegroundColor Green
foreach ($row in $summary) {
    if ($row.InstallerKB -lt 1024) {
        Write-Warning "$($row.Plugin) installer is only $($row.InstallerKB) KB - possible stub installer (payload missing)!"
    }
}
