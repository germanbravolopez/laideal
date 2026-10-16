"""Drives the real laideal.exe over UI Automation (pywinauto, uia backend).

Qt publishes a widget's objectName as its UIA AutomationId, prefixed by the
object names of its parents ("MainWindow.centralWidget.pb_save"), so a widget is
found by the last segment: the same names the e2e bench uses with findChild.
"""

import subprocess
import time
from pathlib import Path

from pywinauto import Application, Desktop
from pywinauto.timings import TimeoutError as WaitTimeout


def object_name(element) -> str:
    return (element.element_info.automation_id or "").rsplit(".", 1)[-1]


def find(parent, name: str, timeout: float = 10.0):
    """The descendant of `parent` whose objectName is `name`."""
    deadline = time.monotonic() + timeout
    while True:
        for element in parent.descendants():
            if object_name(element) == name:
                return element
        if time.monotonic() > deadline:
            raise LookupError(f"no widget named '{name}' under '{parent.window_text()}'")
        time.sleep(0.25)


def text(element) -> str:
    """What the widget shows: the value of an edit / combo box, the name of a label."""
    try:
        return element.iface_value.CurrentValue
    except Exception:
        return element.window_text()


def dump_tree(window, path: Path) -> None:
    """Every element under `window` with its AutomationId, control type and name."""
    lines = []
    for element in window.descendants():
        info = element.element_info
        lines.append(f"{info.automation_id!r:60} {info.control_type:12} {info.name!r}")
    path.write_text("\n".join(lines), encoding="utf-8")


def screenshot(window, path: Path) -> None:
    window.capture_as_image().save(path)


class LaIdeal:
    """One running laideal.exe started with its own settings file."""

    def __init__(self, exe: Path, settings: Path, log):
        self.process = subprocess.Popen([str(exe), "--settings", str(settings)],
                                        stdout=log, stderr=subprocess.STDOUT)
        self.app = Application(backend="uia").connect(process=self.process.pid, timeout=30)
        self.main = self.app.window(auto_id="MainWindow")
        try:
            self.main.wait("visible ready", timeout=30)
        except WaitTimeout:
            raise RuntimeError("the main window did not appear; the windows open are: "
                               + ", ".join(w.window_text() for w in Desktop(backend="uia").windows()))
        self.main = self.main.wrapper_object()

    def close(self) -> None:
        if self.process.poll() is None:
            try:
                self.main.close()
                self.process.wait(timeout=15)
            except Exception:
                self.process.kill()
