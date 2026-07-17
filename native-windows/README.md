# ScreenPilot native Windows application

This is the dependency-free Win32 implementation of ScreenPilot. It uses only libraries included with Windows:

- `Dxva2.dll` for external-monitor DDC/CI controls
- WMI COM interfaces in `ROOT\\WMI` for built-in-panel brightness
- Common Controls and the Win32 window manager for the interface
- Shell notification APIs for the tray icon

## Build requirements

- Windows 10 or 11, x64
- Visual Studio 2022 Build Tools with **Desktop development with C++**
- CMake (included with that Visual Studio workload)
- Inno Setup 6

From the repository root:

```powershell
.\scripts\build-native.ps1
```

The distributable installer is `installer-output\\ScreenPilot-Setup.exe`. The installed application consists of one native executable; no Python, Qt, .NET, or Visual C++ redistributable is required because it uses Windows system libraries and links the C++ runtime statically.

## Behavior

- Starting normally opens the control window.
- Closing the window keeps ScreenPilot in the notification area.
- Starting with `--tray` launches without opening the window.
- A named mutex prevents duplicate instances.
- DDC writes occur when the user releases a slider, avoiding excessive monitor-bus traffic.
