# Example: .\click_window.ps1 -Title 'Recubin' -ClassName GLFW30 -X 640 -Y 360
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [ValidateNotNullOrEmpty()] [string]$Title,
    [string]$ClassName,
    [switch]$NoActivate,
    [Parameter(Mandatory = $true)] [ValidateRange(0, [int]::MaxValue)] [int]$X,
    [Parameter(Mandatory = $true)] [ValidateRange(0, [int]::MaxValue)] [int]$Y,
    [ValidateRange(1, 3600)] [int]$TimeoutSeconds = 15,
    [ValidateRange(0, 600000)] [int]$DelayMilliseconds = 100,
    [ValidateRange(1, 1000)] [int]$HoldMilliseconds = 50
)

$ErrorActionPreference = 'Stop'

try {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public struct ClickWindowRectangle { public int Left, Top, Right, Bottom; }
public static class ClickWindowNative {
    public delegate bool EnumWindowsCallback(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr handle, StringBuilder text, int maxCount);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr handle, StringBuilder text, int maxCount);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out ClickWindowRectangle rectangle);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
}
'@

    function Get-WindowMatches([string]$Pattern, [string]$WantedClass) {
        $matches = [System.Collections.Generic.List[object]]::new()
        $callback = [ClickWindowNative+EnumWindowsCallback] {
            param([IntPtr]$Handle, [IntPtr]$Data)
            if (-not [ClickWindowNative]::IsWindowVisible($Handle)) { return $true }
            $titleBuffer = [Text.StringBuilder]::new(1024)
            if ([ClickWindowNative]::GetWindowText($Handle, $titleBuffer, $titleBuffer.Capacity) -le 0) { return $true }
            $windowTitle = $titleBuffer.ToString()
            if ($windowTitle.IndexOf($Pattern, [StringComparison]::OrdinalIgnoreCase) -lt 0) { return $true }
            $classBuffer = [Text.StringBuilder]::new(256)
            [void][ClickWindowNative]::GetClassName($Handle, $classBuffer, $classBuffer.Capacity)
            $windowClass = $classBuffer.ToString()
            if (-not [string]::IsNullOrEmpty($WantedClass) -and
                -not $windowClass.Equals($WantedClass, [StringComparison]::OrdinalIgnoreCase)) { return $true }
            $rectangle = [ClickWindowRectangle]::new()
            if ([ClickWindowNative]::GetWindowRect($Handle, [ref]$rectangle)) {
                $matches.Add([pscustomobject]@{ Handle = $Handle; Title = $windowTitle; ClassName = $windowClass; Rectangle = $rectangle })
            }
            return $true
        }
        [void][ClickWindowNative]::EnumWindows($callback, [IntPtr]::Zero)
        return $matches
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $matches = @()
    while ([DateTime]::UtcNow -lt $deadline) {
        $matches = @(Get-WindowMatches $Title $ClassName)
        if ($matches.Count -gt 1) { throw "Multiple visible top-level windows match title '$Title'. Specify -ClassName for an exact filter." }
        if ($matches.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($matches.Count -eq 0) { throw "No visible top-level window matching title '$Title' was found within $TimeoutSeconds second(s)." }

    $match = $matches[0]
    $rectangle = $match.Rectangle
    $width = [int]$rectangle.Right - [int]$rectangle.Left
    $height = [int]$rectangle.Bottom - [int]$rectangle.Top
    if ($width -le 0 -or $height -le 0) { throw "Window '$($match.Title)' has invalid rectangle." }
    if ($X -ge $width -or $Y -ge $height) { throw "Relative coordinate ($X,$Y) is outside window rectangle ${width}x${height}." }
    if ($NoActivate) {
        $foregroundActivated = 'skipped'
    } else {
        $foregroundActivated = [ClickWindowNative]::SetForegroundWindow($match.Handle)
    }
    if ($DelayMilliseconds -gt 0) { Start-Sleep -Milliseconds $DelayMilliseconds }

    $screenX = [int]$rectangle.Left + $X
    $screenY = [int]$rectangle.Top + $Y
    if (-not [ClickWindowNative]::SetCursorPos($screenX, $screenY)) { throw "SetCursorPos failed for ($screenX,$screenY)." }
    $mouseLeftDown = 0x0002; $mouseLeftUp = 0x0004
    [ClickWindowNative]::mouse_event($mouseLeftDown, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMilliseconds
    [ClickWindowNative]::mouse_event($mouseLeftUp, 0, 0, 0, [UIntPtr]::Zero)
    Write-Output ("Clicked '{0}' class='{1}' foreground={2} holdMs={3} relative=({4},{5}) screen=({6},{7})" -f $match.Title, $match.ClassName, $foregroundActivated, $HoldMilliseconds, $X, $Y, $screenX, $screenY)
    exit 0
}
catch {
    [Console]::Error.WriteLine("click_window.ps1: $($_.Exception.Message)")
    exit 1
}
