$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-PandaRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..")).Path
}

function Find-PandaCMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $candidates = @()
    if ($env:ProgramFiles) {
        $vsRoot = Join-Path $env:ProgramFiles "Microsoft Visual Studio"
        if (Test-Path $vsRoot) {
            foreach ($version in Get-ChildItem -Path $vsRoot -Directory -ErrorAction SilentlyContinue) {
                foreach ($edition in Get-ChildItem -Path $version.FullName -Directory -ErrorAction SilentlyContinue) {
                    $path = Join-Path $edition.FullName "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
                    if (Test-Path $path) {
                        $candidates += $path
                    }
                }
            }
        }

        $clionRoot = Join-Path $env:ProgramFiles "JetBrains"
        if (Test-Path $clionRoot) {
            foreach ($cmake in Get-ChildItem -Path $clionRoot -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue) {
                if ($cmake.FullName -like "*\bin\cmake\win\*\bin\cmake.exe") {
                    $candidates += $cmake.FullName
                }
            }
        }
    }

    if ($candidates.Count -gt 0) {
        return ($candidates | Sort-Object -Descending | Select-Object -First 1)
    }

    throw "CMake was not found. Install CMake or Visual Studio with C++ CMake tools, or add cmake.exe to PATH."
}

function Get-PandaBuildDir {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Configuration = "Debug"
    )

    return (Join-Path $Root ("cmake-build-" + $Configuration.ToLowerInvariant()))
}

function Get-PandaBuildParallel {
    if ($env:PANDA_BUILD_PARALLEL) {
        return [int]$env:PANDA_BUILD_PARALLEL
    }
    return [Math]::Max(1, [Environment]::ProcessorCount)
}

function Test-PandaVisualStudioBuildDir {
    param([Parameter(Mandatory = $true)][string]$BuildDir)

    $cachePath = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path $cachePath)) {
        return $false
    }

    $generatorLine = Select-String -Path $cachePath -Pattern "^CMAKE_GENERATOR:INTERNAL=Visual Studio" -ErrorAction SilentlyContinue
    return [bool]$generatorLine
}

function Get-PandaCMakeBuildArguments {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [string]$Target = "",
        [string]$Configuration = "Debug",
        [int]$Parallel = 1
    )

    $arguments = @("--build", $BuildDir)
    if ($Target) {
        $arguments += @("--target", $Target)
    }
    $arguments += @("--config", $Configuration, "--parallel", "$Parallel")

    if (Test-PandaVisualStudioBuildDir -BuildDir $BuildDir) {
        $arguments += @("--", "/nodeReuse:false")
    }

    return $arguments
}

function Get-PandaLatestVisualStudioGenerator {
    param([Parameter(Mandatory = $true)][string]$CMake)

    $help = & $CMake --help
    $generators = @()
    foreach ($line in $help) {
        if ($line -match "Visual Studio ([0-9]+) ([0-9]+)") {
            $generators += [PSCustomObject]@{
                Major = [int]$Matches[1]
                Year = [int]$Matches[2]
                Name = "Visual Studio $($Matches[1]) $($Matches[2])"
            }
        }
    }

    if ($generators.Count -eq 0) {
        throw "No Visual Studio CMake generator was found in '$CMake --help'."
    }

    return ($generators | Sort-Object Major, Year -Descending | Select-Object -First 1).Name
}

function Resolve-PandaGenerator {
    param(
        [Parameter(Mandatory = $true)][string]$CMake,
        [ValidateSet("Auto", "Ninja", "VisualStudio")][string]$Generator = "Auto"
    )

    if ($Generator -eq "Ninja") {
        if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
            throw "Ninja was requested but ninja.exe was not found in PATH."
        }
        if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
            throw "Ninja was requested but cl.exe was not found. Run this from Developer PowerShell or use -Generator VisualStudio."
        }
        return @{ Args = @("-G", "Ninja"); Name = "Ninja" }
    }

    if ($Generator -eq "Auto" -and
        (Get-Command ninja -ErrorAction SilentlyContinue) -and
        (Get-Command cl -ErrorAction SilentlyContinue)) {
        return @{ Args = @("-G", "Ninja"); Name = "Ninja" }
    }

    $vsGenerator = Get-PandaLatestVisualStudioGenerator -CMake $CMake
    return @{ Args = @("-G", $vsGenerator, "-A", "x64"); Name = $vsGenerator }
}

function Invoke-PandaCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    Push-Location $WorkingDirectory
    $previousDisableNodeReuse = $env:MSBUILDDISABLENODEREUSE
    try {
        if ($Arguments -contains "--build") {
            $env:MSBUILDDISABLENODEREUSE = "1"
        }

        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')"
        }
    } finally {
        if ($null -eq $previousDisableNodeReuse) {
            Remove-Item Env:\MSBUILDDISABLENODEREUSE -ErrorAction SilentlyContinue
        } else {
            $env:MSBUILDDISABLENODEREUSE = $previousDisableNodeReuse
        }
        Pop-Location
    }
}

function Resolve-PandaBuiltExecutable {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [Parameter(Mandatory = $true)][string]$RelativeDir,
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$Configuration = "Debug"
    )

    $candidates = @(
        (Join-Path $BuildDir "$RelativeDir\$Name.exe"),
        (Join-Path $BuildDir "$RelativeDir\$Configuration\$Name.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "Executable was not found after build: $Name.exe"
}
