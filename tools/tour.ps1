param(
    [Parameter(Mandatory = $true)][string]$ProcessName,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    # Steps as "name:action:x:y", action is click, hover or none. Coordinates
    # are relative to the window's top-left corner.
    [Parameter(Mandatory = $true)][string[]]$Steps
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Tour {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    public static IntPtr FindLargest(uint wanted) {
        IntPtr best = IntPtr.Zero; long area = -1;
        EnumWindows((h, _) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid != wanted) return true;
            RECT r; if (!GetWindowRect(h, out r)) return true;
            long a = (long)(r.Right - r.Left) * (r.Bottom - r.Top);
            if (a > area) { area = a; best = h; }
            return true;
        }, IntPtr.Zero);
        return best;
    }
}
"@

$process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { throw "process $ProcessName is not running" }
[void][Tour]::SetProcessDPIAware()
$handle = [Tour]::FindLargest([uint32]$process.Id)
if ($handle -eq [IntPtr]::Zero) { throw "no top-level window for $ProcessName" }
[void][Tour]::SetWindowPos($handle, [IntPtr](-1), 40, 40, 1480, 940, 0x0040)
[void][Tour]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds 900
$rect = New-Object Tour+RECT
[void][Tour]::GetWindowRect($handle, [ref]$rect)

$expanded = @($Steps | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
foreach ($step in $expanded) {
    $parts = $step.Split(':')
    $name = $parts[0]; $action = $parts[1]
    if ($action -ne 'none') {
        $x = $rect.Left + [int]$parts[2]; $y = $rect.Top + [int]$parts[3]
        [void][Tour]::SetCursorPos($x, $y)
        Start-Sleep -Milliseconds 150
        if ($action -eq 'click') {
            [Tour]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 60
            [Tour]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        }
    }
    Start-Sleep -Milliseconds 700
    $width = $rect.Right - $rect.Left; $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $graphics.Dispose()
    $path = Join-Path $OutputDirectory "$name.png"
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Output "captured $path"
}
