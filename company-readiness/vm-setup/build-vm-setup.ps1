# ============================================================================
#  Build + publish the Zarlino VM setup kit
# ----------------------------------------------------------------------------
#  Produces:
#    company-readiness\vm-setup\ZarlinoVM-Setup-<ver>.exe   (the one-click installer)
#    company-readiness\vm-setup\zarlino-vm-payload.zip      (exec-os payload the exe pulls)
#    company-readiness\vm-setup\vm-setup-manifest.json      (version + sha256 + sizes)
#
#  -Publish uploads both files to the OS (os.zarlinoaudio.com/api/artifacts)
#  using the founder token from the exec-os .dev.vars, so the installer is
#  downloadable from the OS console.
#
#  ASCII only (PowerShell 5.1 + non-ASCII without BOM = parser chaos).
# ============================================================================

[CmdletBinding()]
param(
  [string]$Version = "1.0.0",
  [string]$ExecOsRepo = "C:\Users\khrit\Desktop\zarlino-executive-os",
  [string]$OsUrl = "https://os.zarlinoaudio.com",
  [switch]$Publish,
  [switch]$SkipExe,
  [switch]$SkipPayload
)

$ErrorActionPreference = "Stop"
$kitDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$makensis = "C:\Program Files (x86)\NSIS\makensis.exe"

function Ok($m) { Write-Host ("  OK   " + $m) -ForegroundColor Green }
function Info($m) { Write-Host ("  ..   " + $m) -ForegroundColor Gray }
function Fail($m) { Write-Host ("  FAIL " + $m) -ForegroundColor Red; exit 1 }

Write-Host "=== Zarlino VM setup build $Version" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# 1. Payload: the exec-os source the VM needs (no secrets, no node_modules)
# ---------------------------------------------------------------------------
$payloadPath = Join-Path $kitDir "zarlino-vm-payload.zip"
if ($SkipPayload) { Info "payload build skipped" }
else {
  if (-not (Test-Path $ExecOsRepo)) { Fail "exec-os repo not found at $ExecOsRepo" }
  Info ("packing payload from {0}" -f $ExecOsRepo)
  $stage = Join-Path $env:TEMP ("zarlino-payload-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
  $stageRoot = Join-Path $stage "exec-os"
  New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

  $excludeDirs = @("node_modules", ".git", ".wrangler", "dist-worker", ".local", "test", "_rec", "artifacts", "build", "build-ci", "build-clean-zscorch", "build-msvc", "build-tmp", "build-vs-ci", "build-zscorch-repro", "build-zscorch-vs2026")
  # Everything the executor + bridge need at runtime, nothing else.
  $includeDirs = @("src", "scripts", "tools")
  foreach ($d in $includeDirs) {
    $src = Join-Path $ExecOsRepo $d
    if (-not (Test-Path $src)) { continue }
    $dst = Join-Path $stageRoot $d
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Get-ChildItem -Path $src -Recurse -File | Where-Object {
      $rel = $_.FullName.Substring($src.Length)
      $skip = $false
      foreach ($ex in $excludeDirs) { if ($rel -match ("\\" + [Regex]::Escape($ex) + "\\")) { $skip = $true } }
      if ($_.Name -like "_*.txt") { $skip = $true }
      if ($_.Name -eq ".dev.vars" -or $_.Name -like "*.dev.vars*") { $skip = $true }
      -not $skip
    } | ForEach-Object {
      $target = Join-Path $dst $_.FullName.Substring($src.Length + 1)
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
      Copy-Item $_.FullName $target -Force
    }
  }
  foreach ($f in @("package.json", "package-lock.json", "tsconfig.json")) {
    $src = Join-Path $ExecOsRepo $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $stageRoot $f) -Force }
  }
  # Refuse to ship secrets by accident.
  $leak = Get-ChildItem -Path $stageRoot -Recurse -File | Where-Object { $_.Name -match "dev\.vars|\.env$|\.pem$|_pem" }
  if ($leak) { Remove-Item -Recurse -Force $stage; Fail ("payload would contain secret-looking files: " + (($leak | ForEach-Object { $_.Name }) -join ", ")) }

  if (Test-Path $payloadPath) { Remove-Item $payloadPath -Force }
  Compress-Archive -Path (Join-Path $stage "exec-os") -DestinationPath $payloadPath -CompressionLevel Optimal
  Remove-Item -Recurse -Force $stage
  $p = Get-Item $payloadPath
  Ok ("payload {0:N2} MB" -f ($p.Length / 1MB))
}

