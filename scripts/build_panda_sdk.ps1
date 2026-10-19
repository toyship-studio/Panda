param(
    [ValidateSet("Auto", "Ninja", "VisualStudio")]
    [string]$Generator = "Auto"
)

# The Windows counterpart of build_panda_sdk.sh: builds PandaSDKPackage (bin/PandaSDK —
# libraries, PandaRuntime headers, cmake configs) in a separate build folder.
# On Windows there is a single target platform; the sh version additionally builds iOS.

. "$PSScriptRoot\panda_windows_common.ps1"

$root = Get-PandaRoot
$cmake = Find-PandaCMake
$buildDir = Join-Path $root "cmake-build-sdk-windows"
$generatorInfo = Resolve-PandaGenerator -CMake $cmake -Generator $Generator
$parallel = Get-PandaBuildParallel
$sdkDir = Join-Path $root "bin\PandaSDK"

Write-Host "Root:      $root"
Write-Host "CMake:     $cmake"
Write-Host "Build dir: $buildDir"
Write-Host "Generator: $($generatorInfo.Name)"

$configureArgs = @(
    "-S", $root,
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=Debug",
    "-DPANDA_EDITOR_API=OFF",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
) + $generatorInfo.Args

Invoke-PandaCommand -FilePath $cmake -Arguments $configureArgs -WorkingDirectory $root

Remove-Item (Join-Path $sdkDir ".package.stamp") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $sdkDir "lib\windows\PandaRuntime.stamp") -Force -ErrorAction SilentlyContinue

$coreBuildArgs = Get-PandaCMakeBuildArguments `
    -BuildDir $buildDir `
    -Target "PandaSDKCorePackage" `
    -Configuration "Debug" `
    -Parallel $parallel
Invoke-PandaCommand -FilePath $cmake -Arguments $coreBuildArgs -WorkingDirectory $root

$runtimeBuildArgs = Get-PandaCMakeBuildArguments `
    -BuildDir $buildDir `
    -Target "PandaSDKPackage" `
    -Configuration "Debug" `
    -Parallel $parallel
Invoke-PandaCommand -FilePath $cmake -Arguments $runtimeBuildArgs -WorkingDirectory $root

Write-Host "PandaSDK packaged at: $sdkDir"
