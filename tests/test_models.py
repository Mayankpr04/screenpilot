from screenpilot.models import Capability, Control, Display


class FakeDriver:
    def __init__(self):
        self.values = {Control.BRIGHTNESS: 50, Control.CONTRAST: 70}

    def read(self, control):
        value = self.values.get(control)
        return None if value is None else Capability(control, 0, 100, value)

    def write(self, control, value):
        self.values[control] = value


def test_refresh_detects_only_supported_controls():
    display = Display("one", "Test", "Fake", FakeDriver())
    display.refresh()
    assert set(display.capabilities) == {Control.BRIGHTNESS, Control.CONTRAST}


def test_set_clamps_to_capability_range():
    driver = FakeDriver()
    display = Display("one", "Test", "Fake", driver)
    display.refresh()
    display.set(Control.BRIGHTNESS, 150)
    assert driver.values[Control.BRIGHTNESS] == 100

