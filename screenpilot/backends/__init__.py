from __future__ import annotations

import platform

from screenpilot.models import Display


def discover_displays() -> list[Display]:
    system = platform.system()
    if system == "Linux":
        from .linux import discover_linux_displays

        return discover_linux_displays()
    if system == "Windows":
        from .windows import discover_windows_displays

        return discover_windows_displays()
    raise RuntimeError(f"ScreenPilot does not yet support {system}")

