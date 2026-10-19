param(
    [ValidateSet("Auto", "Ninja", "VisualStudio")]
    [string]$Generator = "Auto",

    # Graphics backend for shaders and the drawing context. CMake defaults to
    # D3D11 on Windows and OpenGL on the other supported desktop platforms.
    [ValidateSet("", "OpenGL", "D3D11")]
    [string]$Backend = "",

    # Required by the OpenGL backend. Defaults to Vendor\tools when omitted.
    [string]$Slangc = "",
    [string[]]$CMakeArgument = @()
)

. "$PSScriptRoot\panda_windows_common.ps1"

$root = Get-PandaRoot
$cmake = Find-PandaCMake
$buildDir = Get-PandaBuildDir -Root $root -Configuration "Release"
$generatorInfo = Resolve-PandaGenerator -CMake $cmake -Generator $Generator

Write-Host "Root:          $root"
Write-Host "CMake:         $cmake"
Write-Host "Build dir:     $buildDir"
Write-Host "Generator:     $($generatorInfo.Name)"
Write-Host "Configuration: Release"

$cmakeArgs = @(
    "-S", $root,
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
) + $generatorInfo.Args

if ($Backend) {
    $cmakeArgs += "-DPANDA_SHADER_BACKEND=$Backend"
}
if ($Slangc) {
    $cmakeArgs += "-DPANDA_SLANGC_EXECUTABLE=$((Resolve-Path $Slangc).Path)"
}
if ($CMakeArgument.Count -gt 0) {
    $cmakeArgs += $CMakeArgument
}

Invoke-PandaCommand -FilePath $cmake -Arguments $cmakeArgs -WorkingDirectory $root
