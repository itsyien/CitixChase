param(
    [ValidateSet('Unreal', 'Portable')][string]$Mode = 'Unreal',
    [string]$Config,
    [string]$Report,
    [string]$EngineRoot = 'E:\UE_5.8',
    [string]$Project = 'E:\UnrealProjects\CitixChase\Citix.uproject'
)
$ErrorActionPreference = 'Stop'
if (-not $Config) {
    $Config = if ($Mode -eq 'Portable') { Join-Path $PSScriptRoot 'device-proof.ini' }
              else { Join-Path (Split-Path $Project) 'Saved\EOSLocal\device-proof.ini' }
}
$Config = [IO.Path]::GetFullPath($Config)
if (-not (Test-Path -LiteralPath $Config -PathType Leaf)) { throw "Create the private config first: $Config" }
# Expose a hash, never the raw OS machine ID or user name. It is a test marker,
# not proof against cloned machines, copied OS profiles or tampered reports.
$machineMarker = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Cryptography').MachineGuid
$identityMarker = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$hasher = [Security.Cryptography.SHA256]::Create()
try {
    $deviceTag = ([BitConverter]::ToString($hasher.ComputeHash(
        [Text.Encoding]::UTF8.GetBytes("$machineMarker|$identityMarker")))).Replace('-', '').ToLowerInvariant()
} finally { $hasher.Dispose() }
if (-not $Report) {
    $reportFolder = if ($Mode -eq 'Portable') { Join-Path $PSScriptRoot 'Reports' }
                    else { Join-Path (Split-Path $Project) 'Saved\EOSProof' }
    $Report = Join-Path $reportFolder ("$Mode-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
}
$Report = [IO.Path]::GetFullPath($Report)
# Never allow the report/log output to overwrite the credential file.
if ($Report -eq $Config -or [IO.Path]::ChangeExtension($Report, '.log') -eq $Config) {
    throw 'Report/log must not overwrite the credential file.'
}
New-Item -ItemType Directory -Path (Split-Path $Report) -Force | Out-Null
if (Test-Path -LiteralPath $Report) { throw "Choose a fresh report path: $Report" }
if ($Mode -eq 'Unreal') {
    $editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $log = [IO.Path]::ChangeExtension($Report, '.log')
    & $editor $Project '-run=CitixEOSDeviceProof' "-EOSConfig=$Config" "-EOSReport=$Report" `
        "-EOSDeviceTag=$deviceTag" '-unattended' '-nop4' '-nosplash' '-nullrhi' '-nosound' "-abslog=$log"
} else {
    & (Join-Path $PSScriptRoot 'CitixEOSDeviceProof.exe') $Config $Report $deviceTag
}
$proofExit = $LASTEXITCODE
if (Test-Path -LiteralPath $Report) {
    $result = Get-Content -LiteralPath $Report -Raw | ConvertFrom-Json
    Write-Host "Stage: $($result.stage) | Success: $($result.success) | PUID: $($result.puid)"
    if ($result.error) { Write-Host "Reason: $($result.error)" }
    Write-Host "Report: $Report"
}
exit $proofExit
