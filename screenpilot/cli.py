from __future__ import annotations

import argparse

from screenpilot.backends import discover_displays
from screenpilot.models import Control


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="screenpilot-cli")
    p.add_argument("--list", action="store_true", help="list displays and capabilities")
    p.add_argument("--display", help="display id from --list")
    p.add_argument("--brightness", type=int)
    p.add_argument("--contrast", type=int)
    p.add_argument("--black-level", type=int)
    return p


def main() -> None:
    args = parser().parse_args()
    displays = discover_displays()
    if args.list or not args.display:
        for display in displays:
            values = ", ".join(
                f"{control.value}={cap.current}/{cap.maximum}"
                for control, cap in display.capabilities.items()
            ) or "no controls"
            print(f"{display.id:18} {display.name} [{values}]")
        return
    display = next((d for d in displays if d.id == args.display), None)
    if display is None:
        raise SystemExit(f"Unknown display id: {args.display}")
    changes = {
        Control.BRIGHTNESS: args.brightness,
        Control.CONTRAST: args.contrast,
        Control.BLACK_LEVEL: args.black_level,
    }
    for control, value in changes.items():
        if value is not None:
            display.set(control, value)


if __name__ == "__main__":
    main()

