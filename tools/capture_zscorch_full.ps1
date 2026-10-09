# Capture ZScorch standalone with a long settle, saving the full client area.
$ErrorActionPreference = 'Stop'
$exe = 'C:\Users\khrit\Desktop\plugin\build\plugins\ZScorch\ZScorch_artefacts\Release\Standalone\ZScorch.exe'
$outDir = 'C:\Users\khrit\Desktop\plugin\artifacts\shots'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
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
$hWnd = [IntPtr]::Zero
for ($i=0; $i -lt 40; $i++){ Start-Sleep -Milliseconds 400; $proc.Refresh(); if($proc.MainWindowHandle -ne [IntPtr]::Zero){$hWnd=$proc.MainWindowHandle; if([Win32]::IsWindowVisible($hWnd)){break}} }
if($hWnd -eq [IntPtr]::Zero){ throw 'no window' }
[Win32]::SetForegroundWindow($hWnd) | Out-Null
Start-Sleep -Seconds 6
$cr=New-Object Win32+RECT; [Win32]::GetClientRect($hWnd,[ref]$cr)|Out-Null
$pt=New-Object Win32+POINT; [Win32]::ClientToScreen($hWnd,[ref]$pt)|Out-Null
$cw=$cr.Right; $ch=$cr.Bottom; $xs=$pt.X; $ys=$pt.Y
Write-Host "Client: ${cw}x${ch} at ($xs,$ys)"
Add-Type -AssemblyName System.Drawing
$full=New-Object System.Drawing.Bitmap($cw,$ch)
$g=[System.Drawing.Graphics]::FromImage($full)
$g.CopyFromScreen($xs,$ys,0,0,(New-Object System.Drawing.Size($cw,$ch)))
$g.Dispose()
$full.Save((Join-Path $outDir 'zscorch-ui-full.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$full.Dispose()
Write-Host "Saved full: zscorch-ui-full.png"
$proc.CloseMainWindow()|Out-Null; Start-Sleep -Milliseconds 700; if(-not $proc.HasExited){$proc.Kill()}
Write-Host "Done."
