. "$PSScriptRoot\panda_windows_common.ps1"

$root = Get-PandaRoot
$buildDir = Get-PandaBuildDir -Root $root -Configuration "Debug"
$resolvedRoot = (Resolve-Path $root).Path

if (-not (Test-Path $buildDir)) {
    Write-Host "Nothing to clean: $buildDir"
    exit 0
}

$resolvedBuildDir = (Resolve-Path $buildDir).Path
if (-not $resolvedBuildDir.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove a build directory outside the repository: $resolvedBuildDir"
}

Remove-Item -LiteralPath $resolvedBuildDir -Recurse -Force
Write-Host "Removed $resolvedBuildDir"
