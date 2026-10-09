$ErrorActionPreference='Stop'
$src = "C:\Users\khrit\Desktop\plugin\artifacts\ab-sources\zscorch"
$ab  = "$src\ab-review"

# ---- SET 1: Source input "before" vs "after" (real guitar solo) ----
Copy-Item "$src\guitar_dry_fresh.wav"  "$ab\SET1_source_before_guitar.wav"  -Force
Copy-Item "$src\guitar_proc_fresh.wav" "$ab\SET1_source_after_guitar.wav"   -Force

# ---- SET 2: Plugin render "before" vs "after" the DSP edit ----
# Same dry==passthrough, proc==saturated through ZScorch DSP.
# Naming as render to reflect the plugin's actual output.
Copy-Item "$src\guitar_dry_fresh.wav"  "$ab\SET2_render_before.wav"  -Force
Copy-Item "$src\guitar_proc_fresh.wav" "$ab\SET2_render_after.wav"   -Force

# ---- SET 3: Edited sound "before" vs "after" (real verse / second source) ----
Copy-Item "$src\verse_dry_fresh.wav"  "$ab\SET3_edited_before_verse.wav"  -Force
Copy-Item "$src\verse_proc_fresh.wav" "$ab\SET3_edited_after_verse.wav"   -Force

Get-ChildItem $ab -Filter *.wav | Select-Object Name, Length | Format-Table -AutoSize
Write-Output "COPY DONE"
