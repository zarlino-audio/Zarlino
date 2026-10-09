$ErrorActionPreference = 'Stop'
$render = "C:\Users\khrit\Desktop\plugin\build\plugins\ZScorch\ZScorch_OfflineRender_artefacts\Release\ZScorch_OfflineRender.exe"
$root = "C:\Program Files\Image-Line\FL Studio 2025\Data\Patches\Misc\Used by demo projects"
$out = "C:\Users\khrit\Desktop\plugin\artifacts\ab-sources\zscorch"

# Real musical source files (verified to exist):
#  - Seamless - Menagerie\Guitar Solo .ogg   (real electric/patch guitar solo)
#  - Astes - Bien Duro\1.1 Verse #2.ogg      (real vocal/instrumental)
#  - Sid De La Cruz ... Strings.ogg          (real strings)
$guitar = "$root\Seamless - Menagerie\Guitar Solo .ogg"
$verse  = "$root\Astes - Bien Duro\1.1 Verse #2.ogg"

Write-Output "guitar exists: $(Test-Path $guitar)"
Write-Output "verse exists:  $(Test-Path $verse)"

# ---- Set 1: Guitar solo, ~9s excerpt, tape-warm ----
Write-Output "--- Rendering guitar (tape-warm) ---"
& $render $guitar "$out\guitar_dry_fresh.wav" "$out\guitar_proc_fresh.wav" tape-warm 10 9
Write-Output "guitar exit: $LASTEXITCODE"

# ---- Set 2: Vocal/instrumental verse, ~8s excerpt, studio-drive ----
Write-Output "--- Rendering verse (studio-drive) ---"
& $render $verse "$out\verse_dry_fresh.wav" "$out\verse_proc_fresh.wav" studio-drive 5 8
Write-Output "verse exit: $LASTEXITCODE"

Write-Output "DONE"
