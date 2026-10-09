# Capture a screenshot of the ZScorch standalone plugin window.
$ErrorActionPreference = 'Stop'

$exe = 'C:\Users\khrit\Desktop\plugin\build\plugins\ZScorch\ZScorch_artefacts\Release\Standalone\ZScorch.exe'
$outDir  = 'C:\Users\khrit\Desktop\plugin\artifacts\shots'
$outFile = Join-Path $outDir 'zscorch-ui.png'

if (-not (Test-Path $exe)) { throw "ZScorch.exe not found: $exe" }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

[Win32]::SetProcessDPIAware() | Out-Null

# Launch the app
$proc = Start-Process -FilePath $exe -PassThru
Write-Host "Launched PID $($proc.Id). Waiting for window..."

# Wait for a visible main window
$hWnd = [IntPtr]::Zero
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 500
    $proc.Refresh()
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
        $hWnd = $proc.MainWindowHandle
        if ([Win32]::IsWindowVisible($hWnd)) { break }
    }
}
if ($hWnd -eq [IntPtr]::Zero) { throw "Could not find window" }
Write-Host "Window found (hwnd=$hWnd). Bringing to foreground..."

# Bring to front and give it a moment to paint
[Win32]::SetForegroundWindow($hWnd) | Out-Null
Start-Sleep -Milliseconds 1800

# Capture the window rect
$rect = New-Object Win32+RECT
[Win32]::GetWindowRect($hWnd, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
Write-Host "Window size: ${w}x${h}"

Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
$bmp.Save($outFile, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()
Write-Host "Saved: $outFile"

# Close the app
$proc.CloseMainWindow() | Out-Null
Start-Sleep -Milliseconds 800
if (-not $proc.HasExited) { $proc.Kill() }
Write-Host "Done."
