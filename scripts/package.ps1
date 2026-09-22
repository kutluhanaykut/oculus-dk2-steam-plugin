[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$BuildRoot = Join-Path $Root 'build\bin'
$DriverSource = Join-Path $BuildRoot 'drivers\dk2vr'
$Installer = Join-Path $BuildRoot "$Configuration\DK2SteamVRDriverInstaller.exe"
$Report = Join-Path $BuildRoot "$Configuration\DK2DisplayReport.exe"
$StageRoot = Join-Path $Root 'dist\DK2-SteamVR-Driver'
$Archive = Join-Path $Root 'dist\DK2-SteamVR-Driver-win64.zip'

if (-not (Test-Path $DriverSource)) {
    throw "Driver output not found: $DriverSource. Run scripts\build.ps1 first."
}

Remove-Item $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $Archive -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $StageRoot | Out-Null

# The driver tree keeps its layout: SteamVR expects the manifest one level
# above bin/win64/, so the folder is copied wholesale.
Copy-Item $DriverSource (Join-Path $StageRoot 'drivers\dk2vr') -Recurse

if (Test-Path $Installer) {
    Copy-Item $Installer $StageRoot
} else {
    Write-Warning "Installer EXE not found at $Installer; packaging the driver only."
}

if (Test-Path $Report) {
    Copy-Item $Report $StageRoot
}

Copy-Item (Join-Path $Root 'scripts\install_steamvr_driver.ps1') $StageRoot
Copy-Item (Join-Path $Root 'README.md') $StageRoot
Copy-Item (Join-Path $Root 'LICENSE') $StageRoot
Copy-Item (Join-Path $Root 'THIRD_PARTY_NOTICES.md') $StageRoot

Compress-Archive -Path $StageRoot -DestinationPath $Archive -CompressionLevel Optimal
Write-Host "Package: $Archive" -ForegroundColor Green
