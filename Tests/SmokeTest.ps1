<#
.SYNOPSIS
    Launches a built WinMon.exe, checks that its main window comes up and keeps
    running, then closes it and checks that the worker thread shuts down cleanly.

.DESCRIPTION
    A failed debug assertion or a crash replaces the main window with a
    "Microsoft Visual C++ Runtime Library" message box or ends the process, so
    both are reported as failures. The Release build requests elevation, so on a
    workstation run it from an elevated shell (CI runners are already elevated).
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'x86')]       [string] $Platform = 'x64',
    [int] $RunSeconds = 8
)

$ErrorActionPreference = 'Stop'

# The solution platform x86 builds the project's Win32 platform, which is the folder name under bin\.
$platformDir = if ($Platform -eq 'x86') { 'Win32' } else { 'x64' }
$exe = Join-Path $PSScriptRoot "..\bin\$platformDir\$Configuration\WinMon.exe"
if (-not (Test-Path $exe)) { throw "WinMon.exe not found at $exe - build $Configuration|$Platform first." }
$exe = (Resolve-Path $exe).Path

Write-Host "Starting $exe"
$process = Start-Process -FilePath $exe -PassThru

try {
    # Wait for the main window.
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
        if ($process.HasExited) { throw "WinMon exited during startup with exit code $($process.ExitCode)." }
    } while ($process.MainWindowHandle -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline)

    if ($process.MainWindowHandle -eq [IntPtr]::Zero) { throw 'WinMon did not create a main window within 30 seconds.' }

    # Let the polling engine run several cycles (process, network and volume sections all refresh).
    Start-Sleep -Seconds $RunSeconds
    $process.Refresh()
    if ($process.HasExited) { throw "WinMon exited while running with exit code $($process.ExitCode)." }

    $title = $process.MainWindowTitle
    Write-Host "Main window title: '$title'"
    if ($title -notlike 'WinMon - System Monitor*') { throw "Unexpected main window '$title' (assertion or error dialog?)." }

    Write-Host ("Running OK: {0} threads, {1:N0} MB working set, {2:N1} s CPU" -f `
        $process.Threads.Count, ($process.WorkingSet64 / 1MB), $process.TotalProcessorTime.TotalSeconds)

    # WM_CLOSE: the dialog stops the worker thread and exits.
    if (-not $process.CloseMainWindow()) { throw 'Could not send WM_CLOSE to the main window.' }
    if (-not $process.WaitForExit(15000)) { throw 'WinMon did not exit within 15 seconds of closing (worker thread hang?).' }
    if ($process.ExitCode -ne 0) { throw "WinMon exited with code $($process.ExitCode)." }

    Write-Host 'Smoke test passed.'
}
finally {
    if (-not $process.HasExited) { $process.Kill() }
}
