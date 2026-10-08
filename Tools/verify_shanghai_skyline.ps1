$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$types = Get-Content -Raw (Join-Path $root 'Source\Citix\Core\CitixTypes.h')
$builder = Get-Content -Raw (Join-Path $root 'Source\Citix\City\CitixCityBuilder.cpp')
$generator = Get-Content -Raw (Join-Path $root 'Source\Citix\City\CitixBuildingGenerator.cpp')
$gameMode = Get-Content -Raw (Join-Path $root 'Source\Citix\Player\CitixDrivingGameMode.cpp')
$material = Get-Content -Raw (Join-Path $root 'Source\Citix\Editor\CitixMaterialSetupCommandlet.cpp')

$required = @(
    'ECitixLandmarkStyle', 'PearlBroadcastTower', 'TwistingSupertall',
    'CrownOpeningTower', 'TieredCrownTower', 'LandmarkStyle',
    'AssignShanghaiHeroLandmarks', 'GeneratePearlBroadcastTower',
    'GenerateTwistingSupertall', 'GenerateCrownOpeningTower',
    'GenerateTieredCrownTower', 'CitixSkylineView', 'ComponentMask', 'HeroInstances'
)

$all = $types + $builder + $generator + $gameMode + $material
$missing = $required | Where-Object { $all -notmatch [regex]::Escape($_) }
if ($missing) { throw "Shanghai skyline contract missing: $($missing -join ', ')" }
if ($material -notmatch 'UV, TEXT\(""\), U, TEXT\(""\)') {
    throw 'Window material must connect UV to ComponentMask through its unnamed input pin.'
}
if ($material -notmatch 'Masked, TEXT\(""\), Final, TEXT\("B"\)') {
    throw 'Window material pane mask must feed the final emissive multiply (broken-window grid).'
}
if ($generator -notmatch 'FloorHeight \* 0\.36f \* Profile\.BandHeightScale') {
    throw 'Lit window bands must be a thin ribbon of the floor height, not a full-height slab.'
}
if ($generator -notmatch '(?s)AddTowerAccents.*?EmissiveWarm.*?EmissiveCool') {
    throw 'Tower accent strips must use the solid emissive material, not a window/pane-mask surface.'
}
if ($generator -notmatch 'Base \+ H \* 0\.86f') {
    throw 'Pearl antenna must be seated into the top bulb, not floating above it.'
}

Write-Host 'Shanghai skyline source contract passed.'
