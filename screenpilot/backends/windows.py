from __future__ import annotations

from dataclasses import dataclass

from screenpilot.models import Capability, Control, Display

VCP_CODES = {
    Control.BRIGHTNESS: 0x10,
    Control.CONTRAST: 0x12,
    Control.BLACK_LEVEL: 0x11,
}


@dataclass
class WindowsLaptopDriver:
    index: int

    def read(self, control: Control) -> Capability | None:
        if control != Control.BRIGHTNESS:
            return None
        import screen_brightness_control as sbc

        # The index is relative to the WMI result set, not sbc's combined
        # WMI + DDC result set. Omitting method can redirect index 0 to the
        # first external monitor and create two cards for the same screen.
        values = sbc.get_brightness(display=self.index, method="wmi")
        value = values[0] if isinstance(values, list) else values
        return Capability(control, 0, 100, int(value))

    def write(self, control: Control, value: int) -> None:
        if control != Control.BRIGHTNESS:
            raise ValueError("Built-in panels only expose brightness")
        import screen_brightness_control as sbc

        sbc.set_brightness(value, display=self.index, method="wmi")


@dataclass
class MonitorControlDriver:
    monitor: object

    def read(self, control: Control) -> Capability | None:
        try:
            with self.monitor:
                current, maximum = self.monitor.vcp.get_vcp_feature(VCP_CODES[control])
        except Exception:
            return None
        return Capability(control, 0, int(maximum), int(current))

    def write(self, control: Control, value: int) -> None:
        with self.monitor:
            self.monitor.vcp.set_vcp_feature(VCP_CODES[control], int(value))


def discover_windows_displays() -> list[Display]:
    displays: list[Display] = []

    # WMI-based enumeration catches internal laptop panels.
    try:
        import screen_brightness_control as sbc

        for index, info in enumerate(sbc.list_monitors_info(method="wmi", allow_duplicates=False)):
            name = info.get("name") or info.get("model") or f"Built-in display {index + 1}"
            display = Display(f"wmi-{index}", name, "Laptop panel", WindowsLaptopDriver(index))
            display.refresh()
            displays.append(display)
    except Exception:
        pass

    # Physical external monitors are addressed directly via DDC/CI.
    try:
        from monitorcontrol import get_monitors

        for index, monitor in enumerate(get_monitors()):
            driver = MonitorControlDriver(monitor)
            display = Display(f"ddc-{index}", f"External monitor {index + 1}", "External / DDC-CI", driver)
            display.refresh()
            # Windows can expose an internal eDP panel through the physical
            # monitor API even though it accepts no DDC/CI features. It is
            # already represented by the WMI entry above.
            if display.capabilities:
                displays.append(display)
    except Exception:
        pass
    return displays
