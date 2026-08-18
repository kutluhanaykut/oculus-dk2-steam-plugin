[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$SkipBootstrap,
    [switch]$Clean,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $Root 'build'

# Locate CMake. It ships with Visual Studio Build Tools but is not always on PATH.
$CmakeExe = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $CmakeExe) {
    $vsCmake = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($vsCmake) {
        $CmakeExe = $vsCmake.FullName
    } else {
        throw 'CMake not found. Install CMake or Visual Studio Build Tools with CMake support.'
    }
}
$Cmake = if ($CmakeExe -is [string]) { $CmakeExe } else { $CmakeExe.Source }
$Ctest = Join-Path (Split-Path $Cmake) 'ctest.exe'
Write-Host "Using CMake: $Cmake" -ForegroundColor Cyan
Write-Host "Using CTest: $Ctest" -ForegroundColor Cyan

if (-not $SkipBootstrap) {
    & (Join-Path $PSScriptRoot 'bootstrap.ps1')
}
if ($Clean -and (Test-Path $BuildDirectory)) {
    Write-Host "Removing $BuildDirectory"
    Remove-Item $BuildDirectory -Recurse -Force
}

$CmakeArguments = @(
    '-S', $Root,
    '-B', $BuildDirectory,
    '-G', 'Visual Studio 18 2026',
    '-A', 'x64',
    '-DDK2VR_BUILD_TESTS=ON',
    '-DDK2VR_PACKAGE_VLC_PLUGINS=OFF'
)
& $Cmake @CmakeArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& $Cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }

& $Ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

if ($Package) {
    & (Join-Path $PSScriptRoot 'package.ps1') -Configuration $Configuration
}

$Exe = Join-Path $BuildDirectory "bin\$Configuration\DK2VRPlayer.exe"
Write-Host ''
Write-Host "Build complete: $Exe" -ForegroundColor Green
