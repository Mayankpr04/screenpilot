from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Protocol


class Control(str, Enum):
    BRIGHTNESS = "brightness"
    CONTRAST = "contrast"
    BLACK_LEVEL = "black_level"


@dataclass(frozen=True)
class Capability:
    control: Control
    minimum: int = 0
    maximum: int = 100
    current: int | None = None


class DisplayDriver(Protocol):
    def read(self, control: Control) -> Capability | None: ...
    def write(self, control: Control, value: int) -> None: ...


@dataclass
class Display:
    id: str
    name: str
    kind: str
    driver: DisplayDriver
    capabilities: dict[Control, Capability] = field(default_factory=dict)

    def refresh(self) -> None:
        self.capabilities = {}
        for control in Control:
            try:
                capability = self.driver.read(control)
                if capability is not None:
                    self.capabilities[control] = capability
            except Exception:
                # A monitor may reject individual VCP features. That does not
                # make its remaining controls unusable.
                continue

    def set(self, control: Control, value: int) -> None:
        capability = self.capabilities.get(control)
        if capability is None:
            raise ValueError(f"{self.name} does not support {control.value}")
        value = max(capability.minimum, min(capability.maximum, int(value)))
        self.driver.write(control, value)
        self.capabilities[control] = Capability(
            control, capability.minimum, capability.maximum, value
        )

