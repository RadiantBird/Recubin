# Example: .\capture_window.ps1 -Title 'Recubin' -ClassName GLFW30 -Output 'artifacts/editor.png'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [ValidateNotNullOrEmpty()] [string]$Title,
    [Parameter(Mandatory = $true)] [ValidateNotNullOrEmpty()] [string]$Output,
    [string]$ClassName,
    [switch]$NoActivate,
    [ValidateRange(1, 3600)] [int]$TimeoutSeconds = 15,
    [ValidateRange(0, 600000)] [int]$DelayMilliseconds = 500
)

$ErrorActionPreference = 'Stop'

try {
    Add-Type -AssemblyName System.Drawing
    Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public struct WindowRectangle { public int Left, Top, Right, Bottom; }
public static class WindowCaptureNative {
    public delegate bool EnumWindowsCallback(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr handle, StringBuilder text, int maxCount);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr handle, StringBuilder text, int maxCount);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out WindowRectangle rectangle);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
}
'@

    function Get-WindowMatches([string]$Pattern) {
        $matches = [System.Collections.Generic.List[object]]::new()
        $callback = [WindowCaptureNative+EnumWindowsCallback] {
            param([IntPtr]$Handle, [IntPtr]$Data)
            if (-not [WindowCaptureNative]::IsWindowVisible($Handle)) { return $true }
            $buffer = [Text.StringBuilder]::new(1024)
            $length = [WindowCaptureNative]::GetWindowText($Handle, $buffer, $buffer.Capacity)
            if ($length -le 0) { return $true }
            $windowTitle = $buffer.ToString()
            if ($windowTitle.IndexOf($Pattern, [StringComparison]::OrdinalIgnoreCase) -lt 0) { return $true }
            $classBuffer = [Text.StringBuilder]::new(256)
            [void][WindowCaptureNative]::GetClassName($Handle, $classBuffer, $classBuffer.Capacity)
            $windowClass = $classBuffer.ToString()
            if (-not [string]::IsNullOrEmpty($ClassName) -and
                -not $windowClass.Equals($ClassName, [StringComparison]::OrdinalIgnoreCase)) { return $true }
            $rectangle = [WindowRectangle]::new()
            if ([WindowCaptureNative]::GetWindowRect($Handle, [ref]$rectangle)) {
                $matches.Add([pscustomobject]@{ Handle = $Handle; Title = $windowTitle; ClassName = $windowClass; Rectangle = $rectangle })
            }
            return $true
        }
        [void][WindowCaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
        return $matches
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $matches = @()
    while ([DateTime]::UtcNow -lt $deadline) {
        $matches = @(Get-WindowMatches $Title)
        if ($matches.Count -gt 1) {
            $details = $matches | ForEach-Object {
                $r = $_.Rectangle
                "Title='$($_.Title)' ClassName='$($_.ClassName)' Handle=$($_.Handle) Rect=($($r.Left),$($r.Top),$($r.Right),$($r.Bottom))"
            }
            throw "Multiple visible top-level windows match title '$Title': $($details -join '; ')"
        }
        if ($matches.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($matches.Count -eq 0) { throw "No visible top-level window matching title '$Title' was found within $TimeoutSeconds second(s)." }

    $match = $matches[0]
    if ($NoActivate) {
        $foregroundActivated = 'skipped'
    } else {
        $foregroundActivated = [WindowCaptureNative]::SetForegroundWindow($match.Handle)
    }
    if ($DelayMilliseconds -gt 0) { Start-Sleep -Milliseconds $DelayMilliseconds }

    $left = [int]$match.Rectangle.Left; $top = [int]$match.Rectangle.Top
    $width = [int]$match.Rectangle.Right - $left; $height = [int]$match.Rectangle.Bottom - $top
    if ($width -le 0 -or $height -le 0) { throw "Window '$($match.Title)' has invalid capture size ${width}x${height}." }

    $absoluteOutput = [System.IO.Path]::GetFullPath($Output)
    $parent = [System.IO.Path]::GetDirectoryName($absoluteOutput)
    if ([string]::IsNullOrEmpty($parent)) { throw "Output path has no parent directory: '$Output'." }
    [System.IO.Directory]::CreateDirectory($parent) | Out-Null

    $bitmap = $null; $graphics = $null
    try {
        $bitmap = [System.Drawing.Bitmap]::new($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($left, $top, 0, 0, $bitmap.Size, [System.Drawing.CopyPixelOperation]::SourceCopy)
        $bitmap.Save($absoluteOutput, [System.Drawing.Imaging.ImageFormat]::Png)
    } catch { throw "PNG capture/save failed for '$absoluteOutput': $($_.Exception.Message)" }
    finally { if ($graphics) { $graphics.Dispose() }; if ($bitmap) { $bitmap.Dispose() } }

    Write-Output ("Captured '{0}' class='{1}' foreground={2} rectangle=({3},{4},{5},{6}) output='{7}'" -f $match.Title, $match.ClassName, $foregroundActivated, $left, $top, $width, $height, $absoluteOutput)
    exit 0
}
catch {
    [Console]::Error.WriteLine("capture_window.ps1: $($_.Exception.Message)")
    exit 1
}
