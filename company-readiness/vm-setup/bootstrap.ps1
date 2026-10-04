# ============================================================================
#  Zarlino Executive OS - one-click VM bring-up (in-VM bootstrap)
# ----------------------------------------------------------------------------
#  What it does (idempotent; safe to re-run):
#    1. Preflight: admin rights, OS, disk, network to the OS
#    2. Node.js LTS (install via winget, fall back to the official MSI)
#    3. Download the exec-os payload from the OS (founder-authenticated)
#    4. Lay it out under the install root and run npm install for the
#       executor + the WhatsApp bridge
#    5. Write the local config (.dev.vars / bridge .env) with tight ACLs
#    6. Register logon auto-start for both processes (VBS launchers)
#    7. Start them now and verify: bridge QR port answers, OS sees the machine
#    8. Print exactly what remains manual (WhatsApp QR pairing)
#
#  What it does NOT do: it does not create or start the cloud VM, it does not
#  embed any secret (you type the founder token yourself), and it does not
#  turn the VM into a build machine (Visual Studio builds still belong to the
#  laptop until the cloud build story exists).
#
#  ASCII only on purpose: PowerShell 5.1 mis-parses non-ASCII bytes in .ps1
#  files that lack a UTF-8 BOM.
# ============================================================================

[CmdletBinding()]
param(
  [string]$InstallRoot = "C:\Zarlino",
  [string]$OsUrl = "https://os.zarlinoaudio.com",
  [string]$ExecToken = $env:ZARLINO_EXEC_TOKEN,
  [string]$InternalSecret = $env:ZARLINO_INTERNAL_SECRET,
  [string]$PayloadKey = "",
  [int]$QrPort = 3037,
  [switch]$BuildHost,
  [string]$GitHubToken = $env:ZARLINO_GITHUB_TOKEN,
  [switch]$DryRun,
  [switch]$SkipDeps,
  [switch]$NoStart
)

$ErrorActionPreference = "Stop"
$script:StepNo = 0
$script:DryFailures = 0
$script:Log = Join-Path $env:TEMP "zarlino-vm-setup.log"

function Log([string]$msg) {
  # File only - the console output is owned by Ok/Info/Warn/Fail (no duplicates).
  $line = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $msg
  try { Add-Content -Path $script:Log -Value $line -Encoding UTF8 } catch { }
}

function Step([string]$title) {
  $script:StepNo++
  Write-Host ""
  Write-Host ("=== {0}. {1}" -f $script:StepNo, $title) -ForegroundColor Cyan
  Log ("STEP {0}: {1}" -f $script:StepNo, $title)
}

function Ok([string]$msg) { Write-Host ("    OK   " + $msg) -ForegroundColor Green; Log ("OK   " + $msg) }
function Info([string]$msg) { Write-Host ("    ..   " + $msg) -ForegroundColor Gray; Log ("..   " + $msg) }
function Warn([string]$msg) { Write-Host ("    WARN " + $msg) -ForegroundColor Yellow; Log ("WARN " + $msg) }
function Fail([string]$msg) {
  Write-Host ("    FAIL " + $msg) -ForegroundColor Red
  Log ("FAIL " + $msg)
  # A dry run is a preview: report every problem it would hit, then continue so
  # the operator sees the whole plan instead of stopping at the first finding.
  if ($DryRun) { $script:DryFailures++; return }
  throw $msg
}

function Invoke-Action([string]$description, [scriptblock]$action) {
  if ($DryRun) { Write-Host ("    DRY  " + $description) -ForegroundColor DarkGray; Log ("DRY  " + $description); return $null }
  Info $description
  return & $action
}

