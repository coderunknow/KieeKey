# KieeKey BS-24 field evidence collector (Phase 0/1, v1.3.0-beta8fix3)
#
# Usage: double-click run_probe_and_report.bat
#        or: powershell -NoProfile -ExecutionPolicy Bypass -File collect_report.ps1
#
# What it does (nothing is sent anywhere, everything stays in this folder):
#   1. collects system facts: OS build, scale (LogPixels), GPU/driver,
#      DWM/theme/contrast, EVERY running KieeKeyApp.exe (count, path,
#      file version, SHA-256), full tasklist, and which top-level
#      windows overlap the settings dialog's rectangle
#   2. runs kieekey_ui_probe.exe -> the REAL settings dialog, all checks
#      (real scale, real fonts, 9 tabs, real scrollbar, pixel audits)
#      -> ui-probe/ui_probe.json + one PNG per tab + ui_probe_trace.jsonl
#   3. zips everything into kieekey-report-<timestamp>.zip
#
# Requires: Windows 10/11 x64, PowerShell 5.1+ (built-in). No admin needed.
# No Vietnamese text on purpose: Windows PowerShell 5.1 reads .ps1 as the
# system code page, so the script and its output are pure ASCII.
# =============================================================================
$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Work = Join-Path $Root ("kieekey-report-" + $Stamp)
$ProbeOut = Join-Path $Work 'ui-probe'
New-Item -ItemType Directory -Force -Path $Work | Out-Null
New-Item -ItemType Directory -Force -Path $ProbeOut | Out-Null
$facts = Join-Path $Work 'system-facts.txt'

function Out-Section {
    param([string]$File, [string]$Name, [string]$Body)
    if ([string]::IsNullOrEmpty($Body)) { return }
    ("== " + $Name + " ==") | Out-File -FilePath $File -Append -Encoding ascii
    $Body | Out-File -FilePath $File -Append -Encoding ascii
    "" | Out-File -FilePath $File -Append -Encoding ascii
}

Write-Host "KieeKey BS-24 field evidence collector"
Write-Host "working folder : $Work"
Write-Host ""

