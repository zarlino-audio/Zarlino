# Zarlino local backup - nightly source + secrets snapshot (Phase 0 continuity)
# ---------------------------------------------------------------------------
# What it does (per repo): a FULL git history bundle + a working-tree snapshot
# of all modified/untracked files (the exact class of work that has been lost
# before when a branch switch wiped the tree). Plus local secret files and the
# WhatsApp bridge session, so the machine dying does not end the company.
#
# Where: $env:USERPROFILE\Desktop\_zarlino_backups\<yyyy-MM-dd>\  (keeps 14 days)
#
# Restore:  git clone <name>-<date>.bundle  ->  full history
#           copy <name>-worktree\* over the clone -> uncommitted state back
#           secrets\ holds .dev.vars + bridge auth -> treat like passwords
#
# IMPORTANT: the backup folder contains credentials (secrets\). Keep the
# laptop copy PLUS an offsite copy (password-protected USB / encrypted cloud).
# ASCII-only script (Windows PowerShell 5.1).
$ErrorActionPreference = 'Continue'
$stamp = Get-Date -Format 'yyyy-MM-dd'
$root = Join-Path $env:USERPROFILE 'Desktop\_zarlino_backups'
$dest = Join-Path $root $stamp
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$repos = @(
  @{ name = 'zarlino-monorepo';     path = (Join-Path $env:USERPROFILE 'Desktop\plugin') },
  @{ name = 'zarlino-website';      path = (Join-Path $env:USERPROFILE 'Desktop\plugin\zarlino-website') },
  @{ name = 'zarlino-executive-os'; path = (Join-Path $env:USERPROFILE 'Desktop\zarlino-executive-os') },
  @{ name = 'songwriting-studio';   path = (Join-Path $env:USERPROFILE 'Desktop\zarlino-songwriting-studio') },
  @{ name = 'realestate-os';        path = (Join-Path $env:USERPROFILE 'Desktop\plugin\realestate-os') }
)

$log = @()
foreach ($r in $repos) {
  $gitDir = Join-Path $r.path '.git'
  if (-not (Test-Path $gitDir)) { $log += "SKIP  $($r.name): no git repo at $($r.path)"; continue }

  $bundle = Join-Path $dest ("{0}-{1}.bundle" -f $r.name, $stamp)
  git -C $r.path bundle create $bundle --all *> $null
  if ($LASTEXITCODE -eq 0) {
    $mb = [math]::Round((Get-Item $bundle).Length / 1MB, 1)
    $log += "OK    $($r.name): history bundle ($mb MB)"
  } else {
    $log += "FAIL  $($r.name): git bundle failed"
  }

  $wt = Join-Path $dest ($r.name + '-worktree')
  New-Item -ItemType Directory -Force -Path $wt | Out-Null
  $files = git -C $r.path ls-files -m -o --exclude-standard
  $copied = 0
  foreach ($f in $files) {
    if ([string]::IsNullOrWhiteSpace($f)) { continue }
    $src = Join-Path $r.path $f
    if (Test-Path $src -PathType Leaf) {
      $tgt = Join-Path $wt $f
      $tgtDir = Split-Path $tgt -Parent
      if (-not (Test-Path $tgtDir)) { New-Item -ItemType Directory -Force -Path $tgtDir | Out-Null }
      Copy-Item -Force $src $tgt
      $copied++
    }
  }
  $log += "OK    $($r.name): worktree snapshot ($copied modified/untracked files)"
}

$sec = Join-Path $dest 'secrets'
New-Item -ItemType Directory -Force -Path $sec | Out-Null
$secretFiles = @(
  (Join-Path $env:USERPROFILE 'Desktop\zarlino-executive-os\.dev.vars'),
  (Join-Path $env:USERPROFILE 'Desktop\plugin\realestate-os\.dev.vars')
)
foreach ($s in $secretFiles) {
  if (Test-Path $s) {
    $label = (Split-Path (Split-Path $s -Parent) -Leaf) + '-dev.vars'
    Copy-Item -Force $s (Join-Path $sec $label)
    $log += "OK    secrets: copied $label"
  }
}
$bridgeAuth = Join-Path $env:USERPROFILE 'Desktop\zarlino-executive-os\tools\whatsapp-bridge\auth'
if (Test-Path $bridgeAuth) {
  robocopy $bridgeAuth (Join-Path $sec 'whatsapp-bridge-auth') /E /NFL /NDL /NJH /NJS /NP *> $null
  $log += 'OK    secrets: whatsapp bridge auth copied'
}

$dirs = Get-ChildItem $root -Directory | Sort-Object Name
if ($dirs.Count -gt 14) {
  $dirs | Select-Object -First ($dirs.Count - 14) | ForEach-Object {
    Remove-Item -Recurse -Force $_.FullName
    $log += "CLEAN removed old backup $($_.Name)"
  }
}

$log | Set-Content (Join-Path $dest 'backup-log.txt')
$log | ForEach-Object { Write-Output $_ }
