# Fine ASCII map of the FULL zscorch-ui-full.png at high resolution.
Add-Type -AssemblyName System.Drawing
$path = 'C:\Users\khrit\Desktop\plugin\artifacts\shots\zscorch-ui-full.png'
$bmp = New-Object System.Drawing.Bitmap($path)
$w=$bmp.Width; $h=$bmp.Height
Write-Host "Image ${w}x${h}"
$cols=128; $rows=56
for($r=0;$r -lt $rows;$r++){
  $y=[int](($r+0.5)*$h/$rows)
  $line=""
  for($c=0;$c -lt $cols;$c++){
    $x=[int](($c+0.5)*$w/$cols)
    $p=$bmp.GetPixel($x,$y)
    $lum=0.299*$p.R+0.587*$p.G+0.114*$p.B
    if($p.R -gt 180 -and $p.G -gt 60 -and $p.G -lt 175 -and $p.B -lt 110){$ch='O'}
    elseif($p.R -gt 130 -and $p.G -gt 40 -and $p.G -lt 150 -and $p.B -lt 110){$ch='o'}
    elseif($lum -gt 230){$ch='#'}
    elseif($lum -gt 180){$ch='@'}
    elseif($lum -gt 120){$ch='x'}
    elseif($lum -gt 80){$ch='+'}
    elseif($lum -gt 50){$ch='.'}
    elseif($lum -gt 25){$ch=':'}
    else{$ch=' '}
    $line+=$ch
  }
  Write-Host $line
}
$bmp.Dispose()
