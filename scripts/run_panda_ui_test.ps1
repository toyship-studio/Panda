. "$PSScriptRoot\panda_windows_common.ps1"

$root = Get-PandaRoot
$binDir = Join-Path $root "bin"
$exe = Join-Path $binDir "PandaUITest.exe"

if (-not (Test-Path $exe)) {
    throw "PandaUITest.exe was not found at $exe. Run scripts\build_panda_ui_test.ps1 first."
}

Push-Location $binDir
try {
    & $exe
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
