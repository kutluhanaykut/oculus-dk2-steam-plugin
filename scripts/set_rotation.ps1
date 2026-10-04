# Sets the DK2 driver's image rotation in steamvr.vrsettings.
#
# Finding the right rotation is a four-option guess that only the headset can
# settle, so this makes the loop quick: run it, start SteamVR, look, repeat.
#
#   powershell -ExecutionPolicy Bypass -File scripts\set_rotation.ps1 0
#   powershell -ExecutionPolicy Bypass -File scripts\set_rotation.ps1 180
#   powershell -ExecutionPolicy Bypass -File scripts\set_rotation.ps1 auto
#
# Other knobs, for when the rotation is right but the eyes are not:
#   -SwapEyes          swap left and right
#   -EyeSplit 0|1      force side-by-side (0) or over-under (1)
#   -MirrorView        show the headset view on the monitor, useful for
#                      watching what the driver is doing without the headset on

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('auto', '0', '90', '180', '270')]
    [string]$Rotation = 'auto',

    [ValidateSet('auto', '0', '1')]
    [string]$EyeSplit,

    [switch]$SwapEyes,
    [switch]$NoSwapEyes,
    [switch]$MirrorView,
    [switch]$NoMirrorView,
    [switch]$Show,

    # Lens tuning. Turn these down when the view bulges like a fisheye, up
    # when straight lines still bow inward at the edges.
    [ValidateSet('on', 'off')]
    [string]$Distortion,
    [double]$K1,
    [double]$K2,
    # Overall field of view. Below 1 narrows it, above 1 widens it. Lowering
    # it also makes everything look bigger, which helps with small text.
    [double]$FovScale,

    # Per-eye render scale. Above 1 costs GPU time but sharpens the dashboard
    # text, which the DK2's 960x1080 per eye renders quite coarsely.
    [double]$Supersample,

    # How the compositor presents to the panel. 'direct' uses DXGI, 'window'
    # presents into a window on the extended desktop. Try 'window' when the
    # headset fills with solid red and only a SteamVR restart clears it.
    [ValidateSet('direct', 'window')]
    [string]$Present,

    # SteamVR's "display locked mode". The compositor normally wants exclusive
    # fullscreen on the headset's display once a game starts; on an extended
    # DK2 that transition can fail, leaving the panel solid red while the
    # dashboard alone still works. Allowing display-locked mode lets it carry
    # on without that transition.
    [ValidateSet('on', 'off')]
    [string]$DisplayLocked,

    # Motion smoothing / async reprojection. It kicks in when a game misses
    # frames, and on an extended display it needs the same display lock that
    # fails here. A light game that holds full rate never triggers it, which
    # is why one game can be fine while another turns the panel red.
    [ValidateSet('on', 'off')]
    [string]$MotionSmoothing,

    # SteamVR Home. Its tutorial expects real controllers and gets in the way
    # of a gaze pointer, so turning it off drops you straight into the void
    # with the dashboard.
    [ValidateSet('on', 'off')]
    [string]$HomeApp
)

$ErrorActionPreference = 'Stop'

# SteamVR rewrites this file when it exits, so edits made while it is running
# are silently thrown away.
$running = Get-Process -Name vrserver, vrmonitor, vrcompositor, vrdashboard -ErrorAction SilentlyContinue
if ($running) {
    throw "SteamVR is running ($(($running | Select-Object -ExpandProperty ProcessName | Sort-Object -Unique) -join ', ')). Close it first, or the change will be overwritten."
}

function Find-SteamPath {
    foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        $entry = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($entry -and $entry.SteamPath) { return $entry.SteamPath }
        if ($entry -and $entry.InstallPath) { return $entry.InstallPath }
    }
    return 'C:\Program Files (x86)\Steam'
}

$config = Join-Path (Find-SteamPath) 'config\steamvr.vrsettings'
if (-not (Test-Path $config)) {
    throw "steamvr.vrsettings not found at $config. Run SteamVR once first."
}

$json = Get-Content $config -Raw | ConvertFrom-Json

if ($Show) {
    if ($json.PSObject.Properties.Name -contains 'dk2vr_display') {
        'Current dk2vr_display settings:'
        $json.dk2vr_display | Format-List | Out-String | Write-Host
    } else {
        'No dk2vr_display section yet; the driver is using its manifest defaults.'
    }
    return
}

Copy-Item $config "$config.dk2backup" -Force

