# Chase multiplayer smoke test: listen host + guest(s).
# Requires the local Zen storage service (the cooked project store) to be reachable;
# starting the editor brings it up. Logs are written per instance via -abslog.
# Usage: powershell -File Tools\chase_mp_test.ps1 [-Seconds 60] [-Extra "flag flag"] [-Tag run] [-Guests 1]
param(
    [int]$Seconds = 60,
    [string]$Extra = "",
    [string]$Tag = "run",
    [switch]$Render,
    [switch]$HeadlessHost,
    [int]$HostWarmup = 22,
    [int]$Guests = 1,
    [switch]$Editor,
    [switch]$Hillside,
	[switch]$Packaged,
    [switch]$PackagedGuest,
    [string]$PackageDirectory = "",
    [switch]$LobbyButtons,
    [int]$Port = 7777,
    [int]$DisconnectGuestAfter = 0,
    [int]$DisconnectHostAfter = 0,
    [string]$JoinAddress = ""
)

$ErrorActionPreference = "Continue"
$root = "E:\UnrealProjects\CitixChase"
$exe = Join-Path $root "Binaries\Win64\Citix.exe"
if ($Editor) { $exe = 'E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' }
if ($Packaged) { $packageRoot = if ($PackageDirectory) { $PackageDirectory } else { Join-Path $root 'PackageShipping' }; $exe = Join-Path $packageRoot 'Windows\Citix\Binaries\Win64\Citix-Win64-Shipping.exe'; $LobbyButtons=$true } # Shipping ignores startup map/IP overrides: use the actual Host/Join buttons.
$logDir = Join-Path $root "Saved\Logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

# Aim validation depends on the client's camera updates; NullRHI is not a camera test.
if ($Extra -match 'CitixChaseTestPistol' -and !$Render) { $Render = $true; $Extra += ' -windowed -ForceRes -ResX=1280 -ResY=720'; Write-Host 'Pistol test: using a rendered client viewport.' }
$rhiArgs = if ($Render) { @() } else { @("-nullrhi") }
$extraArgs = @()
$ownedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
if ($Extra) { $extraArgs = $Extra.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries) }
if ($LobbyButtons) { $extraArgs += "-CitixLobbyButtons" }

function Start-Instance([string]$name, [string[]]$instArgs) {
    $log = Join-Path $logDir "$name.log"
    Remove-Item $log -ErrorAction SilentlyContinue
    $instanceRhiArgs = if ($HeadlessHost -and $name.EndsWith("_Host")) { @("-nullrhi") } else { $rhiArgs }
    $instanceExe=$exe; $instanceEditor=$Editor; $instanceExtraArgs=@($extraArgs)
    if ($name.EndsWith('_B') -and $LobbyButtons) { $instanceExtraArgs=@($instanceExtraArgs | Where-Object { $_ -ne '-CitixLANProbe' }) }
    if ($PackagedGuest -and !$name.EndsWith("_Host")) {
      $packageRoot = if ($PackageDirectory) { $PackageDirectory } else { Join-Path $root 'PackageShipping' }
      $instanceExe=Join-Path $packageRoot 'Windows\Citix\Binaries\Win64\Citix-Win64-Shipping.exe'; $instanceEditor=$false; $instanceExtraArgs += '-CitixLobbyButtons'
    }
    $a = @($instArgs) + $instanceRhiArgs + $instanceExtraArgs + @("-NoSound", "-unattended", "-nosplash", "-log", "-stdout", "-abslog=$log")
    # Separate PCs have separate saved settings. Match that boundary for local probes.
    $profileDir=Join-Path $root 'Saved\TestProfiles'
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
    $a += "-GameUserSettingsINI=$(Join-Path $profileDir ($name + '.ini'))"
    if ($instanceEditor) { $a = @((Join-Path $root 'Citix.uproject')) + $a }
    Write-Host "START $name"
    $ownedProcesses.Add((Start-Process -FilePath $instanceExe -ArgumentList $a -WindowStyle Hidden -PassThru))
}

$map = "/Engine/Maps/Templates/Template_Default"
# A Game-target exe cannot be a dedicated server (IsRunningDedicatedServer is
# compile-time), so the host is a listen server and the guests join by IP.

$hostMap = if ($LobbyButtons) { $map } elseif ($Hillside) { "$map`?listen`?CitixMap=Hillside" } else { "$map`?listen" }
Start-Instance "ChaseMP_${Tag}_Host" @($hostMap, "-port=$Port", "-game", "-CitixNetLog", "-CitixNetTag=Host", "-CitixName=Host")
Start-Sleep -Seconds $HostWarmup

$guestTarget = if ($LobbyButtons) { $map } else { "127.0.0.1:$Port" }
if (!$JoinAddress) { $JoinAddress = "127.0.0.1:$Port" }
Start-Instance "ChaseMP_${Tag}_A" @($guestTarget, "-CitixLobbyJoin=$JoinAddress", "-game", "-CitixNetLog", "-CitixNetTag=A", "-CitixName=Alpha")
if ($Guests -ge 2) {
    if ($LobbyButtons) {
        Start-Sleep -Seconds 25
        Start-Instance "ChaseMP_${Tag}_B" @($map, "-CitixLobbyJoin=127.0.0.1:$Port", "-game", "-CitixNetLog", "-CitixNetTag=B", "-CitixName=Bravo")
    } else {
        Start-Instance "ChaseMP_${Tag}_B" @("127.0.0.1:$Port", "-game", "-CitixNetLog", "-CitixNetTag=B", "-CitixName=Bravo")
    }
}

Write-Host "Running for $Seconds seconds..."
if ($DisconnectHostAfter -gt 0 -and $DisconnectHostAfter -lt $Seconds) {
    Start-Sleep -Seconds $DisconnectHostAfter
    if (!$ownedProcesses[0].HasExited) { Stop-Process -Id $ownedProcesses[0].Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds ($Seconds-$DisconnectHostAfter)
} elseif ($DisconnectGuestAfter -gt 0 -and $DisconnectGuestAfter -lt $Seconds) {
    Start-Sleep -Seconds $DisconnectGuestAfter
    for ($testGuestIndex=1; $testGuestIndex -lt $ownedProcesses.Count; ++$testGuestIndex) { if (!$ownedProcesses[$testGuestIndex].HasExited) { Stop-Process -Id $ownedProcesses[$testGuestIndex].Id -Force -ErrorAction SilentlyContinue } }
    Start-Sleep -Seconds ($Seconds-$DisconnectGuestAfter)
} else { Start-Sleep -Seconds $Seconds }

foreach ($instance in $ownedProcesses) { if (!$instance.HasExited) { Stop-Process -Id $instance.Id -Force -ErrorAction SilentlyContinue } }
Start-Sleep -Seconds 2
Write-Host "DONE. Logs: $logDir\ChaseMP_${Tag}_*.log"
