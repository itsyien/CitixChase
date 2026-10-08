$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$texturePath = Join-Path $projectRoot 'Content\Citix\Textures\T_CitixNight.exr'
$commandletPath = Join-Path $projectRoot 'Source\Citix\Editor\CitixMaterialSetupCommandlet.cpp'
$timeCppPath = Join-Path $projectRoot 'Source\Citix\World\CitixTimeOfDay.cpp'
$timeHeaderPath = Join-Path $projectRoot 'Source\Citix\World\CitixTimeOfDay.h'

$failures = [System.Collections.Generic.List[string]]::new()

if (-not (Test-Path -LiteralPath $texturePath)) {
    $failures.Add('Missing Content/Citix/Textures/T_CitixNight.exr')
} else {
    $dimensions = python -c "import OpenEXR; h=OpenEXR.File(r'$texturePath').header(); lo,hi=h['dataWindow']; print(f'{hi[0]-lo[0]+1}x{hi[1]-lo[1]+1}')"
    if ($LASTEXITCODE -ne 0 -or $dimensions -ne '4096x2048') {
        $failures.Add("Expected a 4096x2048 EXR, got '$dimensions'")
    }
}

$commandlet = Get-Content -Raw -LiteralPath $commandletPath
$timeCpp = Get-Content -Raw -LiteralPath $timeCppPath
$timeHeader = Get-Content -Raw -LiteralPath $timeHeaderPath

foreach ($expected in @(
    'T_CitixNight.exr',
    'TC_HDR',
    'SRGB = false',
    'SkyOpacity',
    'SkyExposure',
    'SkyTint',
    'SkyRotation'
)) {
    if ($commandlet -notmatch [regex]::Escape($expected)) {
        $failures.Add("Material authoring is missing '$expected'")
    }
}

foreach ($expected in @(
    'SkyOpacity',
    'SkyExposure',
    'SkyTint',
    'SkyRotation'
)) {
    if (($timeCpp + $timeHeader) -notmatch [regex]::Escape($expected)) {
        $failures.Add("Time-of-day integration is missing '$expected'")
    }
}

if ($timeCpp -match 'T_SkyNight|T_CitixDusk') {
    $failures.Add('Runtime still references the obsolete T_SkyNight texture')
}

if (($timeCpp + $timeHeader) -match 'DuskSky') {
    $failures.Add('Night-only sky implementation still changes the dusk sky')
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'Night sky source contract passed.'
