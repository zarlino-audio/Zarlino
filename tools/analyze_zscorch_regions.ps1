# High-res region study of the ZScorch screenshot: header + upper-left content + right block.
Add-Type -AssemblyName System.Drawing
$path = 'C:\Users\khrit\Desktop\plugin\artifacts\shots\zscorch-ui-full.png'
$bmp = New-Object System.Drawing.Bitmap($path)
$w=$bmp.Width; $h=$bmp.Height

function MapRegion($x0,$y0,$x1,$y1,$cols,$rows,$label){
  Write-Host ""
  Write-Host "==== $label ($x0,$y0)-($x1,$y1) ===="
  $rw=$x1-$x0; $rh=$y1-$y0
  for($r=0;$r -lt $rows;$r++){
    $y=[int]($y0+($r+0.5)*$rh/$rows)
    $line=""
    for($c=0;$c -lt $cols;$c++){
      $x=[int]($x0+($c+0.5)*$rw/$cols)
      if($x -ge $w -or $y -ge $h){$line+=' ';continue}
      $p=$bmp.GetPixel($x,$y)
      $lum=0.299*$p.R+0.587*$p.G+0.114*$p.B
      if($p.R -gt 180 -and $p.G -gt 60 -and $p.G -lt 175 -and $p.B -lt 110){$ch='O'}
      elseif($lum -gt 230){$ch='#'}
      elseif($lum -gt 180){$ch='@'}
      elseif($lum -gt 125){$ch='x'}
      elseif($lum -gt 80){$ch='+'}
      elseif($lum -gt 50){$ch='.'}
      elseif($lum -gt 25){$ch=':'}
      else{$ch=' '}
      $line+=$ch
    }
    Write-Host $line
  }
}

# Header band (full width, top ~78px)
MapRegion 0 0 962 78 128 6 "HEADER"
# Upper-left content block (the dense cluster)
MapRegion 200 160 560 360 96 24 "UPPER-LEFT BLOCK"
# Right content strip
MapRegion 760 500 940 640 60 20 "RIGHT-BOTTOM STRIP"
$bmp.Dispose()
