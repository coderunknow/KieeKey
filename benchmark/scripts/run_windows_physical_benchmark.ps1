#requires -Version 5.1
<#
.SYNOPSIS
  Run the KieeKey-vs-UniKey benchmark on the user's physical Windows host.

.DESCRIPTION
  This script exists because Arena/CI/VM results must not be used as the final
  answer to "which engine is faster on a normal user's Windows PC?".  It records
  host state, refuses obvious virtual machines by default, pins the benchmark
  process when requested, randomizes independent per-process engine order, keeps
  warm-up rows separate from measured rows (the harness marks warm=1), and writes
  raw artifacts plus a PASS/FAIL report.

  The benchmark binary is still the repository's existing apples-to-apples
  harness: same corpus, same generated input distribution, same warm-up policy,
  same engine adapters, same frozen KieeKey v1.2.2 attribution column, and the
  same vendored UniKey UKEngine source.

.REQUIREMENTS
  * Run from a physical Windows PC, not a VM.
  * MSYS2/Git-Bash-style POSIX shell with g++ available to build/run the existing
    benchmark scripts. Prefer MSYS2 UCRT64 or MINGW64 so all engines are built by
    one compiler with one flag set.
  * PowerShell is used only as the Windows host controller/probe; the benchmark
    timings come from the harness artifacts.
