import sys
import types

from screenpilot.backends.windows import WindowsLaptopDriver
from screenpilot.models import Control


class FakeSbc(types.ModuleType):
    def __init__(self):
        super().__init__("screen_brightness_control")
        self.calls = []

    def get_brightness(self, **kwargs):
        self.calls.append(("get", kwargs))
        return [42]

    def set_brightness(self, value, **kwargs):
        self.calls.append(("set", value, kwargs))


def test_laptop_driver_pins_operations_to_wmi(monkeypatch):
    fake = FakeSbc()
    monkeypatch.setitem(sys.modules, "screen_brightness_control", fake)
    driver = WindowsLaptopDriver(0)
    assert driver.read(Control.BRIGHTNESS).current == 42
    driver.write(Control.BRIGHTNESS, 55)
    assert fake.calls == [
        ("get", {"display": 0, "method": "wmi"}),
        ("set", 55, {"display": 0, "method": "wmi"}),
    ]