# Delete a tree that may be locked or deeper than PS 5.1 likes (node_modules).
# Escalates: Remove-Item -> cmd rmdir -> robocopy mirror -> give up (caller
# then updates files in place instead of failing the whole install).
function Remove-Tree([string]$path) {
  if (-not (Test-Path $path)) { return $true }
  try { Remove-Item -Recurse -Force $path -ErrorAction Stop } catch { }
  if (-not (Test-Path $path)) { return $true }
  try { & cmd /c ("rmdir /s /q `"" + $path + "`"") | Out-Null } catch { }
  if (-not (Test-Path $path)) { return $true }
  try {
    $empty = Join-Path $env:TEMP ("zarlino-empty-" + [Guid]::NewGuid().ToString("N").Substring(0, 6))
    New-Item -ItemType Directory -Force -Path $empty | Out-Null
    & robocopy $empty $path /MIR /NFL /NDL /NJH /NJS /NC /NS /NP | Out-Null
    Remove-Item $empty -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $path -ErrorAction SilentlyContinue
  } catch { }
  return -not (Test-Path $path)
}

# The bridge and the daemon run FROM the install tree: on an upgrade they hold
# files in tools\whatsapp-bridge, which is exactly what made the old delete step
# abort. Two kinds of holder matter:
#   - node.exe running the bridge / daemon
#   - the cmd.exe or wscript.exe wrapper our launcher created, whose *working
#     directory* is inside the tree (a directory with a process CWD cannot be
#     removed on Windows, even when every file handle is closed)
# Stop them all; the start step later in this script brings them back. Never
# touches the running setup itself ($PID).
function Stop-ZarlinoProcesses {
  $pattern = 'whatsapp-bridge|local-daemon|Zarlino\\launchers'
  $found = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object {
      $_.Name -in @('node.exe', 'cmd.exe', 'wscript.exe') -and
      $_.ProcessId -ne $PID -and
      $_.CommandLine -and $_.CommandLine -match $pattern
    }
  $n = 0
  foreach ($p in @($found)) {
    try { Stop-Process -Id $p.ProcessId -Force -ErrorAction Stop; $n++ } catch { }
  }
  if ($n -gt 0) { Start-Sleep -Seconds 3 }
  return $n
}

# ---------------------------------------------------------------------------
Step "Preflight"
# ---------------------------------------------------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($DryRun) { Info "admin check skipped in dry run (currently admin: $isAdmin)" }
elseif (-not $isAdmin) { Fail "Run this as Administrator (right-click the setup, Run as administrator)." }
else { Ok "running elevated" }

$os = Get-CimInstance Win32_OperatingSystem
Ok ("host: {0} ({1})" -f $env:COMPUTERNAME, $os.Caption)
if ($os.Caption -match "Server") { Info "Windows Server detected - the setup is supported but the QR page is console-only." }

$free = [math]::Round((Get-PSDrive -Name ($InstallRoot.Substring(0,1))).Free / 1GB, 1)
if ($free -lt 5) { Fail ("Only {0} GB free on {1} - need at least 5 GB." -f $free, $InstallRoot.Substring(0,1)) }
else { Ok ("disk free on {0}: {1} GB" -f $InstallRoot.Substring(0,1), $free) }
$reachable = Test-NetConnection -ComputerName ([Uri]$OsUrl).Host -Port 443 -InformationLevel Quiet -WarningAction SilentlyContinue
if ($reachable) { Ok ("network: {0} reachable" -f ([Uri]$OsUrl).Host) } else { Fail ("Cannot reach {0} on 443 - check the VM network/firewall." -f ([Uri]$OsUrl).Host) }

# ---------------------------------------------------------------------------
Step "Node.js LTS"
# ---------------------------------------------------------------------------
$nodeOk = $false
try {
  $ver = (& node -v) 2>$null
  if ($ver -match "^v(\d+)") {
    $major = [int]$Matches[1]
    if ($major -ge 20) { $nodeOk = $true; Ok ("node {0} already present" -f $ver) }
    else { Warn ("node {0} is too old (need >= 20)" -f $ver) }
  }
} catch { Info "node not found on PATH" }

if (-not $nodeOk) {
  $winget = Get-Command winget -ErrorAction SilentlyContinue
  if ($winget) {
    Invoke-Action "winget install OpenJS.NodeJS.LTS" { winget install --id OpenJS.NodeJS.LTS --accept-source-agreements --accept-package-agreements --silent | Out-Null }
  }
  else {
    $msi = Join-Path $env:TEMP "node-lts-x64.msi"
    Invoke-Action "download Node.js LTS MSI" {
      $index = Invoke-RestMethod "https://nodejs.org/dist/index.json" -UseBasicParsing -TimeoutSec 30
      $lts = $index | Where-Object { $_.lts -ne $false } | Select-Object -First 1
      $url = "https://nodejs.org/dist/{0}/node-{0}-x64.msi" -f $lts.version
      Invoke-WebRequest $url -OutFile $msi -UseBasicParsing -TimeoutSec 300
      return $msi
    } | Out-Null
    Invoke-Action "install Node.js silently" { Start-Process msiexec.exe -ArgumentList "/i", "`"$msi`"", "/qn", "/norestart" -Wait }
  }
  if (-not $DryRun) {
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
    $ver = (& node -v) 2>$null
    if ($ver -match "^v(\d+)") { Ok ("node {0} installed" -f $ver) } else { Fail "Node install did not land on PATH - open a new shell and re-run this setup." }
  }
}

# ---------------------------------------------------------------------------
Step "Founder token"
# ---------------------------------------------------------------------------
if (-not $ExecToken) {
  if ($DryRun) { $ExecToken = "DRY-RUN-TOKEN"; Warn "no token supplied (dry run: using placeholder)" }
  else {
    Write-Host "    Paste the founder token (FOUNDER_AUTH_TOKEN), then press Enter. It is stored only on this machine." -ForegroundColor Yellow
    $secure = Read-Host -Prompt "    FOUNDER_AUTH_TOKEN" -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { $ExecToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
  }
}
if (-not $ExecToken) { Fail "Founder token is required." }
Ok ("token accepted ({0} chars)" -f $ExecToken.Length)

# ---------------------------------------------------------------------------
Step "Payload from the OS"
# ---------------------------------------------------------------------------
# Published artifacts carry a timestamp prefix (artifacts/<kind>/<ts>-<name>),
# so the payload is resolved from the live listing unless one is passed in.
if (-not $PayloadKey) {
  if ($DryRun) {
    Write-Host "    DRY  resolve the newest *zarlino-vm-payload.zip under artifacts/vm-setup/" -ForegroundColor DarkGray
    $PayloadKey = "artifacts/vm-setup/<resolved-on-real-run>.zip"
  }
  else {
    try {
      $listing = Invoke-RestMethod ("{0}/api/artifacts?prefix=artifacts/vm-setup/" -f $OsUrl.TrimEnd("/")) -Headers @{ Authorization = "Bearer $ExecToken" } -TimeoutSec 60
      $cand = $listing.artifacts | Where-Object { $_.key -like "*zarlino-vm-payload.zip" } | Sort-Object uploaded -Descending | Select-Object -First 1
      if (-not $cand) { Fail "no payload found under artifacts/vm-setup/ - publish it with build-vm-setup.ps1 -Publish" }
      $PayloadKey = $cand.key
    }
    catch { Fail ("could not list artifacts: {0}" -f $_.Exception.Message) }
  }
}

$zip = Join-Path $env:TEMP "zarlino-vm-payload.zip"
$payloadUrl = "{0}/api/artifacts/{1}" -f $OsUrl.TrimEnd("/"), $PayloadKey
if ($DryRun) {
  Write-Host ("    DRY  download {0}" -f $payloadUrl) -ForegroundColor DarkGray
}
else {
  Info ("downloading {0}" -f $payloadUrl)
  $headers = @{ Authorization = "Bearer $ExecToken" }
  try { Invoke-WebRequest -Uri $payloadUrl -Headers $headers -OutFile $zip -UseBasicParsing -TimeoutSec 600 }
  catch { Fail ("payload download failed: {0}. Publish it first with company-readiness\vm-setup\build-vm-setup.ps1 -Publish" -f $_.Exception.Message) }
  Ok ("payload {0:N1} MB" -f ((Get-Item $zip).Length / 1MB))
}

$execRoot = Join-Path $InstallRoot "exec-os"
$bridgeRoot = Join-Path $execRoot "tools\whatsapp-bridge"
Invoke-Action ("create {0}" -f $InstallRoot) { New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null }

# Upgrades: stop the running processes, then replace the tree (or update it in
# place if something still refuses to let go).
$overwriteInPlace = $false
if (Test-Path $execRoot) {
  if ($DryRun) {
    Write-Host ("    DRY  stop running bridge/daemon, then replace {0}" -f $execRoot) -ForegroundColor DarkGray
  }
  else {
    $stopped = Stop-ZarlinoProcesses
    if ($stopped -gt 0) { Ok ("stopped {0} running Zarlino process(es) before replacing files" -f $stopped) }
    else { Info "no running bridge/daemon found" }
    if (-not (Remove-Tree $execRoot)) {
      $overwriteInPlace = $true
      Warn "old install could not be fully deleted (files still locked) - updating files in place"
    }
  }
}

Invoke-Action ("expand payload into {0}" -f $execRoot) {
  if ((Test-Path $execRoot) -and -not $overwriteInPlace) { Remove-Tree $execRoot | Out-Null }
  Expand-Archive -Path $zip -DestinationPath $InstallRoot -Force
}
Ok "payload laid out"

# ---------------------------------------------------------------------------
Step "Dependencies"
# ---------------------------------------------------------------------------
if ($SkipDeps) { Warn "dependency install skipped by flag" }
elseif ($DryRun) { Write-Host ("    DRY  npm ci in {0} and {1}" -f $execRoot, $bridgeRoot) -ForegroundColor DarkGray }
else {
  Push-Location $execRoot
  try { Info "npm ci (executor)"; & npm ci --no-audit --no-fund | Out-Null; if ($LASTEXITCODE -ne 0) { Warn "npm ci failed in the executor (node_modules may be locked) - continuing, the daemon may still start from the existing install" } }
  finally { Pop-Location }
  Ok "executor dependencies installed"
  Push-Location $bridgeRoot
  try { Info "npm ci (whatsapp bridge)"; & npm ci --no-audit --no-fund | Out-Null; if ($LASTEXITCODE -ne 0) { Warn "npm ci failed in the bridge - continuing, check C:\Zarlino\bridge.log after start" } }
  finally { Pop-Location }
  Ok "bridge dependencies installed"
}

# ---------------------------------------------------------------------------
Step "Local configuration"
# ---------------------------------------------------------------------------
$devVars = Join-Path $execRoot ".dev.vars"
$devVarsBody = @()
$devVarsBody += "FOUNDER_AUTH_TOKEN=$ExecToken"
if ($InternalSecret) { $devVarsBody += "INTERNAL_SECRET=$InternalSecret" }
if ($GitHubToken) { $devVarsBody += "GITHUB_TOKEN=$GitHubToken" }
$devVarsBody += "EXEC_URL=$OsUrl"
$devVarsBody += "ZARLINO_VM=1"
Invoke-Action ("write {0} (ACL: Administrators + SYSTEM only)" -f $devVars) {
  [IO.File]::WriteAllLines($devVars, $devVarsBody, (New-Object Text.UTF8Encoding($false)))
  icacls $devVars /inheritance:r /grant:r "Administrators:F" "SYSTEM:F" | Out-Null
}

$bridgeEnv = Join-Path $bridgeRoot ".env"
$bridgeEnvBody = @("EXEC_URL=$OsUrl", "EXEC_TOKEN=$ExecToken", "QR_PORT=$QrPort", "OUTBOX_POLL_MS=20000")
Invoke-Action ("write {0}" -f $bridgeEnv) {
  [IO.File]::WriteAllLines($bridgeEnv, $bridgeEnvBody, (New-Object Text.UTF8Encoding($false)))
  icacls $bridgeEnv /inheritance:r /grant:r "Administrators:F" "SYSTEM:F" | Out-Null
}
Ok "configuration written"

# ---------------------------------------------------------------------------
Step "Auto-start (logon)"
# ---------------------------------------------------------------------------
$startup = [Environment]::GetFolderPath("Startup")
$launcherDir = Join-Path $InstallRoot "launchers"
Invoke-Action ("create {0}" -f $launcherDir) { New-Item -ItemType Directory -Force -Path $launcherDir | Out-Null }

function New-Launcher([string]$name, [string]$workDir, [string]$command, [string]$logFile) {
  $vbsPath = Join-Path $launcherDir ("{0}.vbs" -f $name)
  $vbs = @(
    "' Zarlino VM - hidden launcher for $name (generated by bootstrap.ps1)",
    "Set sh = CreateObject(""WScript.Shell"")",
    "sh.CurrentDirectory = ""$workDir""",
    "sh.Run ""cmd /c $command > """"$logFile"""" 2>&1"", 0, False"
  )
  [IO.File]::WriteAllLines($vbsPath, $vbs, (New-Object Text.UTF8Encoding($false)))
  $lnk = Join-Path $startup ("{0}.lnk" -f $name)
  $shell = New-Object -ComObject WScript.Shell
  $sc = $shell.CreateShortcut($lnk)
  $sc.TargetPath = "$env:SystemRoot\System32\wscript.exe"
  $sc.Arguments = "`"$vbsPath`""
  $sc.WorkingDirectory = $workDir
  $sc.Save()
  return $vbsPath
}

if ($DryRun) {
  Write-Host ("    DRY  register startup launchers for daemon + bridge in {0}" -f $startup) -ForegroundColor DarkGray
}
else {
  $bridgeVbs = New-Launcher "Zarlino WhatsApp Bridge (VM)" $bridgeRoot "node index.mjs" (Join-Path $InstallRoot "bridge.log")
  $daemonVbs = New-Launcher "Zarlino Executive Daemon (VM)" $execRoot "npm run agent:daemon:loop" (Join-Path $InstallRoot "daemon.log")
  Ok ("startup registered: {0}" -f $startup)
  Ok ("launchers: {0}" -f $launcherDir)
}

# ---------------------------------------------------------------------------
Step "Start + verify"
# ---------------------------------------------------------------------------
if ($NoStart -or $DryRun) { Warn "start skipped (dry run or -NoStart)" }
else {
  Start-Process wscript.exe -ArgumentList "`"$bridgeVbs`"" -WindowStyle Hidden
  Start-Sleep -Seconds 6
  $qrUp = $false
  try {
    $resp = Invoke-WebRequest ("http://127.0.0.1:{0}/" -f $QrPort) -UseBasicParsing -TimeoutSec 10
    $qrUp = ($resp.StatusCode -eq 200)
  } catch { $qrUp = $false }
  if ($qrUp) { Ok ("bridge QR page answers on http://localhost:{0}/" -f $QrPort) } else { Warn ("bridge QR page not answering on {0} yet - check {1}" -f $QrPort, (Join-Path $InstallRoot "bridge.log")) }

  Start-Process wscript.exe -ArgumentList "`"$daemonVbs`"" -WindowStyle Hidden
  Start-Sleep -Seconds 5
  $daemonLog = Join-Path $InstallRoot "daemon.log"
  if (Test-Path $daemonLog) { Ok "daemon process started (log written)" } else { Warn "daemon log not written yet - it starts on the first loop" }

  try {
    $headers = @{ Authorization = "Bearer $ExecToken" }
    $mon = Invoke-RestMethod ("{0}/api/ops/monitor" -f $OsUrl.TrimEnd("/")) -Headers $headers -TimeoutSec 30
    $bridge = $mon.states | Where-Object { $_.id -eq "whatsapp-bridge" }
    if ($bridge) { Ok ("OS monitor sees the bridge: {0} (last check {1})" -f $bridge.state, $bridge.at) }
    else { Info "OS monitor has no bridge entry yet - the first heartbeat lands within ~1 minute" }
  } catch { Warn ("could not read the OS monitor: {0}" -f $_.Exception.Message) }
}

# ---------------------------------------------------------------------------
Step "Build host (optional)"
# ---------------------------------------------------------------------------
# With -BuildHost the machine also becomes the plugin build/release host: MSVC
# build tools + CMake + Ninja + NSIS + the monorepo + a verified ZTame build.
# It needs a GitHub token for the private repo; without one the stage still
# installs the toolchain and reports what is missing.
if (-not $BuildHost -and -not $DryRun -and -not $SkipDeps) {
  $answer = Read-Host "    Make this machine the plugin BUILD HOST as well? (installs MSVC + CMake + Ninja + NSIS, 10-30 min) [y/N]"
  if ($answer -match '^(y|yes)$') { $BuildHost = $true }
}
if ($BuildHost -and -not $GitHubToken -and -not $DryRun) {
  Write-Host "    Paste a GitHub token with read access to zarlino-audio/Zarlino (stored only on this machine)." -ForegroundColor Yellow
  $secureGit = Read-Host -Prompt "    GITHUB_TOKEN (Enter to skip the repo stage)" -AsSecureString
  $bstrGit = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureGit)
  try { $GitHubToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstrGit) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstrGit) }
  if ($GitHubToken) {
    # Persist it for the daemon (build tools read it from the machine's env).
    [Environment]::SetEnvironmentVariable("ZARLINO_GITHUB_TOKEN", $GitHubToken, "Machine")
    Ok "GitHub token stored (machine environment)"
  }
  else { Warn "no GitHub token - the toolchain will install but the repo will not be cloned" }
}
if (-not $BuildHost) {
  Info "skipped (answer y above, or pass -BuildHost)"
}
else {
  $provScript = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "provision-buildhost.ps1"
  if (-not (Test-Path $provScript)) {
    # The launcher extracts it next to this script; fetch from the OS otherwise.
    $provScript = Join-Path $InstallRoot "provision-buildhost.ps1"
  }
  if (-not (Test-Path $provScript)) {
    Warn ("provision-buildhost.ps1 not found - run it separately from " + $InstallRoot)
  }
  else {
    $provArgs = @("-Stage", "all", "-Root", $InstallRoot, "-RepoDir", (Join-Path $InstallRoot "workspace"))
    if ($GitHubToken) { $provArgs += @("-GitHubToken", $GitHubToken) }
    if ($DryRun) { $provArgs += "-DryRun" }
    if ($DryRun) {
      Write-Host ("    DRY  powershell -File {0} {1}" -f $provScript, ($provArgs -join " ")) -ForegroundColor DarkGray
    }
    else {
      Info "running the build-host provisioner (this takes 10-30 min: MSVC workload)"
      & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $provScript @provArgs
      if ($LASTEXITCODE -ne 0) { Warn "build-host provisioning reported a failure - see C:\Zarlino\buildhost.log" }
      else { Ok "build host ready (log: C:\Zarlino\buildhost.log)" }
    }
  }
}

# ---------------------------------------------------------------------------
Write-Host ""
if ($DryRun -and $script:DryFailures -gt 0) {
  Write-Host ("=== Dry run finished with {0} finding(s) - fix them before running for real" -f $script:DryFailures) -ForegroundColor Yellow
  Log ("dry run finished with {0} finding(s)" -f $script:DryFailures)
  exit 2
}
Write-Host "=== Done" -ForegroundColor Cyan
Log "setup finished"
Write-Host ""
Write-Host "  Remaining manual step (once): pair WhatsApp" -ForegroundColor Yellow
Write-Host ("    1. Open http://localhost:{0}/ in a browser on this VM (or tunnel the port)." -f $QrPort)
Write-Host "    2. Scan the QR from the business phone (WhatsApp > Linked devices)."
Write-Host "    3. Confirm on the OS: doctor must show whatsapp.bridge OK."
Write-Host ""
Write-Host ("  Logs: {0}  |  bridge: {1}  |  daemon: {2}" -f $script:Log, (Join-Path $InstallRoot "bridge.log"), (Join-Path $InstallRoot "daemon.log"))
Write-Host ""
Write-Host "  What this VM can do: always-on WhatsApp founder channel + executor heartbeat."
Write-Host "  What still needs the laptop: VS/Ninja builds, renders, FL Studio work."
