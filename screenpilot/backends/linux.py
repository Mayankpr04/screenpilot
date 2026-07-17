from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from screenpilot.models import Capability, Control, Display

VCP_CODES = {
    Control.BRIGHTNESS: "10",
    Control.CONTRAST: "12",
    Control.BLACK_LEVEL: "11",
}


def _run(*args: str, timeout: float = 4.0) -> str:
    result = subprocess.run(args, check=True, capture_output=True, text=True, timeout=timeout)
    return result.stdout


@dataclass
class SysfsBacklightDriver:
    path: Path

    def read(self, control: Control) -> Capability | None:
        if control != Control.BRIGHTNESS:
            return None
        maximum = int((self.path / "max_brightness").read_text().strip())
        current = int((self.path / "brightness").read_text().strip())
        percent = round(current * 100 / maximum) if maximum else 0
        return Capability(control, 0, 100, percent)

    def write(self, control: Control, value: int) -> None:
        if control != Control.BRIGHTNESS:
            raise ValueError("Built-in panels only expose brightness")
        maximum = int((self.path / "max_brightness").read_text().strip())
        raw = round(value * maximum / 100)
        try:
            (self.path / "brightness").write_text(str(raw))
        except PermissionError:
            # brightnessctl is commonly configured with the required udev/polkit access.
            _run("brightnessctl", "--device", self.path.name, "set", f"{value}%")


@dataclass
class DdcutilDriver:
    bus: int

    def read(self, control: Control) -> Capability | None:
        code = VCP_CODES[control]
        try:
            output = _run("ddcutil", "--bus", str(self.bus), "getvcp", code, "--terse")
        except (subprocess.SubprocessError, FileNotFoundError):
            return None
        # Example: VCP 10 C 50 100
        match = re.search(r"VCP\s+[0-9A-Fa-f]+\s+C\s+(\d+)\s+(\d+)", output)
        if not match:
            return None
        current, maximum = map(int, match.groups())
        return Capability(control, 0, maximum, current)

    def write(self, control: Control, value: int) -> None:
        _run("ddcutil", "--bus", str(self.bus), "setvcp", VCP_CODES[control], str(value))


def _ddc_displays() -> list[Display]:
    try:
        output = _run("ddcutil", "detect", "--terse", timeout=8.0)
    except (subprocess.SubprocessError, FileNotFoundError):
        return []

    displays: list[Display] = []
    blocks = re.split(r"(?=Display \d+)", output)
    for block in blocks:
        bus_match = re.search(r"I2C bus:\s+/dev/i2c-(\d+)", block)
        if not bus_match:
            continue
        bus = int(bus_match.group(1))
        model = re.search(r"(?:Model|Monitor):\s+(.+)", block)
        name = model.group(1).strip() if model else f"External monitor (I²C {bus})"
        display = Display(f"ddc-{bus}", name, "External / DDC-CI", DdcutilDriver(bus))
        display.refresh()
        displays.append(display)
    return displays


def discover_linux_displays() -> list[Display]:
    displays: list[Display] = []
    backlight_root = Path("/sys/class/backlight")
    if backlight_root.exists():
        for path in sorted(backlight_root.iterdir()):
            display = Display(
                f"backlight-{path.name}", "Built-in display", "Laptop panel", SysfsBacklightDriver(path)
            )
            display.refresh()
            displays.append(display)
    displays.extend(_ddc_displays())
    return displays
