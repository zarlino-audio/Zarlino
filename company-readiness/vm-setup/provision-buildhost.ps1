# ============================================================================
#  Zarlino build-host provisioning (run ON the machine that should build)
# ----------------------------------------------------------------------------
#  Moves the plugin build/release capability off the founder's laptop and onto
#  a dedicated host (the always-on VM). Stages, all idempotent:
#
#    toolchain : MSVC build tools (VS 2022 Build Tools, C++ workload),
#                CMake, Ninja, NSIS, Git
#    repo      : clone/update the private monorepo (needs GITHUB_TOKEN)
#    env       : write C:\Zarlino\build-env.ps1 (PATH + vcvars helper) and a
#                wrapper so the OS daemon can build without a login shell
#    verify    : configure + build ZTame (VST3 + Standalone) and run the
#                release-gate tests  -> this is the acceptance evidence
#    backup    : register the nightly backup task on THIS machine (moved off
#                the laptop) and remove it from the old host when asked
#
#  Usage (on the target machine, elevated):
#    powershell -File provision-buildhost.ps1 -Stage all -GitHubToken <tok>
#    powershell -File provision-buildhost.ps1 -Stage toolchain       # no secrets needed
#    powershell -File provision-buildhost.ps1 -Stage all -DryRun     # preview
#
#  ASCII only (PowerShell 5.1 + non-ASCII without a BOM = parser chaos).
# ============================================================================

