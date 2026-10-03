# One-time helper: put the YouTube Data API key into the OS (value never leaves this machine).
# ---------------------------------------------------------------------------
# Create the key first: console.cloud.google.com -> new project (e.g. "zarlino-os")
# -> APIs & Services -> Library -> enable "YouTube Data API v3"
# -> Credentials -> Create credentials -> API key -> COPY it, then run this script:
#     powershell -ExecutionPolicy Bypass -File set-youtube-key.ps1
# Clipboard-first; falls back to a secure paste prompt.
$ErrorActionPreference = 'Continue'

$key = ''
try { $clip = (Get-Clipboard -Raw).Trim() } catch { $clip = '' }
# Google API keys look like AIza... (39 chars)
if ($clip -match 'AIza[0-9A-Za-z_\-]{30,}') { $key = $Matches[0] }
elseif ($clip.Length -ge 30 -and $clip -notmatch '\s') { $key = $clip }

if ([string]::IsNullOrWhiteSpace($key)) {
  $sec = Read-Host 'Paste the YouTube Data API key (AIza...) and press Enter' -AsSecureString
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
    if ($l -match '^\s*YOUTUBE_API_KEY\s*=') { $found = $true; "YOUTUBE_API_KEY=$key" } else { $l }
  }
  if (-not $found) { $out = @($out) + "YOUTUBE_API_KEY=$key" }
  [System.IO.File]::WriteAllLines((Resolve-Path $envFile).Path, $out, (New-Object System.Text.UTF8Encoding($false)))
  Write-Host "OK    updated $envFile"
} else { Write-Host "SKIP  $envFile (missing)" }

Write-Host "--> pushing Worker secret YOUTUBE_API_KEY (length $($key.Length))"
Push-Location $execOs
$key | npx wrangler secret put YOUTUBE_API_KEY
Pop-Location

Write-Host ''
Write-Host 'DONE.'
Write-Host 'Next: POST /api/tools/run {"name":"youtube.search_channels","input":{"query":"afrobeats producer"}} to verify.'
