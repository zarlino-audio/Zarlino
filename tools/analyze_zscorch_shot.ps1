# Analyze the captured ZScorch screenshot by sampling pixels and printing a coarse ASCII luminance map.
Add-Type -AssemblyName System.Drawing
$path = 'C:\Users\khrit\Desktop\plugin\artifacts\shots\zscorch-ui.png'
$bmp = New-Object System.Drawing.Bitmap($path)
$w = $bmp.Width; $h = $bmp.Height
Write-Host "Image: ${w}x${h}"

function Rgb([System.Drawing.Color]$c) { "{0,3},{1,3},{2,3}" -f $c.R,$c.G,$c.B }

# Sample a grid of pixels (reduce to ~60 columns x 30 rows) and print color triples
$cols = 60
$rows = 30
for ($r = 0; $r -lt $rows; $r++) {
    $y = [int](($r + 0.5) * $h / $rows)
    $line = ""
    for ($c = 0; $c -lt $cols; $c++) {
        $x = [int](($c + 0.5) * $w / $cols)
        $p = $bmp.GetPixel($x, $y)
        # Classify into a single char based on hue/brightness
        $lum = (0.299*$p.R + 0.587*$p.G + 0.114*$p.B)
        if ($lum -lt 25) { $ch = ' ' }          # near black
        elseif ($p.R -gt 200 -and $p.G -gt 100 -and $p.B -lt 100) { $ch = 'O' }  # orange
        elseif ($p.R -gt 150 -and $p.G -lt 120 -and $p.B -lt 120) { $ch = 'o' }  # dim orange
        elseif ($lum -gt 200) { $ch = '#' }      # bright / white text
        elseif ($lum -gt 120) { $ch = 'x' }      # mid light
        elseif ($lum -gt 60)  { $ch = '.' }      # dim
        else { $ch = ':' }                        # dark panel
        $line += $ch
    }
    Write-Host $line
}

# Sample a few diagnostic pixels
Write-Host ""
Write-Host "Top-left corner (should be dark bg): $(Rgb ($bmp.GetPixel(5,5)))"
Write-Host "Header center (ZScorch title area ~y=34): $(Rgb ($bmp.GetPixel(481,34)))"
Write-Host "Subtitle ~y=58: $(Rgb ($bmp.GetPixel(481,58)))"
Write-Host "Center of a knob area (~x=150,y=150): $(Rgb ($bmp.GetPixel(150,150)))"
$bmp.Dispose()
