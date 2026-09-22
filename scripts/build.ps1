[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$Clean,
    [switch]$Package,
    [switch]$SkipTests,
    # Override the CMake generator. Left empty, CMake picks the newest
    # Visual Studio it finds, so this works on 2022 and on 2026 without the
    # script having to know which is installed.
    [string]$Generator = ''
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $Root 'build'

# CMake ships with Visual Studio Build Tools but is not always on PATH.
$CmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($CmakeCommand) {
    $Cmake = $CmakeCommand.Source
} else {
    $vsCmake = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $vsCmake) {
        throw 'CMake not found. Install CMake, or Visual Studio Build Tools with CMake support.'
    }
    $Cmake = $vsCmake.FullName
}
$Ctest = Join-Path (Split-Path $Cmake) 'ctest.exe'
Write-Host "Using CMake: $Cmake" -ForegroundColor Cyan

if ($Clean -and (Test-Path $BuildDirectory)) {
    Write-Host "Removing $BuildDirectory"
    Remove-Item $BuildDirectory -Recurse -Force
}

$CmakeArguments = @('-S', $Root, '-B', $BuildDirectory)
if ($Generator) {
    $CmakeArguments += @('-G', $Generator)
}
$CmakeArguments += @('-A', 'x64', '-DDK2VR_BUILD_TESTS=ON')

& $Cmake @CmakeArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& $Cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }

if (-not $SkipTests) {
    & $Ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}

if ($Package) {
    & (Join-Path $PSScriptRoot 'package.ps1') -Configuration $Configuration
}

Write-Host ''
Write-Host 'Build complete.' -ForegroundColor Green
Write-Host "  Driver:    $(Join-Path $BuildDirectory 'bin\drivers\dk2vr')"
Write-Host "  Installer: $(Join-Path $BuildDirectory "bin\$Configuration\DK2SteamVRDriverInstaller.exe")"
Write-Host ''
Write-Host 'Install it with:' -ForegroundColor Cyan
Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\install_steamvr_driver.ps1"