#>
[CmdletBinding()]
param(
    [string]$Campaign = ("winphys-" + (Get-Date -Format "yyyyMMdd-HHmmss")),
    [int]$Sessions = 6,
    [int]$Rounds = 20,
    [int]$Keys = 200000,
    [int]$Words = 74000,
    [int]$L2Rounds = 10,
    [int]$DiffabSeeds = 3,
    [int]$DiffabKeys = 60000,
    [int]$DiffabWords = 4000,
    [int]$ProcessSamples = 5,
    [int]$PinCore = -1,
    [ValidateSet("Normal", "AboveNormal", "High", "RealTime")]
    [string]$Priority = "High",
    [string]$BashPath = "",
    [switch]$SkipSanitizers,
    [switch]$AllowVirtualized,
    [switch]$NoFailOnNoise
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Repo-Root {
    $p = Split-Path -Parent $MyInvocation.ScriptName
    return (Resolve-Path (Join-Path $p "../..")).Path
}

function Convert-ToPosixPath([string]$Path) {
    $full = (Resolve-Path $Path).Path
    if ($full -match '^([A-Za-z]):\\(.*)$') {
        $drive = $matches[1].ToLowerInvariant()
        $rest = $matches[2] -replace '\\', '/'
        return "/$drive/$rest"
    }
    return ($full -replace '\\', '/')
}

function Find-Bash([string]$Requested) {
    if ($Requested -and (Test-Path $Requested)) { return (Resolve-Path $Requested).Path }
    $candidates = @(
        "$env:MSYSTEM_PREFIX\bin\bash.exe",
        "$env:ProgramFiles\Git\bin\bash.exe",
        "$env:ProgramFiles\Git\usr\bin\bash.exe",
        "C:\msys64\usr\bin\bash.exe",
        "C:\msys64\ucrt64\bin\bash.exe",
        "C:\msys64\mingw64\bin\bash.exe"
    ) | Where-Object { $_ -and (Test-Path $_) }
    if ($candidates.Count -gt 0) { return $candidates[0] }
    $cmd = Get-Command bash.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "bash.exe not found. Install MSYS2/Git Bash or pass -BashPath."
}

function New-AffinityMask([int]$Core) {
    if ($Core -lt 0) { return [IntPtr]::Zero }
    if ($Core -ge [IntPtr]::Size * 8) { throw "PinCore $Core is outside this process mask width" }
    return [IntPtr]([int64]1 -shl $Core)
}

function Invoke-ControlledProcess {
    param(
        [string]$FilePath,
        [string]$Arguments,
        [string]$StdoutPath,
        [string]$StderrPath,
        [IntPtr]$AffinityMask,
        [string]$PriorityClass
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    $psi.Arguments = $Arguments
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::new()
    $p.StartInfo = $psi
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    [void]$p.Start()
    if ($AffinityMask -ne [IntPtr]::Zero) {
        try { $p.ProcessorAffinity = $AffinityMask } catch { Write-Warning "Could not set affinity: $_" }
    }
    try { $p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::$PriorityClass } catch { Write-Warning "Could not set priority: $_" }
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    $p.WaitForExit()
    $sw.Stop()
    $outTask.Wait(); $errTask.Wait()
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $StdoutPath) | Out-Null
    [IO.File]::WriteAllText($StdoutPath, $outTask.Result)
    [IO.File]::WriteAllText($StderrPath, $errTask.Result)
    return [pscustomobject]@{
        ExitCode = $p.ExitCode
        WallMs = [math]::Round($sw.Elapsed.TotalMilliseconds, 3)
        CpuMs = [math]::Round($p.TotalProcessorTime.TotalMilliseconds, 3)
        PeakWorkingSetMiB = [math]::Round($p.PeakWorkingSet64 / 1MB, 3)
        PrivateMemoryMiB = [math]::Round($p.PrivateMemorySize64 / 1MB, 3)
    }
}

function Get-HostProbe([string]$PriorityClass, [string]$AffinityText) {
    $cs = Get-CimInstance Win32_ComputerSystem
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    $os = Get-CimInstance Win32_OperatingSystem
    $probe = [ordered]@{
        computer = $env:COMPUTERNAME
        timestamp_local = (Get-Date).ToString("o")
        timestamp_utc = (Get-Date).ToUniversalTime().ToString("o")
        manufacturer = $cs.Manufacturer
        model = $cs.Model
        hypervisor_present = [bool]$cs.HypervisorPresent
        cpu = $cpu.Name
        physical_cores = $cpu.NumberOfCores
        logical_processors = $cpu.NumberOfLogicalProcessors
        os = $os.Caption + " " + $os.Version
        process_priority = $PriorityClass
        affinity_mask = $AffinityText
        total_memory_gib = [math]::Round($cs.TotalPhysicalMemory / 1GB, 2)
    }

    $vmText = (($cs.Manufacturer + " " + $cs.Model) -join " ")
    if ($vmText -match 'Virtual|VMware|VirtualBox|KVM|QEMU|Hyper-V|Bochs|Xen|Parallels') {
        $probe.virtualization_warning = "Host model/manufacturer looks virtualized: $vmText"
    } elseif ($cs.HypervisorPresent) {
        $probe.virtualization_warning = "Windows reports HypervisorPresent=True. This can be VBS/Hyper-V on physical hardware; record it and rerun with VBS off if the result is close."
    }

    try {
        $pp = (powercfg /getactivescheme) -join " "
        $probe.power_plan = $pp.Trim()
        if ($pp -notmatch 'High performance|Ultimate Performance') {
            $probe.power_warning = "Active power plan is not High/Ultimate Performance. Rerun after fixing if margins are close."
        }
    } catch { $probe.power_plan = "unavailable: $_" }

    try {
        $mp = Get-MpComputerStatus -ErrorAction Stop
        $probe.antivirus = "Defender realtime=$($mp.RealTimeProtectionEnabled) behavior=$($mp.BehaviorMonitorEnabled) ioav=$($mp.IoavProtectionEnabled)"
        if ($mp.RealTimeProtectionEnabled) {
            $probe.defender_warning = "Microsoft Defender real-time scanning is enabled; add the checkout/build directory exclusion or rerun if noise is high."
        }
    } catch { $probe.antivirus = "Defender status unavailable: $_" }

    try {
        $q = (Get-Counter '\System\Processor Queue Length').CounterSamples.CookedValue
        $cpuPct = (Get-Counter '\Processor(_Total)\% Processor Time').CounterSamples.CookedValue
        $probe.background_cpu_before = "processor_queue=$([math]::Round($q,2)); total_cpu_pct=$([math]::Round($cpuPct,2))"
        if ($q -gt 2 -or $cpuPct -gt 20) {
            $probe.background_load_warning = "Background CPU/queue load was material before the run. Close background tasks and rerun."
        }
    } catch { $probe.background_cpu_before = "unavailable: $_" }

    try {
        $temps = Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature -ErrorAction Stop |
            ForEach-Object { [math]::Round(($_.CurrentTemperature / 10.0) - 273.15, 1) }
        if ($temps) {
            $probe.thermal_status = ($temps -join ",") + " C (ACPI zones)"
            if (($temps | Measure-Object -Maximum).Maximum -gt 90) {
                $probe.thermal_warning = "ACPI thermal zone exceeded 90 C; throttling may affect results."
            }
        } else { $probe.thermal_status = "no ACPI thermal zones exposed" }
    } catch { $probe.thermal_status = "unavailable: $_" }

    return $probe
}

$root = Repo-Root
$bash = Find-Bash $BashPath
$posixRoot = Convert-ToPosixPath $root
$res = Join-Path $root "benchmark/results/$Campaign"
$raw = Join-Path $res "raw"
$logs = Join-Path $res "logs"
New-Item -ItemType Directory -Force -Path $raw, $logs | Out-Null

$affinity = New-AffinityMask $PinCore
$affText = if ($PinCore -ge 0) { "core=$PinCore mask=$affinity" } else { "not pinned by PowerShell" }
$probe = Get-HostProbe -PriorityClass $Priority -AffinityText $affText
$probe.bash = $bash
$probe.repository = $root
$probe.campaign = $Campaign
$probe | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $res "windows_host_probe.json")

if ($probe.virtualization_warning -and -not $AllowVirtualized) {
    throw "Refusing to run final physical benchmark because virtualization was detected: $($probe.virtualization_warning). Pass -AllowVirtualized only for a non-final diagnostic run."
}
if (($probe.power_warning -or $probe.background_load_warning) -and -not $NoFailOnNoise) {
    throw "Host state has material noise flags. Fix them or pass -NoFailOnNoise for a diagnostic/non-final run."
}

