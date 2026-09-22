# Third party notices

This project is distributed under the MIT license. It builds against the
following third party components, each under its own license. None of them are
redistributed in source form here; the build fetches them.

## OpenVR SDK
- Valve Corporation — https://github.com/ValveSoftware/openvr
- BSD 3-Clause License
- Only the headers are used. The driver DLL is loaded by `vrserver.exe` and
  does not link `openvr_api`.

## libusb
- https://github.com/libusb/libusb
- GNU Lesser General Public License v2.1 or later
- Used to reach the DK2 tracker through the WinUSB or libusb-win32 driver.
  Linked statically. Under LGPL-2.1 section 6, the object files needed to
  relink the driver against a modified libusb are reproducible from this
  repository: `scripts\build.ps1` fetches the pinned version and rebuilds.

## hidapi
- https://github.com/libusb/hidapi
- Tri-licensed: GPL v3, BSD 3-Clause, or the original HIDAPI license
- Used here under the BSD 3-Clause option, as the fallback transport for DK2
  units bound to the Windows HID driver.

## Oculus Rift DK2 protocol
The tracker's report layouts and feature-report handshake follow the
descriptions in the OpenHMD project (https://github.com/OpenHMD/OpenHMD),
Boost Software License 1.0. No OpenHMD code is compiled into this project.

"Oculus" and "Rift" are trademarks of their respective owners. This project is
not affiliated with, endorsed by, or supported by Meta, Oculus VR or Valve.