[CmdletBinding()]
param(
  # Comma-separated list is accepted when run with -File (PS 5.1 does not split
  # arrays for -File): -Stage toolchain   or   -Stage repo,env,verify
  [string]$Stage = "all",
  [string]$Root = "C:\Zarlino",
  [string]$RepoDir = "",
  [string]$GitHubToken = $env:ZARLINO_GITHUB_TOKEN,
  [string]$RepoUrl = "https://github.com/zarlino-audio/Zarlino.git",
  [string]$Plugin = "ZTame",
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$Log = Join-Path $Root "buildhost.log"
if (-not (Test-Path $Root)) { New-Item -ItemType Directory -Force -Path $Root | Out-Null }
if (-not $RepoDir) { $RepoDir = Join-Path $Root "workspace" }

function Log([string]$msg) {
  # File only - the console output belongs to Ok/Info/Warn/Fail (no duplicates).
  $line = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $msg
  try { Add-Content -Path $Log -Value $line -Encoding UTF8 } catch { }
}
function Step([string]$t) { Write-Host ""; Write-Host ("=== " + $t) -ForegroundColor Cyan; Log ("STEP " + $t) }
function Ok([string]$m) { Write-Host ("    OK   " + $m) -ForegroundColor Green; Log ("OK   " + $m) }
function Info([string]$m) { Write-Host ("    ..   " + $m) -ForegroundColor Gray; Log ("..   " + $m) }
function Warn([string]$m) { Write-Host ("    WARN " + $m) -ForegroundColor Yellow; Log ("WARN " + $m) }
function Fail([string]$m) {
  Write-Host ("    FAIL " + $m) -ForegroundColor Red
  Log ("FAIL " + $m)
  if ($DryRun) { return }
  throw $m
}
function Invoke-Act([string]$what, [scriptblock]$act) {
  if ($DryRun) { Write-Host ("    DRY  " + $what) -ForegroundColor DarkGray; Log ("DRY  " + $what); return $null }
  Info $what
  return & $act
}
function Have([string]$exe) { return [bool](Get-Command $exe -ErrorAction SilentlyContinue) }

$stages = @($Stage -split "," | ForEach-Object { $_.Trim().ToLower() } | Where-Object { $_ })
foreach ($s in $stages) {
  if ($s -notin @("toolchain", "repo", "env", "verify", "backup", "all")) { throw "unknown stage '$s' (use toolchain|repo|env|verify|backup|all)" }
}
if ($stages -contains "all") { $stages = @("toolchain", "repo", "env", "verify", "backup") }
Log ("build-host provisioning start on {0} (stages: {1})" -f $env:COMPUTERNAME, ($stages -join ","))

# Shared: find the MSVC toolchain. vswhere is the only reliable way on a
# machine that may have several VS installs (or none).
$script:VsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
function Get-VsPath {
  if (-not (Test-Path $script:VsWhere)) { return "" }
  $p = & $script:VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
  return ($p | Select-Object -First 1)
}
function Get-ClExe {
  $vs = Get-VsPath
  if (-not $vs) { return "" }
  $cl = Get-ChildItem (Join-Path $vs "VC\Tools\MSVC") -Recurse -Filter cl.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
  if ($cl) { return $cl.FullName }
  return ""
}

# ---------------------------------------------------------------------------
# Stage: toolchain
# ---------------------------------------------------------------------------
if ($stages -contains "toolchain") {
  Step "Toolchain"

  $winget = Get-Command winget -ErrorAction SilentlyContinue
  if ($winget) { Ok "winget available" } else { Warn "winget not found - falling back to direct downloads" }

  # --- Visual Studio 2022 Build Tools (C++ workload) ------------------------
  $existingCl = Get-ClExe
  if ($existingCl) { Ok ("MSVC present: " + $existingCl) }
  else {
    if (Get-VsPath) { Info "a Visual Studio install exists but without the C++ workload - adding VCTools" }
    if ($winget) {
      Invoke-Act "winget install Microsoft.VisualStudio.2022.BuildTools (VCTools workload, silent, ~10 min)" {
        winget install --id Microsoft.VisualStudio.2022.BuildTools --accept-source-agreements --accept-package-agreements --silent `
          --override "--quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
      }
    }
    # Fallback (also used when winget reports success but installed nothing):
    if (-not (Get-ClExe)) {
      $vsExe = Join-Path $env:TEMP "vs_BuildTools.exe"
      Invoke-Act "download + install VS 2022 Build Tools directly from Microsoft (~10 min)" {
        Invoke-WebRequest "https://aka.ms/vs/17/release/vs_BuildTools.exe" -OutFile $vsExe -UseBasicParsing -TimeoutSec 600
        Start-Process $vsExe -ArgumentList "--quiet", "--wait", "--norestart", "--nocache", "--add", "Microsoft.VisualStudio.Workload.VCTools", "--includeRecommended" -Wait
      }
    }
    if (-not $DryRun) {
      if (Get-ClExe) { Ok ("MSVC installed: " + (Get-ClExe)) }
      else { Fail "MSVC is still missing after installation. Run the VS installer interactively on this VM (install the 'Desktop development with C++' workload), then re-run: -Stage env" }
    }
  }

  # --- CMake ---------------------------------------------------------------
  if (Have cmake) { Ok ("cmake present: " + (Get-Command cmake).Source) }
  else {
    if ($winget) {
      Invoke-Act "winget install Kitware.CMake" { winget install --id Kitware.CMake --accept-source-agreements --accept-package-agreements --silent }
    }
    if (-not (Have cmake)) {
      Invoke-Act "download CMake zip (installed to C:\Zarlino\tools\cmake)" {
        $zip = Join-Path $env:TEMP "cmake.zip"
        $api = Invoke-RestMethod "https://api.github.com/repos/Kitware/CMake/releases/latest" -UseBasicParsing -TimeoutSec 120
        $asset = $api.assets | Where-Object { $_.name -like "cmake-*-windows-x86_64.zip" } | Select-Object -First 1
        if (-not $asset) { throw "no cmake windows asset found" }
        Invoke-WebRequest $asset.browser_download_url -OutFile $zip -UseBasicParsing -TimeoutSec 600
        $dst = Join-Path $Root "tools"
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        Expand-Archive -Path $zip -DestinationPath $dst -Force
        $cmakeExe = Get-ChildItem $dst -Recurse -Filter cmake.exe | Select-Object -First 1
        $binDir = Split-Path -Parent $cmakeExe.FullName
        [Environment]::SetEnvironmentVariable("Path", [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + $binDir, "Machine")
        $env:Path += ";" + $binDir
      }
    }
    if (-not $DryRun -and (Have cmake)) { Ok ("cmake ready: " + (Get-Command cmake).Source) }
  }

  # --- Ninja ---------------------------------------------------------------
  if (Have ninja) { Ok ("ninja present: " + (Get-Command ninja).Source) }
  else {
    if ($winget) { Invoke-Act "winget install Ninja-build.Ninja" { winget install --id Ninja-build.Ninja --accept-source-agreements --accept-package-agreements --silent } }
    if (-not (Have ninja)) {
      Invoke-Act "download ninja zip into C:\Zarlino\tools" {
        $zip = Join-Path $env:TEMP "ninja.zip"
        Invoke-WebRequest "https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip" -OutFile $zip -UseBasicParsing -TimeoutSec 600
        $dst = Join-Path $Root "tools\ninja"
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        Expand-Archive -Path $zip -DestinationPath $dst -Force
        [Environment]::SetEnvironmentVariable("Path", [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + $dst, "Machine")
        $env:Path += ";" + $dst
      }
    }
  }

  # --- NSIS + Git ----------------------------------------------------------
  if (Test-Path "C:\Program Files (x86)\NSIS\makensis.exe") { Ok "NSIS present" }
  elseif ($winget) { Invoke-Act "winget install NSIS.NSIS" { winget install --id NSIS.NSIS --accept-source-agreements --accept-package-agreements --silent } }
  else { Warn "NSIS missing and no winget - installers (local_package_release) will fail until NSIS is installed" }

  if (Have git) { Ok ("git present: " + (Get-Command git).Source) }
  elseif ($winget) { Invoke-Act "winget install Git.Git" { winget install --id Git.Git --accept-source-agreements --accept-package-agreements --silent } }
  else { Warn "git missing and no winget - the repo stage cannot clone" }
}

# ---------------------------------------------------------------------------
# Stage: repo
# ---------------------------------------------------------------------------
if ($stages -contains "repo") {
  Step "Monorepo"
  if (-not $GitHubToken) { $GitHubToken = $env:ZARLINO_GITHUB_TOKEN }
  $gitDir = Join-Path $RepoDir ".git"
  if (Test-Path $gitDir) {
    Invoke-Act ("git pull + submodule update in " + $RepoDir) {
      Push-Location $RepoDir
      try {
        & git pull --ff-only
        & git submodule update --init --recursive --depth 1
      }
      finally { Pop-Location }
    }
    Ok "monorepo updated"
  }
  else {
    if (-not $GitHubToken -and -not $DryRun) { Fail "GITHUB_TOKEN required to clone the private monorepo (pass -GitHubToken or set ZARLINO_GITHUB_TOKEN)" }
    $authUrl = if ($GitHubToken) { $RepoUrl -replace "^https://", ("https://x-access-token:" + $GitHubToken + "@") } else { $RepoUrl }
    # JUCE is a submodule in this repo - without --recurse-submodules the build
    # configure fails on a missing JUCE/CMakeLists.txt.
    Invoke-Act ("git clone (with submodules: JUCE) into " + $RepoDir) { & git clone --depth 1 --recurse-submodules --shallow-submodules $authUrl $RepoDir }
    if (-not $DryRun) {
      if (-not (Test-Path (Join-Path $RepoDir "CMakeLists.txt"))) { Fail "clone finished but CMakeLists.txt is missing - check the token's repo scope" }
      if (-not (Test-Path (Join-Path $RepoDir "JUCE\CMakeLists.txt"))) { Warn "JUCE submodule is empty - run: git submodule update --init --recursive" }
      Ok "monorepo cloned (submodules included)"
    }
  }
}

# ---------------------------------------------------------------------------
# Stage: env (a build environment the daemon can use without an interactive shell)
# ---------------------------------------------------------------------------
if ($stages -contains "env") {
  Step "Build environment"
  $vsPath = Get-VsPath
  $clExe = Get-ClExe
  if (-not $vsPath -or -not $clExe) {
    if ($DryRun) { Warn "MSVC not detected - the env stage would fail on a real run (install the toolchain first)" }
    else { Fail "MSVC toolchain not found (no C++ workload). Run: powershell -File <this script> -Stage toolchain   then re-run -Stage env" }
  }
  Ok ("visual studio: " + $vsPath)
  Ok ("compiler: " + $clExe)
  $envFile = Join-Path $Root "build-env.ps1"
  $lines = @(
    "# Zarlino build environment (generated by provision-buildhost.ps1)",
    "# Dot-source this in any shell that must build:  . C:\Zarlino\build-env.ps1",
    ('$env:ZARLINO_REPO = "' + $RepoDir + '"'),
    ('$env:VSINSTALLDIR = "' + $vsPath + '"'),
    '# Re-resolve Visual Studio at load time (a repair/update can move it).',
    '$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"',
    'if (-not (Test-Path $env:VSINSTALLDIR) -and (Test-Path $vswhere)) {',
    '  $env:VSINSTALLDIR = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)',
    '}',
    '$vsDevCmd = Join-Path $env:VSINSTALLDIR "Common7\Tools\VsDevCmd.bat"',
    'if (-not (Test-Path $vsDevCmd)) { throw "MSVC toolchain not found - run: provision-buildhost.ps1 -Stage toolchain" }',
    '# Assemble the toolchain environment DIRECTLY instead of parsing cmd output.',
    '# Two traps this avoids, each of which cost a real debugging session:',
    '#   1. an inline cmd /c "`"$path`" ... && set" loses its quoting when this file is',
    '#      dot-sourced, so the `&& set` silently never runs;',
    '#   2. cmd`s `set` WRAPS long values, so a line-by-line import truncates PATH and',
    '#      drops the MSVC directories (appended last) - the compiler then looks missing',
    '#      even though it is installed.',
    '$msvc = Get-ChildItem (Join-Path $env:VSINSTALLDIR "VC\Tools\MSVC") -Directory -ErrorAction SilentlyContinue |',
    '  Sort-Object Name -Descending | Select-Object -First 1',
    'if (-not $msvc) { throw "no MSVC toolset under $env:VSINSTALLDIR\VC\Tools\MSVC" }',
    '$msvcBin = Join-Path $msvc.FullName "bin\Hostx64\x64"',
    '$msvcInc = Join-Path $msvc.FullName "include"',
    '$msvcLib = Join-Path $msvc.FullName "lib\x64"',
    '$sdkRoot = "C:\Program Files (x86)\Windows Kits\10"',
    '$sdkVer = Get-ChildItem (Join-Path $sdkRoot "Include") -Directory -ErrorAction SilentlyContinue |',
    '  Where-Object { $_.Name -match "^\d+\.\d+" } | Sort-Object Name -Descending | Select-Object -First 1',
    'if (-not $sdkVer) { throw "no Windows SDK found under $sdkRoot\Include" }',
    '$sdkInc = Join-Path $sdkRoot ("Include\" + $sdkVer.Name)',
    '$sdkLib = Join-Path $sdkRoot ("Lib\" + $sdkVer.Name)',
    '$sdkBin = Get-ChildItem (Join-Path $sdkRoot "bin") -Directory -ErrorAction SilentlyContinue |',
    '  Where-Object { $_.Name -match "^\d+\.\d+" } | Sort-Object Name -Descending | Select-Object -First 1',
    '# PATH: compiler first, then the SDK tools; the rest of the machine PATH follows.',
    '$env:Path = @($msvcBin, (Join-Path $env:VSINSTALLDIR "Common7\IDE\VC\VCPackages")) +',
    '  $(if ($sdkBin) { @((Join-Path $sdkBin.FullName "x64")) } else { @() }) +',
    '  @($env:Path) -join ";"',
    '$env:INCLUDE = @($msvcInc, (Join-Path $env:VSINSTALLDIR "VC\Auxiliary\VS\include"),',
    '  (Join-Path $sdkInc "ucrt"), (Join-Path $sdkInc "um"), (Join-Path $sdkInc "shared"),',
    '  (Join-Path $sdkInc "winrt"), (Join-Path $sdkInc "cppwinrt")) -join ";"',
    '$env:LIB = @($msvcLib, (Join-Path $sdkLib "ucrt\x64"), (Join-Path $sdkLib "um\x64")) -join ";"',
    '$env:VCToolsInstallDir = $msvc.FullName + "\"',
    '$env:WindowsSdkDir = $sdkRoot + "\"',
    '$env:WindowsSDKVersion = $sdkVer.Name + "\"',
    'if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw "cl.exe is not on PATH after loading VsDevCmd - the MSVC toolchain is broken; run: provision-buildhost.ps1 -Stage toolchain" }',    'if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { Write-Warning "ninja is not on PATH - run: provision-buildhost.ps1 -Stage toolchain" }',
    'if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Write-Warning "cmake is not on PATH - run: provision-buildhost.ps1 -Stage toolchain" }'
  )
  Invoke-Act ("write " + $envFile) { [IO.File]::WriteAllLines($envFile, $lines, (New-Object Text.UTF8Encoding($false))) }
  Ok ("build environment written (repo: " + $RepoDir + ")")

  # A wrapper the OS daemon (or any scheduler) can call: builds a plugin and
  # leaves the artifacts where the release pipeline expects them.
  $buildPs1 = Join-Path $Root "build-plugin.ps1"
  $buildLines = @(
    "param([string]`$Plugin = `"$Plugin`", [string]`$Configuration = `"Release`")",
    "Set-Location `"$RepoDir`"",
    ". `"$envFile`"",
    "& .\tools\build-release-local.ps1 -Plugin `$Plugin -Configuration `$Configuration"
  )
  Invoke-Act ("write " + $buildPs1) { [IO.File]::WriteAllLines($buildPs1, $buildLines, (New-Object Text.UTF8Encoding($false))) }
  Ok "build wrapper written (C:\Zarlino\build-plugin.ps1)"
}

# ---------------------------------------------------------------------------
# Stage: verify (this is the acceptance test - a real build must succeed here)
# ---------------------------------------------------------------------------
if ($stages -contains "verify") {
  Step ("Verify: configure + build " + $Plugin)
  $envFile = Join-Path $Root "build-env.ps1"
  if (-not $DryRun -and -not (Test-Path (Join-Path $RepoDir "CMakeLists.txt"))) { Fail "repo missing - run -Stage repo first" }
  if (-not $DryRun -and -not (Test-Path (Join-Path $RepoDir "JUCE\CMakeLists.txt"))) { Fail "JUCE submodule missing - run: git -C $RepoDir submodule update --init --recursive" }
  if ($DryRun) {
    Write-Host ("    DRY  . C:\Zarlino\build-env.ps1; cmake -S {0} -B {0}\build-vm -G Ninja -DCMAKE_BUILD_TYPE=Release; ninja -C {0}\build-vm {1}_All; ctest --test-dir {0}\build-vm -L release-gate -R ^{1}\." -f $RepoDir, $Plugin) -ForegroundColor DarkGray
  }
  else {
    . $envFile
    if (-not (Have cl.exe)) { Fail "cl.exe is not on PATH after loading build-env.ps1 - the MSVC toolchain is missing (run -Stage toolchain)" }
    if (-not (Have cmake)) { Fail "cmake not on PATH after env load" }
    if (-not (Have ninja)) { Fail "ninja not on PATH after env load" }
    $buildDir = Join-Path $RepoDir "build-vm"
    Push-Location $RepoDir
    try {
      Info "cmake configure (Ninja, Release)"
      & cmake -S . -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
      if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }
      Ok "configured"
      Info ("ninja " + $Plugin + "_All (this is the slow step; LTO links take minutes)")
      & ninja -C $buildDir ("{0}_All" -f $Plugin)
      if ($LASTEXITCODE -ne 0) { Fail ("ninja " + $Plugin + "_All failed") }
      Ok "built VST3 + Standalone"
      Info "ctest release-gate"
      & ctest --test-dir $buildDir -L release-gate -R ("^{0}\." -f $Plugin) --output-on-failure
      if ($LASTEXITCODE -ne 0) { Fail "release-gate tests failed" }
      Ok "release-gate passed"
    }
    finally { Pop-Location }
    $vst3 = Get-ChildItem $buildDir -Recurse -Filter ("{0}*.vst3" -f $Plugin) -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($vst3) { Ok ("artifact present: " + $vst3.FullName) } else { Warn "no .vst3 found in the build dir - check the bundle output path" }
  }
}

# ---------------------------------------------------------------------------
# Stage: backup (the nightly job moves here with the rest)
# ---------------------------------------------------------------------------
if ($stages -contains "backup") {
  Step "Nightly backup on this host"
  $backupPs1 = Join-Path $Root "nightly-backup.ps1"
  $body = @(
    "# Zarlino nightly backup (generated) - cloud data is already backed up by the OS;",
    "# this keeps a second, independent copy of the code + config on this host.",
    ('$dest = Join-Path "C:\Zarlino\backups" (Get-Date -Format "yyyy-MM-dd")'),
    'New-Item -ItemType Directory -Force -Path $dest | Out-Null',
    ('if (Test-Path "' + $RepoDir + '\.git") { Push-Location "' + $RepoDir + '"; try { git bundle create (Join-Path $dest "workspace.bundle") --all } finally { Pop-Location } }'),
    ('Copy-Item "' + (Join-Path $Root "exec-os\.dev.vars") + '" $dest -ErrorAction SilentlyContinue'),
    '# keep 14 days',
    'Get-ChildItem "C:\Zarlino\backups" -Directory | Where-Object { $_.Name -lt (Get-Date).AddDays(-14).ToString("yyyy-MM-dd") } | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue'
  )
  Invoke-Act ("write " + $backupPs1) { [IO.File]::WriteAllLines($backupPs1, $body, (New-Object Text.UTF8Encoding($false))) }
  Invoke-Act "register scheduled task 'Zarlino Nightly Backup' (02:30 daily)" {
    schtasks /create /tn "Zarlino Nightly Backup" /tr ("powershell -NoProfile -ExecutionPolicy Bypass -File `"" + $backupPs1 + "`"") /sc daily /st 02:30 /ru SYSTEM /f
  }
  Ok "nightly backup registered on this host"
}

Write-Host ""
Write-Host "=== Build host provisioning finished" -ForegroundColor Cyan
Log "build-host provisioning finished"
Write-Host ("    log: " + $Log)
Write-Host ("    build wrapper: C:\Zarlino\build-plugin.ps1   (used by the OS daemon / local tasks)")
