param(
    [ValidateSet("Auto", "Ninja", "VisualStudio")]
    [string]$Generator = "Auto"
)

# Release PandaSDK package for Windows. macOS and iOS are built by
# build_panda_sdk_release.sh on macOS.

. "$PSScriptRoot\panda_windows_common.ps1"

$root = Get-PandaRoot
$cmake = Find-PandaCMake
$buildDir = Join-Path $root "cmake-build-sdk-release-windows"
$generatorInfo = Resolve-PandaGenerator -CMake $cmake -Generator $Generator
$parallel = Get-PandaBuildParallel
$sdkDir = Join-Path $root "bin\PandaSDK"

Write-Host "Root:          $root"
Write-Host "CMake:         $cmake"
Write-Host "Build dir:     $buildDir"
Write-Host "Generator:     $($generatorInfo.Name)"
Write-Host "Configuration: Release"

$configureArgs = @(
    "-S", $root,
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DPANDA_EDITOR_API=OFF",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
) + $generatorInfo.Args

Invoke-PandaCommand -FilePath $cmake -Arguments $configureArgs -WorkingDirectory $root

# bin\PandaSDK is shared by Debug and Release build trees. Force this build to
# refresh the packaged libraries even if another tree wrote newer stamp files.
Remove-Item (Join-Path $sdkDir ".package.stamp") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $sdkDir "lib\windows\PandaRuntime.stamp") -Force -ErrorAction SilentlyContinue

$coreBuildArgs = Get-PandaCMakeBuildArguments `
    -BuildDir $buildDir `
    -Target "PandaSDKCorePackage" `
    -Configuration "Release" `
    -Parallel $parallel
Invoke-PandaCommand -FilePath $cmake -Arguments $coreBuildArgs -WorkingDirectory $root

$runtimeBuildArgs = Get-PandaCMakeBuildArguments `
    -BuildDir $buildDir `
    -Target "PandaSDKPackage" `
    -Configuration "Release" `
    -Parallel $parallel
Invoke-PandaCommand -FilePath $cmake -Arguments $runtimeBuildArgs -WorkingDirectory $root

Write-Host "PandaSDK Release packaged at: $sdkDir"
