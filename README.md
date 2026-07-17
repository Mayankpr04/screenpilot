# ScreenPilot

[![Latest release](https://img.shields.io/github/v/release/Mayankpr04/screenpilot?display_name=tag)](https://github.com/Mayankpr04/screenpilot/releases/latest)
[![Windows build](https://github.com/Mayankpr04/screenpilot/actions/workflows/native-windows-installer.yml/badge.svg)](https://github.com/Mayankpr04/screenpilot/actions/workflows/native-windows-installer.yml)
[![Linux build](https://github.com/Mayankpr04/screenpilot/actions/workflows/native-linux-deb.yml/badge.svg)](https://github.com/Mayankpr04/screenpilot/actions/workflows/native-linux-deb.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A lightweight native app for controlling the brightness and contrast of every supported display from one place.

ScreenPilot handles laptop panels and external monitors without Electron, .NET, Python, or another bundled runtime. Controls are shown only when the display reports that it supports them.

## Download

**[Download the latest ScreenPilot release](https://github.com/Mayankpr04/screenpilot/releases/latest)**

| Platform | Installer | Notes |
| --- | --- | --- |
| Windows 10/11 (64-bit) | `ScreenPilot-Setup.exe` | Graphical installer; no terminal required |
| Ubuntu/Debian (64-bit) | `screenpilot_0.3.1_amd64.deb` | Install by double-clicking or with APT |

### Windows

1. Download `ScreenPilot-Setup.exe` from the latest release.
2. Run the installer.
3. Open ScreenPilot from the Start menu.

You can right-click ScreenPilot in the Start menu or taskbar and select **Pin to taskbar**.

### Ubuntu and Debian

Download the `.deb` from the latest release and open it with your software installer, or run:

```bash
sudo apt install ./screenpilot_0.3.1_amd64.deb
```

After the first installation, sign out and back in—or restart—so your new display-device permissions take effect.

## Supported controls

| Display type | Brightness | Contrast | Black level |
| --- | :---: | :---: | :---: |
| Built-in laptop panel | Yes | Usually unavailable | Usually unavailable |
| DDC/CI external monitor | Yes | If supported | If supported |

- External monitors use DDC/CI VCP codes `0x10` (brightness), `0x12` (contrast), and `0x11` (black level).
- Laptop panels use Windows display APIs or Linux `/sys/class/backlight`.
- ScreenPilot clamps values to the range reported by each display and debounces slider updates to avoid flooding the DDC bus.

## Before using an external monitor

Open the monitor's physical on-screen menu and enable **DDC/CI**. The name varies by manufacturer and may appear under System, General, or Other Settings.

Some docks, KVM switches, adapters, and capture devices do not forward DDC/CI commands even when video works normally.

## Troubleshooting

### A monitor is missing

- Confirm that DDC/CI is enabled in the monitor menu.
- Connect the monitor directly to the computer to rule out a dock, adapter, or KVM.
- Select **Refresh displays** after changing the connection.
- On Linux, sign out and back in after installing ScreenPilot.

### A setting is rejected

- Confirm that the specific control exists in the monitor's own menu.
- Disable HDR temporarily; the OS or GPU may override some monitor controls.
- Some monitors advertise a DDC/CI control but reject writes to it.
- OLED displays may use manufacturer-specific controls instead of standard black level.

### Linux permission message

The Debian installer adds the invoking desktop user to the `i2c` and `video` groups without making device files world-accessible. If the package was installed from a root-only session, run:

```bash
sudo usermod -aG i2c,video "$USER"
```

Then sign out and back in, or restart.

## Build from source

### Native Windows installer

Requirements:

- Windows 10 or 11
- Visual Studio with the Desktop development with C++ workload
- CMake
- Inno Setup 6

From PowerShell:

```powershell
.\scripts\build-native.ps1
```

The installer is written to `installer-output\ScreenPilot-Setup.exe`.

### Native Linux Debian package

On Ubuntu/Debian:

```bash
sudo apt install build-essential cmake pkg-config libgtk-3-dev
chmod +x scripts/build-linux-deb.sh
./scripts/build-linux-deb.sh
```

The package is written to `installer-output/screenpilot_0.3.1_amd64.deb`.

Both installers can also be built from the repository's [GitHub Actions page](https://github.com/Mayankpr04/screenpilot/actions).

## Limitations

- Hardware DDC/CI support depends on the monitor and the entire connection path.
- HDR can change how brightness and contrast behave.
- Many displays do not implement standard black-level control.
- ScreenPilot changes hardware controls; it intentionally does not simulate dimming with a software gamma overlay.

## License

ScreenPilot is available under the [MIT License](LICENSE).
