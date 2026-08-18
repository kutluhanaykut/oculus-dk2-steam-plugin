# Installs the DK2 SteamVR driver into the SteamVR drivers directory.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/install_steamvr_driver.ps1 [-BuildDir <path>]

param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

function Find-SteamVR {
    $steamPath = ""
    $steamRegistry = Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue
    if ($steamRegistry -and $steamRegistry.SteamPath) {
        $steamPath = $steamRegistry.SteamPath
    }

    if (-not $steamPath) {
        $steamPath = "C:\Program Files (x86)\Steam"
    }

    $steamVrPath = Join-Path $steamPath "steamapps\common\SteamVR"
    if (-not (Test-Path $steamVrPath)) {
        throw "SteamVR not found at $steamVrPath. Please install SteamVR from Steam."
    }

    return $steamVrPath
}

function Find-BuildOutput {
    param([string]$BuildDir)

    if ($BuildDir) {
        $candidate = Join-Path $BuildDir "bin\drivers\dk2vr"
        if (Test-Path $candidate) {
            return $candidate
        }
        throw "Build output not found at $candidate"
    }

    # Try common build directories
    $candidates = @(
        "build\bin\drivers\dk2vr",
        "build\Release\bin\drivers\dk2vr",
        "build\Debug\bin\drivers\dk2vr",
        "out\build\bin\drivers\dk2vr"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "Build output not found. Please build the project first or specify -BuildDir."
}

Write-Host "=== DK2 SteamVR Driver Installer ===" -ForegroundColor Cyan

# Find SteamVR installation
$steamVrPath = Find-SteamVR
Write-Host "SteamVR found at: $steamVrPath" -ForegroundColor Green

# Find build output
$buildOutput = Find-BuildOutput -BuildDir $BuildDir
Write-Host "Build output found at: $buildOutput" -ForegroundColor Green

# Destination directory
$destDir = Join-Path $steamVrPath "drivers\dk2vr"
$destBinDir = Join-Path $destDir "bin"
$destDriverDir = Join-Path $destBinDir "win64"

# Create destination directories
New-Item -ItemType Directory -Force -Path $destDriverDir | Out-Null

# Copy driver files
Write-Host "Copying driver files..." -ForegroundColor Yellow

# Copy the DLL (manifest layout: bin/win64/driver_dk2vr.dll)
$dllSource = Join-Path $buildOutput "bin\win64\driver_dk2vr.dll"
if (Test-Path $dllSource) {
    Copy-Item $dllSource (Join-Path $destDriverDir "driver_dk2vr.dll") -Force
    Write-Host "  Copied driver_dk2vr.dll" -ForegroundColor Green
} else {
    # Try alternative locations
    $altDll = Get-ChildItem -Path $buildOutput -Filter "driver_dk2vr.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($altDll) {
        Copy-Item $altDll.FullName (Join-Path $destDriverDir "driver_dk2vr.dll") -Force
        Write-Host "  Copied driver_dk2vr.dll from $($altDll.FullName)" -ForegroundColor Green
    } else {
        throw "driver_dk2vr.dll not found in build output."
    }
}

# Copy the manifest
$manifestSource = Join-Path $buildOutput "driver.vrdrivermanifest"
if (Test-Path $manifestSource) {
    Copy-Item $manifestSource (Join-Path $destDir "driver.vrdrivermanifest") -Force
    Write-Host "  Copied driver.vrdrivermanifest" -ForegroundColor Green
} else {
    # Try to find it in the source tree
    $altManifest = Get-ChildItem -Path $PSScriptRoot\..\steamvr_driver -Filter "driver.vrdrivermanifest" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($altManifest) {
        Copy-Item $altManifest.FullName (Join-Path $destDir "driver.vrdrivermanifest") -Force
        Write-Host "  Copied driver.vrdrivermanifest from source" -ForegroundColor Green
    } else {
        throw "driver.vrdrivermanifest not found."
    }
}

# Copy libusb DLL if present
$libusbDll = Get-ChildItem -Path $buildOutput -Filter "libusb*.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($libusbDll) {
    Copy-Item $libusbDll.FullName (Join-Path $destDriverDir $libusbDll.Name) -Force
    Write-Host "  Copied $($libusbDll.Name)" -ForegroundColor Green
}

# Copy hidapi DLL if present
$hidapiDll = Get-ChildItem -Path $buildOutput -Filter "hidapi*.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($hidapiDll) {
    Copy-Item $hidapiDll.FullName (Join-Path $destDriverDir $hidapiDll.Name) -Force
    Write-Host "  Copied $($hidapiDll.Name)" -ForegroundColor Green
}

Write-Host ""
Write-Host "DK2 SteamVR driver installed successfully!" -ForegroundColor Green
Write-Host "Driver location: $destDir" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Restart SteamVR" -ForegroundColor White
Write-Host "  2. Open SteamVR Settings > Developer" -ForegroundColor White
Write-Host "  3. Click 'Enable Driver' for the DK2 VR driver" -ForegroundColor White
Write-Host "  4. The DK2 should appear as a connected HMD" -ForegroundColor White
