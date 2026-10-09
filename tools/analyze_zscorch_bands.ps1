# Analyze zscorch-ui-full.png in horizontal bands + save crops.
Add-Type -AssemblyName System.Drawing
$dir = 'C:\Users\khrit\Desktop\plugin\artifacts\shots'
$full = Join-Path $dir 'zscorch-ui-full.png'
$bmp = New-Object System.Drawing.Bitmap($full)
$w = $bmp.Width; $h = $bmp.Height
Write-Host "Image: ${w}x${h}"

function Stats($b, $name) {
  $orange=0;$dark=0;$light=0;$text=0;$tot=0
  for($y=0;$y -lt $b.Height;$y+=2){for($x=0;$x -lt $b.Width;$x+=2){
    $p=$b.GetPixel($x,$y);$tot++
    $lum=0.299*$p.R+0.587*$p.G+0.114*$p.B
    if($p.R -gt 180 -and $p.G -gt 60 -and $p.G -lt 175 -and $p.B -lt 110){$orange++}
    elseif($lum -gt 170){$light++}
    elseif($lum -lt 30){$dark++}
    elseif($lum -gt 55 -and $lum -lt 170){$text++}
  }}
  $O=[math]::Round(100*$orange/$tot,1); $L=[math]::Round(100*$light/$tot,1); $D=[math]::Round(100*$dark/$tot,1); $T=[math]::Round(100*$text/$tot,1)
  Write-Host ("{0,-14} orange={1,6}% light={2,6}% dark={3,6}% mid/text={4,6}%  (px orange={5})" -f $name,$O,$L,$D,$T,$orange)
}

$bands = 12
$bh = [int]($h / $bands)
for ($i=0; $i -lt $bands; $i++){
  $y0 = $i*$bh
  $b = New-Object System.Drawing.Bitmap($w,$bh)
  $g = [System.Drawing.Graphics]::FromImage($b)
  $src = New-Object System.Drawing.Rectangle(0,$y0,$w,$bh)
  $dst = New-Object System.Drawing.Rectangle(0,0,$w,$bh)
  $g.DrawImage($bmp,$dst,$src,[System.Drawing.GraphicsUnit]::Pixel)
  Stats $b ("band{0}(y{1})" -f $i,$y0)
  $g.Dispose();$b.Dispose()
}

# Also full-image stats
Write-Host ""
Stats $bmp "FULL IMAGE"
$bmp.Dispose()
