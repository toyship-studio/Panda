param(
    [int]$Parallel = 0
)

. "$PSScriptRoot\panda_windows_common.ps1"

$configuration = "Release"
$root = Get-PandaRoot
$cmake = Find-PandaCMake
$buildDir = Get-PandaBuildDir -Root $root -Configuration $configuration
if ($Parallel -le 0) {
    $Parallel = Get-PandaBuildParallel
}

if (-not (Test-Path $buildDir)) {
    throw "Build directory does not exist: $buildDir. Run scripts\configure_panda_release.ps1 first."
}

$arguments = Get-PandaCMakeBuildArguments `
    -BuildDir $buildDir `
    -Configuration $configuration `
    -Parallel $Parallel
Invoke-PandaCommand `
    -FilePath $cmake `
    -Arguments $arguments `
    -WorkingDirectory $root