# ---- identities of THIS package ---------------------------------------------
$probeExe = Join-Path $Root 'kieekey_ui_probe.exe'
if (-not (Test-Path $probeExe)) {
    Write-Host "ERROR: kieekey_ui_probe.exe must sit next to this script."
    exit 1
}
$probeSha = (Get-FileHash -Algorithm SHA256 $probeExe).Hash
$hdr = @(
    ("collected : " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')),
    ("machine   : " + $env:COMPUTERNAME),
    ("user      : " + $env:USERNAME),
    ("probe exe : $probeExe"),
    ("probe sha : $probeSha"),
    ""
)
$hdr | Out-File $facts -Encoding ascii

# ---- 1. OS / display ----------------------------------------------------------
Out-Section $facts "OS" ((Get-CimInstance Win32_OperatingSystem) |
    Select-Object Caption, Version, BuildNumber, OSArchitecture | Format-List | Out-String)
$nt = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction SilentlyContinue
Out-Section $facts "Windows NT" ($nt |
    Select-Object DisplayVersion, CurrentBuild, CurrentBuildNumber, ReleaseId, EditionID |
    Format-List | Out-String)
Out-Section $facts "GPU / video controller" ((Get-CimInstance Win32_VideoController) |
    Select-Object Name, DriverVersion, DriverDate, VideoModeDescription | Format-List | Out-String)
Out-Section $facts "Monitors" ((Get-CimInstance Win32_DesktopMonitor) |
    Select-Object ScreenWidth, ScreenHeight | Format-List | Out-String)
$desk = Get-ItemProperty 'HKCU:\Control Panel\Desktop' -Name LogPixels, LogPixels1, LogPixels2, FontSmoothing, FontSmoothingType, DragFullWindows -ErrorAction SilentlyContinue
Out-Section $facts "Scale + font smoothing (LogPixels 96/120/144 = 100/125/150%)" ($desk | Format-List | Out-String)
$theme = @(
    (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\DWM' -Name enable_aero -ErrorAction SilentlyContinue),
    (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' -Name SystemUsesLightTheme, AppsUseLightTheme -ErrorAction SilentlyContinue),
    (Get-ItemProperty 'HKCU:\Control Panel\Accessibility\HighContrast' -Name Flags -ErrorAction SilentlyContinue)
)
Out-Section $facts "DWM / theme / high contrast" ($theme | Format-List | Out-String)
$dwm = Get-Process dwm -ErrorAction SilentlyContinue
if ($dwm) { Out-Section $facts "dwm process" "running (pid $($dwm.Id))" }
else      { Out-Section $facts "dwm process" "NOT RUNNING (classic session?)" }

# ---- 2. how many KieeKeyApp.exe, which file, which version --------------------
$kk = @(Get-Process KieeKeyApp -ErrorAction SilentlyContinue)
if ($kk.Count -gt 0) {
    $lines = @()
    foreach ($p in $kk) {
        $sha = ''; $fver = ''
        if ($p.Path) {
            try { $sha = (Get-FileHash -Algorithm SHA256 $p.Path).Hash } catch {}
            try { $fver = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($p.Path).FileVersion } catch {}
        }
        $lines += ("pid=$($p.Id) path='$($p.Path)' title='$($p.MainWindowTitle)'")
        $lines += ("  file version = $fver")
        $lines += ("  sha256 = $sha")
    }
    Out-Section $facts "KieeKeyApp.exe processes (COUNT=$($kk.Count))" ($lines -join "`r`n")
} else {
    Out-Section $facts "KieeKeyApp.exe processes (COUNT=0)" "none running at collection time"
}
tasklist /FO CSV > (Join-Path $Work 'tasklist-full.csv')

# ---- 3. top-level windows: who overlaps the settings dialog? ------------------
# (optional: a locked-down machine may refuse Add-Type; the collector keeps
#  going without the window map - facts 1-2 and the probe still run)
$csharp = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class KkWin32 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public class Win { public uint Pid; public string Title; public string Class; public RECT R; }
    public static List<Win> All() {
        var list = new List<Win>();
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            var t = new StringBuilder(512); GetWindowTextW(h, t, t.Capacity);
            var c = new StringBuilder(256); GetClassNameW(h, c, c.Capacity);
            uint pid; GetWindowThreadProcessId(h, out pid);
            RECT r; GetWindowRect(h, out r);
            list.Add(new Win { Pid = pid, Title = t.ToString(), Class = c.ToString(), R = r });
            return true;
        }, IntPtr.Zero);
        return list;
    }
}
'@
$lines = @()
try {
    Add-Type -TypeDefinition $csharp -ErrorAction Stop
    $wins = [KkWin32]::All()
    $dlg = $null
    foreach ($w in $wins) {
        if ($w.Class -eq 'KieeKeySettings') { $dlg = $w; break }
    }
    if ($null -ne $dlg) {
        $lines += ("settings dialog on screen: pid=$($dlg.Pid) rect=$($dlg.R.Left),$($dlg.R.Top),$($dlg.R.Right - $dlg.R.Left)x$($dlg.R.Bottom - $dlg.R.Top)")
        $lines += ('title: ' + $dlg.Title)
        $lines += 'windows OVERLAPPING the settings dialog rect:'
        $over = 0
        foreach ($w in $wins) {
            if ($w -eq $dlg) { continue }
            $ix = [Math]::Min($w.R.Right, $dlg.R.Right) - [Math]::Max($w.R.Left, $dlg.R.Left)
            $iy = [Math]::Min($w.R.Bottom, $dlg.R.Bottom) - [Math]::Max($w.R.Top, $dlg.R.Top)
            if ($ix -gt 0 -and $iy -gt 0) {
                $over++
                $lines += ("  pid=$($w.Pid) class=$($w.Class) rect=$($w.R.Left),$($w.R.Top),$($w.R.Right - $w.R.Left)x$($w.R.Bottom - $w.R.Top) title='$($w.Title)'")
            }
        }
        if ($over -eq 0) { $lines += '  (none)' }
    } else {
        $lines += 'no KieeKeySettings window on screen at collection time'
        $lines += 'all visible top-level windows (class | title):'
        foreach ($w in $wins) {
            $lines += ("  pid=$($w.Pid) class=$($w.Class) title='$($w.Title)'")
        }
    }
} catch {
    $lines += "window map skipped (Add-Type refused on this machine: " + $_.Exception.Message + ")"
}
Out-Section $facts "Top-level window map (H2: overlays)" ($lines -join "`r`n")

# ---- 4. run the probe ----------------------------------------------------------
Write-Host "Running kieekey_ui_probe.exe (2-5 minutes; a dialog opens and closes on its own)."
Write-Host "Keep the desktop unobstructed while it runs."
Write-Host ""
& $probeExe $ProbeOut 60 *> (Join-Path $ProbeOut 'console.log')
$code = $LASTEXITCODE
("probe exit code = $code" | Out-File (Join-Path $ProbeOut 'console.log') -Append -Encoding ascii)
if ($code -ne 0) {
    Write-Host "PROBE EXIT CODE = $code (non-zero: the probe judged something; the report is still useful - zip it anyway)"
}

# ---- 5. zip it ------------------------------------------------------------------
$zip = Join-Path $Root ("kieekey-report-" + $Stamp + ".zip")
Compress-Archive -Path (Join-Path $Work '*') -DestinationPath $zip -Force
Write-Host ""
Write-Host "DONE. Send back this file:"
Write-Host "   $zip"
Write-Host ""
pause
