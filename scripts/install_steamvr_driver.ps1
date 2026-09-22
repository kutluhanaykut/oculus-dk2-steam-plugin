# Installs the DK2 SteamVR driver into the SteamVR drivers directory.
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts\install_steamvr_driver.ps1
#
# SteamVR usually lives under Program Files (x86), so this needs to run
# elevated. It re-launches itself as administrator if it is not.

[CmdletBinding()]
param(
    [string]$BuildDir = '',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    # Set internally when the script re-launches itself elevated.
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    if ($Elevated) {
        throw 'Elevation was requested but the script is still not running as administrator.'
    }
    Write-Host 'Re-launching as administrator...' -ForegroundColor Yellow
    $arguments = @(
        '-ExecutionPolicy', 'Bypass',
        '-NoProfile',
        '-File', "`"$PSCommandPath`"",
        '-Configuration', $Configuration,
        '-Elevated'
    )
    if ($BuildDir) { $arguments += @('-BuildDir', "`"$BuildDir`"") }
    Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -Wait
    return
}

function Find-SteamVR {
    $steamPath = ''
    foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        $entry = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($entry -and $entry.SteamPath) { $steamPath = $entry.SteamPath; break }
        if ($entry -and $entry.InstallPath) { $steamPath = $entry.InstallPath; break }
    }
    if (-not $steamPath) { $steamPath = 'C:\Program Files (x86)\Steam' }

    # SteamVR is often on a different drive than Steam itself, so follow the
    # library folder list rather than assuming the default library.
    $candidates = @((Join-Path $steamPath 'steamapps\common\SteamVR'))
    $libraryFile = Join-Path $steamPath 'steamapps\libraryfolders.vdf'
    if (Test-Path $libraryFile) {
        foreach ($match in ([regex]'"path"\s+"([^"]+)"').Matches((Get-Content $libraryFile -Raw))) {
            $library = $match.Groups[1].Value -replace '\\\\', '\'
            $candidates += (Join-Path $library 'steamapps\common\SteamVR')
        }
    }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "SteamVR not found. Looked in:`n  $($candidates -join "`n  ")`nInstall SteamVR from Steam and try again."
}

function Find-BuildOutput {
    param([string]$BuildDir)

    if ($BuildDir) {
        $candidate = Join-Path $BuildDir 'bin\drivers\dk2vr'
        if (Test-Path $candidate) { return $candidate }
        throw "Build output not found at $candidate"
    }

    # Resolved against the repository root, not the current directory, so the
    # script works from anywhere.
    $candidates = @(
        (Join-Path $Root 'build\bin\drivers\dk2vr'),
        (Join-Path $Root "build\bin\$Configuration\drivers\dk2vr"),
        (Join-Path $Root 'out\build\bin\drivers\dk2vr')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "Build output not found. Run scripts\build.ps1 first, or pass -BuildDir.`nLooked in:`n  $($candidates -join "`n  ")"
}

Write-Host '=== DK2 SteamVR Driver Installer ===' -ForegroundColor Cyan

$steamVrPath = Find-SteamVR
Write-Host "SteamVR: $steamVrPath" -ForegroundColor Green

$buildOutput = Find-BuildOutput -BuildDir $BuildDir
Write-Host "Build output: $buildOutput" -ForegroundColor Green

$destDir = Join-Path $steamVrPath 'drivers\dk2vr'
$destDriverDir = Join-Path $destDir 'bin\win64'
New-Item -ItemType Directory -Force -Path $destDriverDir | Out-Null

$dllSource = Join-Path $buildOutput 'bin\win64\driver_dk2vr.dll'
if (-not (Test-Path $dllSource)) {
    $found = Get-ChildItem -Path $buildOutput -Filter 'driver_dk2vr.dll' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $found) { throw 'driver_dk2vr.dll not found in the build output.' }
    $dllSource = $found.FullName
}
Copy-Item $dllSource (Join-Path $destDriverDir 'driver_dk2vr.dll') -Force
Write-Host '  driver_dk2vr.dll' -ForegroundColor Green

$manifestSource = Join-Path $buildOutput 'driver.vrdrivermanifest'
if (-not (Test-Path $manifestSource)) {
    $manifestSource = Join-Path $Root 'steamvr_driver\driver.vrdrivermanifest'
}
if (-not (Test-Path $manifestSource)) { throw 'driver.vrdrivermanifest not found.' }
Copy-Item $manifestSource (Join-Path $destDir 'driver.vrdrivermanifest') -Force
Write-Host '  driver.vrdrivermanifest' -ForegroundColor Green

Write-Host ''
Write-Host "Installed to: $destDir" -ForegroundColor Cyan
Write-Host ''
Write-Host 'Next steps:' -ForegroundColor Yellow
Write-Host '  1. Connect the DK2 over HDMI and USB.'
Write-Host '  2. In Windows display settings, extend the desktop onto the DK2.'
Write-Host '  3. Restart SteamVR.'
Write-Host '  4. If the image looks rotated, set "image_rotation" to 90, 180 or 270'
Write-Host '     in the dk2vr_display section of Steam\config\steamvr.vrsettings.'
Write-Host ''
Write-Host 'Driver log (useful when something does not work):' -ForegroundColor Yellow
Write-Host "  $(Join-Path $steamVrPath '..\..\..\logs\vrserver.txt')"
