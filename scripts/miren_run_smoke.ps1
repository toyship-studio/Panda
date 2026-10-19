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

$arguments = Get-PandaCMakeBuildArguments -BuildDir $buildDir -Target "MirenSmoke" -Configuration $Configuration -Parallel $Parallel
Invoke-PandaCommand `
    -FilePath $cmake `
    -Arguments $arguments `
    -WorkingDirectory $root

# The test opens a window (Fern) and draws through the active build backend (on Windows — D3D11):
# a depth-only pass into DEPTH32F + SampleCmp; it compiles the shaders itself from
# Miren\Smoke\shaders.
$exe = Resolve-PandaBuiltExecutable -BuildDir $buildDir -RelativeDir "Miren\Smoke" -Name "MirenSmoke" -Configuration $Configuration
Invoke-PandaCommand -FilePath $exe -Arguments @() -WorkingDirectory (Split-Path -Parent $exe)
