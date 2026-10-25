param(
    [Parameter(Mandatory = $true)][string]$ProcessName,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [int]$WaitSeconds = 3
)

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class WindowFinder {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr hWnd, StringBuilder name, int count);

    public static IntPtr FindLargest(uint wanted_pid, out string class_name) {
        IntPtr best = IntPtr.Zero;
        long best_area = -1;
        class_name = "";
        EnumWindows((handle, _) => {
            uint pid;
            GetWindowThreadProcessId(handle, out pid);
            if (pid != wanted_pid) return true;
            RECT rect;
            if (!GetWindowRect(handle, out rect)) return true;
            long area = (long)(rect.Right - rect.Left) * (rect.Bottom - rect.Top);
            if (area > best_area) {
                best_area = area;
                best = handle;
            }
            return true;
        }, IntPtr.Zero);
        if (best != IntPtr.Zero) {
            var name = new StringBuilder(256);
            GetClassName(best, name, name.Capacity);
            class_name = name.ToString();
        }
        return best;
    }
}
"@

Start-Sleep -Seconds $WaitSeconds
$process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { throw "process $ProcessName is not running" }

[void][WindowFinder]::SetProcessDPIAware()
$className = ""
$handle = [WindowFinder]::FindLargest([uint32]$process.Id, [ref]$className)
if ($handle -eq [IntPtr]::Zero) { throw "no top-level window for $ProcessName" }
Write-Output "window handle=$handle class=$className"

[void][WindowFinder]::SetWindowPos($handle, [IntPtr](-1), 40, 40, 1480, 940, 0x0040)
[void][WindowFinder]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds 1200

$rect = New-Object WindowFinder+RECT
[void][WindowFinder]::GetWindowRect($handle, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) { throw "window has no area" }

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
$graphics.Dispose()
$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output "captured ${width}x${height} -> $OutputPath"