$steps = "gate,selftest,timer,tput,tput-lead,latency,correctness,diffab,mem,robust,summarize"
if (-not $SkipSanitizers) { $steps += ",sanitizers" }
$cmd = @(
    "cd '$posixRoot'",
    "DIFFAB_KEYS=$DiffabKeys DIFFAB_WORDS=$DiffabWords DIFFAB_MIN_EVENTS=1000000 SAN_WORDS=20000",
    "bash benchmark/scripts/campaign_rc1.sh --name=$Campaign --sessions=$Sessions --rounds=$Rounds --keys=$Keys --words=$Words --l2rounds=$L2Rounds --diffab-seeds=$DiffabSeeds --steps=$steps --engines=contest,ctl,attrib --candidate --pin=none"
) -join "; "

Write-Host "[winphys] running campaign $Campaign on physical Windows host"
Write-Host "[winphys] bash: $bash"
$main = Invoke-ControlledProcess -FilePath $bash -Arguments "-lc `"$cmd`"" `
    -StdoutPath (Join-Path $logs "windows_campaign.stdout.log") `
    -StderrPath (Join-Path $logs "windows_campaign.stderr.log") `
    -AffinityMask $affinity -PriorityClass $Priority
if ($main.ExitCode -ne 0) {
    Get-Content (Join-Path $logs "windows_campaign.stdout.log") -Tail 80 | Write-Host
    Get-Content (Join-Path $logs "windows_campaign.stderr.log") -Tail 80 | Write-Error
    throw "campaign failed with exit code $($main.ExitCode)"
}

# Independent per-engine process samples for Windows wall/CPU/peak-memory data.
# The order is randomized each iteration to reduce thermal/cache/order bias.
$metricsPath = Join-Path $raw "windows_process_metrics.jsonl"
Remove-Item -ErrorAction SilentlyContinue $metricsPath
$engines = @("kieekey", "kieekey-cand", "kieekey-base", "unikey-4.x")
for ($i = 0; $i -lt $ProcessSamples; $i++) {
    foreach ($engine in ($engines | Sort-Object { Get-Random })) {
        $label = "latency-$engine-$i"
        $cmd2 = "cd '$posixRoot'; benchmark/.build/bench --mode=latency --engines=$engine --rounds=$L2Rounds --keys=$Keys --words=20000 --out='benchmark/results/$Campaign/raw/windows_${label}.jsonl'"
        $m = Invoke-ControlledProcess -FilePath $bash -Arguments "-lc `"$cmd2`"" `
            -StdoutPath (Join-Path $logs "windows_$label.stdout.log") `
            -StderrPath (Join-Path $logs "windows_$label.stderr.log") `
            -AffinityMask $affinity -PriorityClass $Priority
        $row = [ordered]@{
            mode = "windows-process-metric"
            label = $label
            engine = $engine
            iteration = $i
            wall_ms = $m.WallMs
            cpu_ms = $m.CpuMs
            peak_working_set_mib = $m.PeakWorkingSetMiB
            private_memory_mib = $m.PrivateMemoryMiB
            exit_code = $m.ExitCode
        }
        ($row | ConvertTo-Json -Compress) | Add-Content -Encoding UTF8 $metricsPath
        if ($m.ExitCode -ne 0) { throw "$label failed with exit code $($m.ExitCode)" }
    }
}

try {
    $q2 = (Get-Counter '\System\Processor Queue Length').CounterSamples.CookedValue
    $cpu2 = (Get-Counter '\Processor(_Total)\% Processor Time').CounterSamples.CookedValue
    $probe.background_cpu_after = "processor_queue=$([math]::Round($q2,2)); total_cpu_pct=$([math]::Round($cpu2,2))"
    $probe | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $res "windows_host_probe.json")
} catch {}

$reportCmd = "cd '$posixRoot'; python3 benchmark/scripts/windows_physical_report.py --results='benchmark/results/$Campaign'"
$report = Invoke-ControlledProcess -FilePath $bash -Arguments "-lc `"$reportCmd`"" `
    -StdoutPath (Join-Path $logs "windows_report.stdout.log") `
    -StderrPath (Join-Path $logs "windows_report.stderr.log") `
    -AffinityMask ([IntPtr]::Zero) -PriorityClass "Normal"
Get-Content (Join-Path $logs "windows_report.stdout.log") | Write-Host
if ($report.ExitCode -ne 0) {
    Write-Warning "Report produced a FAIL verdict or environment warnings. See benchmark/results/$Campaign/windows_physical_report.md"
} else {
    Write-Host "[winphys] PASS. See benchmark/results/$Campaign/windows_physical_report.md"
}
Write-Host "[winphys] raw artifacts: benchmark/results/$Campaign"
exit $report.ExitCode
