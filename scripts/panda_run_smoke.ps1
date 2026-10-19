param(
    [string]$Configuration = "Debug",
    [int]$Parallel = 0
)

. "$PSScriptRoot\panda_windows_common.ps1"

$root = Get-PandaRoot
$cmake = Find-PandaCMake
$buildDir = Get-PandaBuildDir -Root $root -Configuration $Configuration
if ($Parallel -le 0) {
    $Parallel = Get-PandaBuildParallel
}

if (-not (Test-Path $buildDir)) {
    throw "Build directory does not exist: $buildDir. Run scripts\configure_panda_debug.ps1 first."
}

$arguments = Get-PandaCMakeBuildArguments -BuildDir $buildDir -Target "PandaSmoke" -Configuration $Configuration -Parallel $Parallel
Invoke-PandaCommand `
    -FilePath $cmake `
    -Arguments $arguments `
    -WorkingDirectory $root

# Engine smoke test (Panda\Smoke): a World with a plane, a cube and a sun → offscreen render →
# pixel checks of the cascade shadows (on Windows — D3D11). The engine resources live next to the
# exe in bin\.
$exe = Join-Path $root "bin\PandaSmoke.exe"
if (-not (Test-Path $exe)) {
    throw "Executable was not found after build: $exe"
}
Invoke-PandaCommand -FilePath $exe -Arguments @() -WorkingDirectory (Split-Path -Parent $exe)
