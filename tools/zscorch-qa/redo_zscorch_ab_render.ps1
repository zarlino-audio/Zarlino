$ErrorActionPreference = 'Stop'
$render = "C:\Users\khrit\Desktop\plugin\build\plugins\ZScorch\ZScorch_OfflineRender_artefacts\Release\ZScorch_OfflineRender.exe"
$root   = "C:\Program Files\Image-Line\FL Studio 2025\Data\Patches\Misc\Used by demo projects"
$out    = "C:\Users\khrit\Desktop\plugin\artifacts\ab-sources\zscorch\recheck"
New-Item -ItemType Directory -Force -Path $out | Out-Null
Write-Output "exe exists:  $(Test-Path $render)"
Write-Output "exe time:    $((Get-Item $render).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"

$guitar = "$root\Seamless - Menagerie\Guitar Solo .ogg"
$verse  = "$root\Astes - Bien Duro\1.1 Verse #2.ogg"

Write-Output "--- guitar recheck render (tape-warm) ---"
& $render $guitar "$out\guitar_dry.wav" "$out\guitar_proc.wav" tape-warm 10 9
Write-Output "guitar exit code: $LASTEXITCODE"

Write-Output "--- verse recheck render (studio-drive) ---"
& $render $verse "$out\verse_dry.wav" "$out\verse_proc.wav" studio-drive 5 8
Write-Output "verse exit code: $LASTEXITCODE"

Get-ChildItem $out -Filter *.wav | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
Write-Output "RECHECK RENDER DONE"
