# Downloads slangc (the HLSL → SPIR-V compiler) into Vendor\tools\slang\windows\x86_64.
# The only external tool of the shader toolchain; the SPIR-V → GLSL/MSL conversion is done by the
# vendored SPIRV-Cross inside MirenTools (context/rendering.md). The Vulkan SDK is not needed.
# On Windows with the D3D11 backend (the default) slangc is not required for the build — the editor
# needs it to export games for Metal/OpenGL, and it is baked into its distribution.
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$slangVersion = "2026.14.1"
$asset = "slang-$slangVersion-windows-x86_64.zip"
$sha256 = "5ed0a59d650a0af0aca45d5db4e083b3d8fb5cea05748747dd95dfbe9c580658"

$root = (Resolve-Path (Join-Path (Split-Path -Parent $PSCommandPath) "..")).Path
$destination = Join-Path $root "Vendor\tools\slang\windows\x86_64"
$marker = Join-Path $destination ".version"

if (-not $Force -and (Test-Path $marker) -and ((Get-Content $marker -Raw).Trim() -eq $slangVersion)) {
    Write-Host "slangc $slangVersion is already in $destination (-Force to re-download)"
    exit 0
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("panda-slang-" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $archive = Join-Path $work $asset
    $url = "https://github.com/shader-slang/slang/releases/download/v$slangVersion/$asset"
    Write-Host "-> $url"
    Invoke-WebRequest -Uri $url -OutFile $archive

    $actual = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
    if ($actual -ne $sha256) {
        throw "sha256 mismatch: $actual != $sha256"
    }

    Expand-Archive -Path $archive -DestinationPath $work
    if (Test-Path $destination) {
        Remove-Item -Recurse -Force $destination
    }
    New-Item -ItemType Directory -Path (Join-Path $destination "bin") | Out-Null
    Copy-Item (Join-Path $work "bin\slangc.exe") (Join-Path $destination "bin\")
    # Compiler DLLs without slang-llvm (~100 MB — Slang CPU targets, not needed).
    Get-ChildItem (Join-Path $work "bin") -Filter "*.dll" |
        Where-Object { $_.Name -notlike "*llvm*" } |
        Copy-Item -Destination (Join-Path $destination "bin\")
    if (Test-Path (Join-Path $work "LICENSE")) {
        Copy-Item (Join-Path $work "LICENSE") (Join-Path $destination "LICENSE")
    }

    & (Join-Path $destination "bin\slangc.exe") -v | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "slangc.exe does not run from $destination"
    }
    Set-Content -Path $marker -Value $slangVersion
    Write-Host "slangc $slangVersion -> $destination"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
