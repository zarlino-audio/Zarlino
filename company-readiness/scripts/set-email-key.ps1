# One-time helper: put the EMAIL provider API key (Brevo) into the OS (value never leaves this machine).
# ---------------------------------------------------------------------------
# After you create the API key in the Brevo dashboard (SMTP & API > API Keys),
# COPY it to the clipboard (Ctrl+C) and run this script:
#     powershell -ExecutionPolicy Bypass -File set-email-key.ps1
# If the clipboard does not contain a key, it asks for a secure paste instead.
#
# It updates the local .dev.vars and pushes the Worker secret for
# zarlino-executive-os (the sender used by the outreach pipeline).
$ErrorActionPreference = 'Continue'

$key = ''
# 1) clipboard first (value never echoed)
try { $clip = (Get-Clipboard -Raw).Trim() } catch { $clip = '' }
if ($clip -match 'xkeysib-[A-Za-z0-9_\-]{20,}') { $key = $Matches[0] }
elseif ($clip.Length -ge 24 -and $clip -notmatch '\s') { $key = $clip }

# 2) fallback: secure prompt
if ([string]::IsNullOrWhiteSpace($key)) {
  $sec = Read-Host 'Paste the EMAIL provider API key (xkeysib-...) and press Enter' -AsSecureString
  $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
  $key = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
  [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
  $key = "$key".Trim()
}
if ([string]::IsNullOrWhiteSpace($key)) { Write-Host 'No key found - aborted.'; exit 1 }
if ($key -match '\s') { Write-Host 'Key contains whitespace - aborted.'; exit 1 }

$home_ = $env:USERPROFILE
$execOs = Join-Path $home_ 'Desktop\zarlino-executive-os'
$envFile = Join-Path $execOs '.dev.vars'
if (Test-Path $envFile) {
  $found = $false
  $out = foreach ($l in (Get-Content $envFile)) {
    if ($l -match '^\s*EMAIL_API_KEY\s*=') { $found = $true; "EMAIL_API_KEY=$key" } else { $l }
  }
  if (-not $found) { $out = @($out) + "EMAIL_API_KEY=$key" }
  [System.IO.File]::WriteAllLines((Resolve-Path $envFile).Path, $out, (New-Object System.Text.UTF8Encoding($false)))
  Write-Host "OK    updated $envFile"
} else { Write-Host "SKIP  $envFile (missing)" }

Write-Host "--> pushing Worker secret EMAIL_API_KEY (length $($key.Length))"
Push-Location $execOs
$key | npx wrangler secret put EMAIL_API_KEY
Pop-Location

Write-Host ''
Write-Host 'DONE.'
Write-Host 'Next: GET /api/outreach/report should show providerConfigured=true, then run a smoke send.'
