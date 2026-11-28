# Registers (or replaces) the Windows scheduled task that runs the v4 bridge
# every 5 minutes. Run from any folder:  powershell -File bridge\install-task.ps1
# Remove with:  Unregister-ScheduledTask -TaskName ClaudeUsageBridge -Confirm:$false

param(
    [string]$TaskName = "ClaudeUsageBridge",
    [string]$PythonW  = ""   # defaults to pythonw.exe next to the python.exe on PATH
)

$ErrorActionPreference = "Stop"
$bridgeDir  = $PSScriptRoot
$projectDir = Split-Path $bridgeDir -Parent
$script     = Join-Path $bridgeDir "bridge.py"

if (-not $PythonW) {
    # Skip the Microsoft Store stub in WindowsApps; use the real install.
    $py = Get-Command python.exe -All | Where-Object { $_.Source -notlike "*\WindowsApps\*" } | Select-Object -First 1
    if (-not $py) { throw "python.exe not found on PATH; pass -PythonW <path>" }
    $PythonW = Join-Path (Split-Path $py.Source) "pythonw.exe"
}
if (-not (Test-Path $PythonW)) { throw "pythonw.exe not found at $PythonW" }

$action  = New-ScheduledTaskAction -Execute $PythonW -Argument "`"$script`"" -WorkingDirectory $projectDir
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) `
             -RepetitionInterval (New-TimeSpan -Minutes 5)
$settings = New-ScheduledTaskSettingsSet `
             -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
             -StartWhenAvailable `
             -MultipleInstances IgnoreNew `
             -ExecutionTimeLimit (New-TimeSpan -Minutes 2)
# Runs as you, only while you're logged on (ccusage reads your profile's JSONL).
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Settings $settings -Principal $principal `
    -Description "Claude usage monitor v4: ccusage -> Worker /ingest-cc every 5 min" -Force | Out-Null

Write-Host "Registered '$TaskName': $PythonW `"$script`" every 5 min (first run in ~1 min)."
