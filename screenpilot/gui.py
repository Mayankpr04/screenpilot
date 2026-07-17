from __future__ import annotations

from functools import partial

from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import (
    QApplication, QFrame, QHBoxLayout, QLabel, QMainWindow, QMessageBox,
    QMenu, QPushButton, QScrollArea, QSlider, QSystemTrayIcon, QVBoxLayout, QWidget,
)
from PySide6.QtGui import QAction, QCloseEvent, QIcon
from pathlib import Path
import sys

from screenpilot.backends import discover_displays
from screenpilot.models import Control, Display

LABELS = {
    Control.BRIGHTNESS: "Brightness",
    Control.CONTRAST: "Contrast",
    Control.BLACK_LEVEL: "Black level",
}


class DisplayCard(QFrame):
    def __init__(self, display: Display) -> None:
        super().__init__()
        self.display = display
        self.setObjectName("card")
        layout = QVBoxLayout(self)
        title = QLabel(display.name)
        title.setObjectName("title")
        layout.addWidget(title)
        layout.addWidget(QLabel(display.kind))

        if not display.capabilities:
            layout.addWidget(QLabel("No adjustable hardware controls were detected."))
        for control, capability in display.capabilities.items():
            row = QHBoxLayout()
            label = QLabel(LABELS[control])
            label.setMinimumWidth(90)
            slider = QSlider(Qt.Orientation.Horizontal)
            slider.setRange(capability.minimum, capability.maximum)
            slider.setValue(capability.current or 0)
            value_label = QLabel(str(capability.current or 0))
            value_label.setMinimumWidth(34)
            slider.valueChanged.connect(lambda value, out=value_label: out.setText(str(value)))
            # Debounce writes: dragging should not flood a slow DDC bus.
            timer = QTimer(slider)
            timer.setSingleShot(True)
            timer.setInterval(120)
            slider.valueChanged.connect(lambda _value, t=timer: t.start())
            timer.timeout.connect(partial(self._write, control, slider))
            row.addWidget(label)
            row.addWidget(slider, 1)
            row.addWidget(value_label)
            layout.addLayout(row)

    def _write(self, control: Control, slider: QSlider) -> None:
        try:
            self.display.set(control, slider.value())
        except Exception as exc:
            QMessageBox.warning(self, "Display control failed", str(exc))


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("ScreenPilot")
        self.allow_close = False
        self.resize(680, 500)
        self.container = QVBoxLayout()
        body = QWidget()
        body.setLayout(self.container)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(body)
        wrapper = QWidget()
        root = QVBoxLayout(wrapper)
        header = QHBoxLayout()
        heading = QLabel("ScreenPilot")
        heading.setObjectName("heading")
        refresh = QPushButton("Refresh displays")
        refresh.clicked.connect(self.reload)
        header.addWidget(heading)
        header.addStretch()
        header.addWidget(refresh)
        root.addLayout(header)
        root.addWidget(scroll)
        self.setCentralWidget(wrapper)
        self.reload()

    def closeEvent(self, event: QCloseEvent) -> None:
        if self.allow_close:
            event.accept()
        else:
            event.ignore()
            self.hide()

    def exit_application(self) -> None:
        self.allow_close = True
        QApplication.quit()

    def reload(self) -> None:
        while self.container.count():
            item = self.container.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        try:
            displays = discover_displays()
        except Exception as exc:
            displays = []
            self.container.addWidget(QLabel(f"Display discovery failed: {exc}"))
        if not displays:
            self.container.addWidget(QLabel(
                "No controllable displays found. On Linux, install ddcutil and enable I²C access."
            ))
        for display in displays:
            self.container.addWidget(DisplayCard(display))
        self.container.addStretch()


def run() -> int:
    app = QApplication.instance() or QApplication([])
    app.setQuitOnLastWindowClosed(False)
    app.setApplicationName("ScreenPilot")
    app.setOrganizationName("ScreenPilot")
    if sys.platform == "win32":
        # Gives the taskbar/Start menu a stable identity instead of grouping
        # the packaged app as python.exe.
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(
            "MayankPratap.ScreenPilot.1"
        )

    bundle_root = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))
    icon = QIcon(str(bundle_root / "assets" / "screenpilot.svg"))
    app.setWindowIcon(icon)
    app.setStyleSheet("""
        QWidget { font-size: 14px; }
        QMainWindow { background: #121722; }
        QLabel { color: #dce4f2; }
        #heading { font-size: 26px; font-weight: 700; padding: 8px; }
        #title { font-size: 18px; font-weight: 600; }
        #card { background: #202838; border: 1px solid #344057; border-radius: 10px; margin: 5px; padding: 12px; }
        QPushButton { color: white; background: #3662e3; border: 0; border-radius: 6px; padding: 8px 12px; }
    """)
    window = MainWindow()
    tray = QSystemTrayIcon(icon, app)
    tray.setToolTip("ScreenPilot")
    menu = QMenu()
    show_action = QAction("Open ScreenPilot", menu)
    show_action.triggered.connect(lambda: (window.showNormal(), window.raise_(), window.activateWindow()))
    refresh_action = QAction("Refresh displays", menu)
    refresh_action.triggered.connect(window.reload)
    exit_action = QAction("Exit", menu)
    exit_action.triggered.connect(window.exit_application)
    menu.addAction(show_action)
    menu.addAction(refresh_action)
    menu.addSeparator()
    menu.addAction(exit_action)
    tray.setContextMenu(menu)
    tray.activated.connect(
        lambda reason: show_action.trigger()
        if reason in (QSystemTrayIcon.ActivationReason.Trigger, QSystemTrayIcon.ActivationReason.DoubleClick)
        else None
    )
    tray.show()
    if "--tray" not in sys.argv:
        window.show()
    return app.exec()
