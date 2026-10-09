# Capture the ZScorch standalone UI with a longer settle time and client-area capture.
$ErrorActionPreference = 'Stop'

$exe = 'C:\Users\khrit\Desktop\plugin\build\plugins\ZScorch\ZScorch_artefacts\Release\Standalone\ZScorch.exe'
$outDir  = 'C:\Users\khrit\Desktop\plugin\artifacts\shots'
$outFull = Join-Path $outDir 'zscorch-ui.png'

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, out POINT pt);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X; public int Y; }
}
"@

[Win32]::SetProcessDPIAware() | Out-Null

$proc = Start-Process -FilePath $exe -PassThru
Write-Host "Launched PID $($proc.Id). Waiting..."
$hWnd = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 400
    $proc.Refresh()
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
        $hWnd = $proc.MainWindowHandle
        if ([Win32]::IsWindowVisible($hWnd)) { break }
    }
}
if ($hWnd -eq [IntPtr]::Zero) { throw "No window" }
[Win32]::SetForegroundWindow($hWnd) | Out-Null
# Let it settle and paint all controls
Start-Sleep -Seconds 4

# Client-rect based capture (excludes OS titlebar)
$cr = New-Object Win32+RECT
[Win32]::GetClientRect($hWnd, [ref]$cr) | Out-Null
$pt = New-Object Win32+POINT
[Win32]::ClientToScreen($hWnd, [ref]$pt) | Out-Null
$cw = $cr.Right; $ch = $cr.Bottom
$xs = $pt.X; $ys = $pt.Y
Write-Host "Client area: ${cw}x${ch} at ($xs,$ys)"

Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap($cw, $ch)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($xs, $ys, 0, 0, (New-Object System.Drawing.Size($cw, $ch)))
$bmp.Save($outFull, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()
Write-Host "Saved (client): $outFull"

$proc.CloseMainWindow() | Out-Null
Start-Sleep -Milliseconds 700
if (-not $proc.HasExited) { $proc.Kill() }
Write-Host "Done."
