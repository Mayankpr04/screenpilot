# ScreenPilot

Current version: **0.1.2**

## Native Windows edition

The native Windows 0.2.0 edition is in `native-windows`. It has no Python, Qt, .NET, or Electron runtime and builds to one small `ScreenPilot.exe`. Run `scripts\\build-native.ps1` on Windows or trigger the included **Build native Windows installer** GitHub workflow. The resulting end-user file is `ScreenPilot-Setup.exe`.

ScreenPilot is one control panel for all displays attached to a Windows or Linux computer. It discovers each display and exposes brightness, contrast, and black level only when the display reports that the control is supported.

## How it works

- **External monitors:** DDC/CI over the display cable. Brightness uses VCP `0x10`, contrast `0x12`, and black level `0x11`.
- **Laptop panels:** Windows WMI or Linux `/sys/class/backlight`. Laptop panels normally expose brightness only.
- **Unsupported monitors:** Some monitors disable DDC/CI by default, some docks/adapters do not forward it, and many displays do not implement black level.

## Install on Windows

1. In each external monitor's physical menu, enable **DDC/CI**.
2. Install Python 3.10 or newer.
3. From PowerShell in this folder:

```powershell
py -m venv .venv
.venv\Scripts\Activate.ps1
pip install .
screenpilot
```

To build a portable application folder:

```powershell
.\scripts\build.ps1
```

Install [Inno Setup 6](https://jrsoftware.org/isinfo.php), then run the build command. The finished graphical installer is created at `installer-output\ScreenPilot-Setup.exe`. End users only need this file and do not need Python or a terminal.

Once installed, ScreenPilot appears in the Start menu and notification area. Closing its window keeps it in the tray; use the tray menu's **Exit** command to stop it. Right-click the Start menu entry or running taskbar icon and choose **Pin to taskbar**.

## Install on Ubuntu/Debian Linux

Enable DDC access and install the system helper:

```bash
sudo apt install ddcutil i2c-tools python3-venv
sudo modprobe i2c-dev
sudo usermod -aG i2c "$USER"
```

Log out and back in after adding the group, then:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install .
screenpilot
```

If laptop brightness is permission-denied, install `brightnessctl`; most distributions configure its access automatically.

To build a standalone binary folder:

```bash
chmod +x scripts/build.sh
./scripts/build.sh
```

## CLI

```bash
screenpilot-cli --list
screenpilot-cli --display ddc-6 --brightness 60 --contrast 70
```

## Development

```bash
pip install -e '.[dev]'
pytest
ruff check .
```

## Practical limitations

- HDMI/DisplayPort/USB-C usually support DDC/CI, but capture cards, some KVMs, DisplayLink adapters, and inexpensive docks may block it.
- HDR mode may cause the OS/GPU to override or reinterpret brightness and contrast.
- OLED televisions and monitors often expose manufacturer-specific controls instead of standard black level VCP `0x11`.
- Software gamma overlays can simulate dimming, but they do not change the panel backlight and are intentionally not used here.

## Safety

ScreenPilot probes only the three standard VCP controls above. It clamps every write to the range reported by the display and debounces GUI slider updates to avoid flooding the DDC bus.