if ($json.PSObject.Properties.Name -notcontains 'dk2vr_display') {
    $json | Add-Member -MemberType NoteProperty -Name 'dk2vr_display' -Value ([pscustomobject]@{})
}
$section = $json.dk2vr_display

function Set-Value {
    param($Target, [string]$Name, $Value)
    $Target | Add-Member -MemberType NoteProperty -Name $Name -Value $Value -Force
}

# -1 is the driver's "work it out yourself" sentinel.
$rotationValue = if ($Rotation -eq 'auto') { -1 } else { [int]$Rotation }
Set-Value $section 'image_rotation' $rotationValue

if ($PSBoundParameters.ContainsKey('EyeSplit')) {
    $splitValue = if ($EyeSplit -eq 'auto') { -1 } else { [int]$EyeSplit }
    Set-Value $section 'eye_split' $splitValue
}
if ($SwapEyes) { Set-Value $section 'swap_eyes' $true }
if ($NoSwapEyes) { Set-Value $section 'swap_eyes' $false }

if ($Distortion) { Set-Value $section 'distortion_enabled' ($Distortion -eq 'on') }
if ($PSBoundParameters.ContainsKey('K1')) { Set-Value $section 'distortion_k1' $K1 }
if ($PSBoundParameters.ContainsKey('K2')) { Set-Value $section 'distortion_k2' $K2 }
if ($PSBoundParameters.ContainsKey('FovScale')) { Set-Value $section 'fov_scale' $FovScale }
if ($PSBoundParameters.ContainsKey('Supersample')) { Set-Value $section 'supersample' $Supersample }
if ($Present) { Set-Value $section 'display_is_real' ($Present -eq 'direct') }

if ($MirrorView -or $NoMirrorView -or $HomeApp -or $DisplayLocked -or $MotionSmoothing) {
    if ($json.PSObject.Properties.Name -notcontains 'steamvr') {
        $json | Add-Member -MemberType NoteProperty -Name 'steamvr' -Value ([pscustomobject]@{})
    }
    if ($MirrorView -or $NoMirrorView) {
        Set-Value $json.steamvr 'showMirrorView' ([bool]$MirrorView)
    }
    if ($HomeApp) {
        Set-Value $json.steamvr 'enableHomeApp' ($HomeApp -eq 'on')
    }
    if ($DisplayLocked) {
        Set-Value $json.steamvr 'allowDisplayLockedMode' ($DisplayLocked -eq 'on')
    }
    if ($MotionSmoothing) {
        Set-Value $json.steamvr 'motionSmoothing' ($MotionSmoothing -eq 'on')
    }
}

($json | ConvertTo-Json -Depth 20) | Set-Content $config -Encoding utf8

Write-Host "Updated $config" -ForegroundColor Green
Write-Host "  image_rotation = $rotationValue$(if ($rotationValue -eq -1) { '  (auto)' })"
if ($PSBoundParameters.ContainsKey('EyeSplit')) { Write-Host "  eye_split      = $($section.eye_split)" }
if ($SwapEyes -or $NoSwapEyes) { Write-Host "  swap_eyes      = $($section.swap_eyes)" }
if ($Distortion) { Write-Host "  distortion     = $($section.distortion_enabled)" }
if ($PSBoundParameters.ContainsKey('K1')) { Write-Host "  distortion_k1  = $($section.distortion_k1)" }
if ($PSBoundParameters.ContainsKey('K2')) { Write-Host "  distortion_k2  = $($section.distortion_k2)" }
if ($PSBoundParameters.ContainsKey('FovScale')) { Write-Host "  fov_scale      = $($section.fov_scale)" }
if ($PSBoundParameters.ContainsKey('Supersample')) { Write-Host "  supersample    = $($section.supersample)" }
if ($Present) { Write-Host "  display_is_real = $($section.display_is_real)  ($Present)" }
if ($MirrorView -or $NoMirrorView) { Write-Host "  showMirrorView = $($json.steamvr.showMirrorView)" }
if ($HomeApp) { Write-Host "  enableHomeApp  = $($json.steamvr.enableHomeApp)" }
if ($DisplayLocked) { Write-Host "  allowDisplayLockedMode = $($json.steamvr.allowDisplayLockedMode)" }
if ($MotionSmoothing) { Write-Host "  motionSmoothing = $($json.steamvr.motionSmoothing)" }
Write-Host ''
Write-Host 'Now start SteamVR and look through the headset.' -ForegroundColor Cyan
Write-Host "Backup of the previous file: $config.dk2backup"
