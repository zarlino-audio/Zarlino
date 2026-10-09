# Detailed analysis of the ZScorch screenshot: histogram of colors + high-res ASCII map.
Add-Type -AssemblyName System.Drawing
$path = 'C:\Users\khrit\Desktop\plugin\artifacts\shots\zscorch-ui.png'
$bmp = New-Object System.Drawing.Bitmap($path)
$w = $bmp.Width; $h = $bmp.Height
Write-Host "Image: ${w}x${h}"

# 1) Color histogram by category
$cats = @{
  'white/bright' = 0; 'nearblack' = 0; 'scorch-orange' = 0; 'dim-orange' = 0;
  'light' = 0; 'mid' = 0; 'panel-dark' = 0; 'other' = 0
}
for ($y = 0; $y -lt $h; $y += 3) {
  for ($x = 0; $x -lt $w; $x += 3) {
    $p = $bmp.GetPixel($x,$y)
    $lum = 0.299*$p.R + 0.587*$p.G + 0.114*$p.B
    # Scorch orange accent ~ #ff6b35 / #ff8c42
    if ($p.R -gt 180 -and $p.G -gt 60 -and $p.G -lt 175 -and $p.B -lt 110) { $key='scorch-orange' }
    elseif ($p.R -gt 130 -and $p.R -lt 230 -and $p.G -gt 40 -and $p.G -lt 150 -and $p.B -lt 110) { $key='dim-orange' }
    elseif ($lum -gt 220) { $key='white/bright' }
    elseif ($lum -lt 30) { $key='nearblack' }
    elseif ($lum -gt 150) { $key='light' }
    elseif ($lum -gt 90) { $key='mid' }
    else { $key='panel-dark' }
    $cats[$key]++
  }
}
Write-Host ""
Write-Host "== COLOR PROFILE (sampled every 3px) =="
$total = ($cats.Values | Measure-Object -Sum).Sum
foreach ($k in $cats.Keys) { $pct = [math]::Round(100.0*$cats[$k]/$total,1); Write-Host ("{0,-16} {1,7}  {2,5}%" -f $k, $cats[$k], $pct) }

# 2) High-res ASCII map (100 cols x 44 rows)
Write-Host ""
$cols = 100; $rows = 44
for ($r = 0; $r -lt $rows; $r++) {
  $y = [int](($r + 0.5) * $h / $rows)
  $line = ""
  for ($c = 0; $c -lt $cols; $c++) {
    $x = [int](($c + 0.5) * $w / $cols)
    $p = $bmp.GetPixel($x,$y)
    $lum = 0.299*$p.R + 0.587*$p.G + 0.114*$p.B
    if ($p.R -gt 180 -and $p.G -gt 60 -and $p.G -lt 175 -and $p.B -lt 110) { $ch='O' }
    elseif ($p.R -gt 130 -and $p.G -gt 40 -and $p.G -lt 150 -and $p.B -lt 110) { $ch='o' }
    elseif ($lum -gt 230) { $ch='#' }
    elseif ($lum -gt 190) { $ch='@' }
    elseif ($lum -gt 130) { $ch='x' }
    elseif ($lum -gt 90) { $ch='+' }
    elseif ($lum -gt 55) { $ch='.' }
    elseif ($lum -gt 25) { $ch=':' }
    else { $ch=' ' }
    $line += $ch
  }
  Write-Host $line
}
$bmp.Dispose()
