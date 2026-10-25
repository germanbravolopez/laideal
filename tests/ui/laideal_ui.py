"""Drives the real laideal.exe over UI Automation (pywinauto, uia backend).

Qt publishes a widget's objectName as its UIA AutomationId (menu actions are
prefixed by their window: "MainWindow.actionGastos"), so a widget is found by the
last segment: the same names the e2e bench uses with findChild. Unnamed widgets
(menu items, message-box buttons) are found by their visible text.

A dialog is a `Window` element nested in the window that opened it, so lookups
stop at nested windows: find(main, "lblResult") is MainWindow's panel even while
a dialog with its own lblResult is open. Widgets placed in table cells
(setCellWidget) are not in the tree at all; they are clicked inside their cell.
"""

import subprocess
import time
from pathlib import Path

from pywinauto import Application, Desktop, mouse
from pywinauto.uia_defines import IUIA
from pywinauto.uia_element_info import UIAElementInfo

LOG = Path.home() / ".laideal.log"


def object_name(element) -> str:
    return (element.element_info.automation_id or "").rsplit(".", 1)[-1]


def wait_for(condition, timeout: float = 10.0, what: str = "condition"):
    """Polls `condition` until it returns something truthy, and returns that."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            value = condition()
        except Exception:
            value = None
        if value:
            return value
        if time.monotonic() > deadline:
            raise AssertionError(f"timed out waiting for {what}")
        time.sleep(0.25)


def _in_scope(element, parent) -> bool:
    """`element` belongs to `parent` itself, not to a dialog nested in it."""
    if parent.element_info.control_type != "Window":
        return True
    node = element.element_info.parent
    while node is not None and node.control_type != "Window":
        node = node.parent
    return node == parent.element_info


def _matches(parent, test, control_type=None):
    elements = parent.descendants(control_type=control_type) if control_type else parent.descendants()
    return [e for e in elements if test(e) and _in_scope(e, parent)]


def find(parent, name: str, timeout: float = 10.0):
    """The widget of `parent` whose objectName is `name`."""
    def lookup():
        return next(iter(_matches(parent, lambda e: object_name(e) == name)), None)
    return wait_for(lookup, timeout, f"widget '{name}'")


def find_text(parent, label: str, control_type: str = None, timeout: float = 10.0):
    """The element of `parent` that shows `label` (a button, a menu item, a tab)."""
    def lookup():
        return next(iter(_matches(parent, lambda e: e.window_text() == label, control_type)), None)
    return wait_for(lookup, timeout, f"'{label}'")


def exists(parent, name: str) -> bool:
    return bool(_matches(parent, lambda e: object_name(e) == name))


def text(element) -> str:
    """What the widget shows: the value of an edit / combo box, the name of a label."""
    try:
        return element.iface_value.CurrentValue
    except Exception:
        return element.window_text()


def click(parent, name: str):
    find(parent, name).click_input()


def type_into(parent, name: str, value: str, enter: bool = False):
    """Clicks the field, replaces its content by keyboard, optionally presses Enter."""
    field = find(parent, name)
    field.click_input()
    field.type_keys("^a{BACKSPACE}" + escape_keys(value) + ("{ENTER}" if enter else ""), with_spaces=True)


def escape_keys(value: str) -> str:
    return "".join("{" + c + "}" if c in "+^%~(){}[]" else c for c in value)


def set_date(parent, name: str, date: str):
    """Types a dd-MM-yyyy date into a QDateEdit section by section (typing the whole date
    over it does not work): each section is selected with Shift+Right and typed over."""
    box = find(parent, name)
    rect = box.rectangle()
    mouse.click(coords=(rect.left + 8, rect.mid_point().y))
    day, month, year = date.split("-")
    box.type_keys("{HOME}{RIGHT 6}+{RIGHT 4}" + year)
    box.type_keys("{HOME}{RIGHT 3}+{RIGHT 2}" + month)
    box.type_keys("{HOME}+{RIGHT 2}" + day)
    wait_for(lambda: text(find(parent, name)) == date, 5, f"{name} = {date}")


def is_checked(element) -> bool:
    return element.iface_toggle.CurrentToggleState == 1


def table_cells(table):
    """The cells of a table view, row by row (cell widgets are not in the UIA tree)."""
    return [c for c in table.descendants() if c.element_info.control_type == "DataItem"]


def table_rows(table, columns: int):
    cells = [c.window_text() for c in table_cells(table)]
    return [cells[i:i + columns] for i in range(0, len(cells), columns)]


def click_in_cell(cell, fraction: float):
    """Clicks a widget placed in a table cell, at `fraction` of the cell's width
    (the Reintentar | Error | Posponer buttons of one cell: 1/6, 1/2, 5/6)."""
    rect = cell.rectangle()
    mouse.click(coords=(rect.left + int(rect.width() * fraction), rect.mid_point().y))


def click_check(cell):
    """Clicks the check box of a checkable table cell (no UIA toggle pattern on Qt's cells)."""
    rect = cell.rectangle()
    mouse.click(coords=(rect.left + 11, rect.mid_point().y))


def focused_control_type() -> str:
    """The control type of the element with the keyboard focus (a table's cell editor is
    not in the table's tree, but it has the focus)."""
    return UIAElementInfo(IUIA().iuia.GetFocusedElement()).control_type


def double_click(element):
    rect = element.rectangle()
    mouse.double_click(coords=(rect.mid_point().x, rect.mid_point().y))


def dump_tree(window, path: Path) -> None:
    """Every element under `window` with its AutomationId, control type and name."""
    lines = [f"{window.element_info.automation_id!r} {window.window_text()!r}"]
    for element in window.descendants():
        info = element.element_info
        lines.append(f"{info.automation_id!r:60} {info.control_type:12} {info.name!r}")
    path.write_text("\n".join(lines), encoding="utf-8")


def screenshot(window, path: Path) -> None:
    try:
        window.capture_as_image().save(path)
    except Exception as error:   # a locked screen; the run's checks do not depend on it
        print(f"  (no screenshot {path.name}: {error})")


class LaIdeal:
    """One running laideal.exe started with its own settings file."""

    def __init__(self, exe: Path, settings: Path, output, fake_aeat: str = None):
        args = [str(exe), "--settings", str(settings)]
        if fake_aeat:
            args += ["--fake-aeat", fake_aeat]
        self.log_start = LOG.stat().st_size if LOG.exists() else 0
        self.process = subprocess.Popen(args, stdout=output, stderr=subprocess.STDOUT)
        self.app = Application(backend="uia").connect(process=self.process.pid, timeout=30)
        self.main = self.window("MainWindow", timeout=30)

    def windows(self):
        """The app's windows: the top-level ones and the dialogs nested in them."""
        found = []
        for top in Desktop(backend="uia").windows(process=self.process.pid):
            found.append(top)
            found += top.descendants(control_type="Window")
        return found

    def window(self, name: str = None, title: str = None, timeout: float = 10.0):
        """A top-level window of the app, by objectName or by a part of its title."""
        def lookup():
            for w in self.windows():
                if (name and object_name(w) == name) or (title and title in w.window_text()):
                    return w
            return None
        return wait_for(lookup, timeout, f"window {name or title!r}")

    def message_box(self, timeout: float = 10.0):
        """The open QMessageBox (an unnamed dialog with a 'qt_msgbox_label')."""
        def lookup():
            return next((w for w in self.windows() if object_name(w) != "MainWindow"
                         and exists(w, "qt_msgbox_label")), None)
        return wait_for(lookup, timeout, "a message box")

    def answer(self, button: str, timeout: float = 10.0) -> str:
        """Presses `button` on the open message box and returns its text."""
        box = self.message_box(timeout)
        message = find(box, "qt_msgbox_label").window_text()
        find_text(box, button, "Button").click_input()
        return message

    def menu(self, top: str, item: str):
        find_text(self.main, top, "MenuItem").click_input()
        find_text(self.main, item, "MenuItem").click_input()

    def log(self) -> str:
        """What the app wrote to ~/.laideal.log since it was started."""
        if not LOG.exists():
            return ""
        with open(LOG, "rb") as f:
            f.seek(self.log_start)
            return f.read().decode("utf-8", errors="replace")

    def close(self) -> None:
        if self.process.poll() is None:
            try:
                self.main.close()
                self.process.wait(timeout=15)
            except Exception:
                self.process.kill()