# ---------------------------------------------------------------------------
# 2. Installer exe (C# launcher via the in-box compiler)
#    NSIS was tried first and Defender flagged the unsigned stub as
#    Trojan:Win32/Bearfoos.A!ml (a known false positive). A managed PE does not
#    trip that heuristic; a signed NSIS build can return once a cert exists.
# ---------------------------------------------------------------------------
$exePath = Join-Path $kitDir ("ZarlinoVM-Setup-{0}.exe" -f $Version)
if ($SkipExe) { Info "exe build skipped" }
else {
  $csc = "$env:SystemRoot\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
  if (-not (Test-Path $csc)) {
    $alt = Get-ChildItem "$env:SystemRoot\Microsoft.NET\Framework*\v4.0.30319\csc.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($alt) { $csc = $alt.FullName } else { Fail "csc.exe (in-box .NET compiler) not found" }
  }
  $launcherDir = Join-Path $kitDir "launcher"
  $versionStub = Join-Path $launcherDir "_build_version.cs"
  $stubBody = @(
    "// generated by build-vm-setup.ps1 - do not edit",
    "internal static class BuildInfoVersionStub {",
    "    // placeholder replaced below"
  )
  # Compile the launcher with the real version baked in.
  $src = Join-Path $launcherDir "ZarlinoVmSetup.cs"
  $patched = Join-Path $launcherDir "_build_launcher.cs"
  $code = [IO.File]::ReadAllText($src) -replace 'public const string Version = "[^"]*";', ('public const string Version = "' + $Version + '";')
  [IO.File]::WriteAllText($patched, $code, (New-Object Text.UTF8Encoding($false)))
  Info ("csc -> {0}" -f (Split-Path -Leaf $exePath))
  & $csc /nologo /target:exe /platform:anycpu /optimize+ /win32manifest:"$launcherDir\vm-setup.manifest" `
    /resource:"$kitDir\bootstrap.ps1",bootstrap.ps1 /resource:"$kitDir\README.md",README.md /resource:"$kitDir\provision-buildhost.ps1",provision-buildhost.ps1 `
    /out:"$exePath" "$patched" | Out-Null
  Remove-Item $patched -ErrorAction SilentlyContinue
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exePath)) { Fail "csc failed with exit code $LASTEXITCODE" }
  $e = Get-Item $exePath
  Ok ("installer {0} ({1:N0} KB)" -f $e.Name, ($e.Length / 1KB))
}

# ---------------------------------------------------------------------------
# 3. Manifest (what the OS serves + how to verify the download)
# ---------------------------------------------------------------------------
function Sha256($path) { if (Test-Path $path) { (Get-FileHash -Algorithm SHA256 $path).Hash.ToLower() } else { $null } }
$manifest = [ordered]@{
  name        = "zarlino-vm-setup"
  version     = $Version
  built_at    = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
  installer   = [ordered]@{ file = (Split-Path -Leaf $exePath); sha256 = (Sha256 $exePath); bytes = if (Test-Path $exePath) { (Get-Item $exePath).Length } else { 0 } }
  payload     = [ordered]@{ file = "zarlino-vm-payload.zip"; key = "artifacts/vm-setup/zarlino-vm-payload.zip"; sha256 = (Sha256 $payloadPath); bytes = if (Test-Path $payloadPath) { (Get-Item $payloadPath).Length } else { 0 } }
  artifacts   = [ordered]@{ installer_key = "artifacts/vm-setup/$((Split-Path -Leaf $exePath).ToLower())"; manifest_key = "artifacts/vm-setup/vm-setup-manifest.json" }
  requirements = @("Windows Server 2016+ / Windows 10+", "Administrator", "outbound 443 to os.zarlinoaudio.com", "5 GB free disk")
  manual_steps = @("Start/allocate the cloud VM", "Run the installer inside the VM", "Paste the founder token", "Scan the WhatsApp QR once")
  not_included = @("no secrets embedded", "no Visual Studio / build toolchain", "does not create the cloud VM itself")
}
$manifestPath = Join-Path $kitDir "vm-setup-manifest.json"
$manifestJson = $manifest | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText($manifestPath, $manifestJson, (New-Object Text.UTF8Encoding($false)))
Ok "manifest written"

# ---------------------------------------------------------------------------
# 4. Publish to the OS (founder-authenticated)
# ---------------------------------------------------------------------------
if (-not $Publish) {
  Write-Host ""
  Write-Host "  Tip: add -Publish to upload the installer + payload to the OS so it is downloadable from the console." -ForegroundColor Yellow
  exit 0
}

$devVars = Join-Path $ExecOsRepo ".dev.vars"
if (-not (Test-Path $devVars)) { Fail "no .dev.vars in $ExecOsRepo - cannot publish" }
$token = (Get-Content $devVars | Where-Object { $_ -like "FOUNDER_AUTH_TOKEN=*" } | Select-Object -First 1) -replace "^FOUNDER_AUTH_TOKEN=", ""
if (-not $token) { Fail "FOUNDER_AUTH_TOKEN missing from .dev.vars" }
$headers = @{ Authorization = "Bearer $token" }

function Publish-Artifact([string]$path, [string]$name, [string]$kind, [string]$mime) {
  if (-not (Test-Path $path)) { Fail "missing file: $path" }
  $bytes = [IO.File]::ReadAllBytes($path)
  $body = @{ name = $name; kind = $kind; mime = $mime; data_base64 = [Convert]::ToBase64String($bytes) } | ConvertTo-Json -Compress
  Info ("upload {0} ({1:N0} KB)" -f $name, ($bytes.Length / 1KB))
  $resp = Invoke-RestMethod ("{0}/api/artifacts" -f $OsUrl.TrimEnd("/")) -Method Post -Headers $headers -ContentType "application/json" -Body $body -TimeoutSec 600
  if (-not $resp.ok) { Fail ("upload rejected: " + ($resp | ConvertTo-Json -Compress)) }
  Ok ("published -> {0}" -f $resp.key)
  return $resp.key
}

Publish-Artifact $payloadPath "zarlino-vm-payload.zip" "vm-setup" "application/zip" | Out-Null
$exeKey = $null
if (-not $SkipExe) { $exeKey = Publish-Artifact $exePath (Split-Path -Leaf $exePath) "vm-setup" "application/vnd.microsoft.portable-executable" }
$manifest.artifacts.installer_key = $exeKey
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
Publish-Artifact $manifestPath "vm-setup-manifest.json" "vm-setup" "application/json" | Out-Null

Write-Host ""
Ok "published. Download (founder bearer): $OsUrl/api/artifacts/$exeKey"
Write-Host ("     sha256: " + $manifest.installer.sha256) -ForegroundColor Yellow
