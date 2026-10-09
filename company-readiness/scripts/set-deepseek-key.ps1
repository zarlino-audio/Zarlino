# One-time helper: put the NEW DeepSeek API key everywhere (value never leaves this machine).
# ---------------------------------------------------------------------------
# Why: the previous key belonged to a Google account we cannot recover. After you
# create the new key on platform.deepseek.com, RUN THIS SCRIPT and paste the key
# ONCE. It updates every local .dev.vars AND pushes the Worker secrets.
#
# Usage:  powershell -ExecutionPolicy Bypass -File set-deepseek-key.ps1
#
# The key is read as a secure string, never echoed, never written to logs.
$ErrorActionPreference = 'Continue'

$sec = Read-Host 'Paste the NEW DeepSeek API key (sk-...) and press Enter' -AsSecureString
$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
$key = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
if ([string]::IsNullOrWhiteSpace($key)) { Write-Host 'No key entered - aborted.'; exit 1 }
$key = $key.Trim()
if (-not $key.StartsWith('sk-')) { Write-Host 'That does not look like a DeepSeek key (sk-...). Aborted.'; exit 1 }

function Update-EnvFile([string]$Path, [string]$Name, [string]$Value) {
  if (-not (Test-Path $Path)) { Write-Host "SKIP  $Path (missing)"; return }
  $found = $false
  $out = foreach ($l in (Get-Content $Path)) {
    if ($l -match ("^\s*" + $Name + "\s*=")) { $found = $true; "$Name=$Value" } else { $l }
  }
  if (-not $found) { $out = @($out) + "$Name=$Value" }
  Set-Content -Path $Path -Value $out -Encoding UTF8
  Write-Host "OK    updated $Path"
}

$home_ = $env:USERPROFILE
Update-EnvFile (Join-Path $home_ 'Desktop\zarlino-executive-os\.dev.vars') 'DEEPSEEK_API_KEY' $key
Update-EnvFile (Join-Path $home_ 'Desktop\plugin\realestate-os\.dev.vars') 'DEEPSEEK_API_KEY' $key
Update-EnvFile (Join-Path $home_ 'Desktop\zarlino-songwriting-studio\.dev.vars') 'DEEPSEEK_API_KEY' $key

$workers = @(
  (Join-Path $home_ 'Desktop\zarlino-executive-os'),
  (Join-Path $home_ 'Desktop\plugin\realestate-os'),
  (Join-Path $home_ 'Desktop\zarlino-songwriting-studio')
)
foreach ($w in $workers) {
  if (-not (Test-Path $w)) { Write-Host "SKIP  worker in $w (missing)"; continue }
  Write-Host "--> pushing Worker secret in $w"
  Push-Location $w
  $key | npx wrangler secret put DEEPSEEK_API_KEY
  Pop-Location
}

Write-Host ''
Write-Host 'DONE.'
Write-Host 'Next: 1) run the doctor (npm run doctor in zarlino-executive-os) to confirm provider.balance goes OK.'
Write-Host '      2) for songwriting-studio, if its deploy scripts expect secrets from .dev.vars, run its tools\push-secrets.ps1 too.'
