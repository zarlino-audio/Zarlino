# Zarlino Audio - canonical screenshot validator (ASCII only on purpose;
# Windows PowerShell 5.1 misparses non-ASCII without a UTF-8 BOM).
#
# Validates that a rendered plugin screenshot is REAL evidence before it can be
# attached to a release manifest (§9): it exists, is a valid PNG of sensible
# dimensions, has actual content (not blank), and is not (mostly) black.
#
# USAGE
#   powershell -File tools/release/validate-screenshot.ps1 -Path <file.png> `
#       [-MinWidth 640] [-MinHeight 480] [-MinBytes 10240]
# EXIT 0 valid - 1 invalid (each failing rule is printed)

param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$MinWidth = 640,
    [int]$MinHeight = 480,
    [int]$MinBytes = 10240
)

$ErrorActionPreference = "Stop"
$failures = New-Object System.Collections.Generic.List[string]

function Fail([string]$msg) { $script:failures.Add($msg) | Out-Null }

# 1. existence + size floor
if (-not (Test-Path -LiteralPath $Path)) {
    Write-Host "validate-screenshot: FAIL file does not exist: $Path"
    exit 1
}
$file = Get-Item -LiteralPath $Path
if ($file.Length -lt $MinBytes) {
    Fail ("file too small: {0} bytes (minimum {1}) - possibly a truncated or empty render" -f $file.Length, $MinBytes)
}

# 2. PNG signature
$bytes = [System.IO.File]::ReadAllBytes($file.FullName)
$pngMagic = @(137, 80, 78, 71, 13, 10, 26, 10)
$magicOk = $true
if ($bytes.Length -lt 8) { $magicOk = $false }
else {
    for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $pngMagic[$i]) { $magicOk = $false; break } }
}
if (-not $magicOk) { Fail "not a PNG file (signature mismatch)" }

# 3. decodes + dimensions + content sampling via System.Drawing
if ($magicOk) {
    Add-Type -AssemblyName System.Drawing
    $img = $null
    try {
        $img = [System.Drawing.Image]::FromFile($file.FullName)
    } catch {
        Fail ("could not decode image: {0}" -f $_.Exception.Message)
    }
    if ($null -ne $img) {
        try {
            if ($img.Width -lt $MinWidth -or $img.Height -lt $MinHeight) {
                Fail ("dimensions too small: {0}x{1} (minimum {2}x{3})" -f $img.Width, $img.Height, $MinWidth, $MinHeight)
            }
            if ($img.Width -gt 4096 -or $img.Height -gt 4096) {
                Fail ("dimensions implausible: {0}x{1}" -f $img.Width, $img.Height)
            }

            # sample up to a 48x32 grid of pixels; compute luminance statistics
            $bmp = New-Object System.Drawing.Bitmap($img)
            $cols = 48; $rows = 32
            $lum = New-Object System.Collections.Generic.List[double]
            for ($r = 0; $r -lt $rows; $r++) {
                $y = [Math]::Min($bmp.Height - 1, [int](($r + 0.5) * $bmp.Height / $rows))
                for ($c = 0; $c -lt $cols; $c++) {
                    $x = [Math]::Min($bmp.Width - 1, [int](($c + 0.5) * $bmp.Width / $cols))
                    $px = $bmp.GetPixel($x, $y)
                    $l = 0.2126 * $px.R + 0.7152 * $px.G + 0.0722 * $px.B
                    $lum.Add($l) | Out-Null
                }
            }
            $bmp.Dispose()

            $min = ($lum | Measure-Object -Minimum).Minimum
            $max = ($lum | Measure-Object -Maximum).Maximum
            $avg = ($lum | Measure-Object -Average).Average
            $spread = $max - $min

            # not blank: the sampled pixels must show real structure
            if ($spread -lt 40) {
                Fail ("image looks BLANK: luminance spread {0:N1} over sampled grid (minimum 40)" -f $spread)
            }
            # not mostly black: a dark-but-structured UI is fine; an almost black frame is not
            if ($avg -lt 15) {
                Fail ("image is MOSTLY BLACK: average luminance {0:N1} (minimum 15)" -f $avg)
            }
            Write-Host ("validate-screenshot: sampled {0}x{1}, dims {2}x{3}, lum min/avg/max = {4:N1}/{5:N1}/{6:N1}" -f $cols, $rows, $img.Width, $img.Height, $min, $avg, $max)
        } finally {
            $img.Dispose()
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "validate-screenshot: FAIL"
    foreach ($f in $failures) { Write-Host ("  - {0}" -f $f) }
    exit 1
}

Write-Host ("validate-screenshot: VALID ({0}, {1} bytes)" -f $file.Name, $file.Length)
exit 0
