# One-time helper: rotate CF_API_TOKEN (value never leaves this machine).
# ---------------------------------------------------------------------------
# After you create the new scoped token in the Cloudflare dashboard
# (My Profile -> API Tokens -> Create Token: Workers Scripts:Edit, Workers KV:Edit,
#  R2:Edit, Account Settings:Read, Zone zarlinoaudio.com:Read+DNS:Edit),
# RUN THIS SCRIPT and paste it ONCE. It updates the local .dev.vars and pushes
# the Worker secret for zarlino-executive-os.
#
# Usage:  powershell -ExecutionPolicy Bypass -File set-cf-token.ps1
$ErrorActionPreference = 'Continue'

$sec = Read-Host 'Paste the NEW Cloudflare API token and press Enter' -AsSecureString
$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
$tok = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
if ([string]::IsNullOrWhiteSpace($tok)) { Write-Host 'No token entered - aborted.'; exit 1 }
$tok = $tok.Trim()

$execOs = Join-Path $env:USERPROFILE 'Desktop\zarlino-executive-os'
$envFile = Join-Path $execOs '.dev.vars'
if (Test-Path $envFile) {
  $found = $false
  $out = foreach ($l in (Get-Content $envFile)) {
    if ($l -match '^\s*CF_API_TOKEN\s*=') { $found = $true; "CF_API_TOKEN=$tok" } else { $l }
  }
  if (-not $found) { $out = @($out) + "CF_API_TOKEN=$tok" }
  Set-Content -Path $envFile -Value $out -Encoding UTF8
  Write-Host "OK    updated $envFile"
} else {
  Write-Host "SKIP  $envFile (missing)"
}

Push-Location $execOs
$tok | npx wrangler secret put CF_API_TOKEN
Pop-Location

Write-Host ''
Write-Host 'DONE. Verify: npm run doctor (in zarlino-executive-os) and a Cloudflare tool call from the OS.'
