param(
    [ValidateSet("Auto", "Ninja", "VisualStudio")]
    [string]$Generator = "Auto",

    # Graphics backend of the build (shaders + context). By default CMake decides:
    # Windows — D3D11, the other platforms — OpenGL.
    [ValidateSet("", "OpenGL", "D3D11")]
    [string]$Backend = "",

    # slangc for the OpenGL backend (D3D11 compiles HLSL at runtime and requires no tools).
    # By default — Vendor\tools after scripts\fetch_shader_tools.ps1.
    [string]$Slangc = "",
    [string[]]$CMakeArgument = @()
)

. "$PSScriptRoot\panda_windows_common.ps1"

$root = Get-PandaRoot
$cmake = Find-PandaCMake
$buildDir = Get-PandaBuildDir -Root $root -Configuration "Debug"
$generatorInfo = Resolve-PandaGenerator -CMake $cmake -Generator $Generator

Write-Host "Root:      $root"
Write-Host "CMake:     $cmake"
Write-Host "Build dir: $buildDir"
Write-Host "Generator: $($generatorInfo.Name)"

$cmakeArgs = @(
    "-S", $root,
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=Debug",
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
