"""P0 spike of the automated UI smoke test.

Starts the installed laideal.exe on a synthetic database, finds its widgets over
UI Automation by object name, reads the result panel, saves one unpaid ticket the
way an operator does (mouse + keyboard), and checks the row it stored.

    python tests/ui/smoke_p0.py --exe "<install dir>/laideal.exe" --out ui-smoke-out

Everything the run produces (screenshots, the control tree, the app's output and
the database) lands in --out, kept by the workflow as an artifact.
"""

import argparse
import sys
import time
from pathlib import Path

import seed
from laideal_ui import LaIdeal, dump_tree, find, screenshot, text

CLIENT = "Cliente Smoke"
GARMENT = "Camisa"
TABLE_COLUMNS = 6
GARMENT_COLUMN = 1


def wait_for(condition, timeout: float = 10.0, what: str = "condition"):
    deadline = time.monotonic() + timeout
    while not condition():
        if time.monotonic() > deadline:
            raise AssertionError(f"timed out waiting for {what}")
        time.sleep(0.25)


def table_cell(table, row: int, column: int):
    # The cell combo boxes are not in the UIA tree; their cell (a DataItem) is, row by row.
    cells = [c for c in table.descendants() if c.element_info.control_type == "DataItem"]
    return cells[row * TABLE_COLUMNS + column]


def run(exe: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    db_path, settings = out / "shop.db", out / "settings.json"
    seed.create_database(db_path)
    seed.write_settings(settings, db_path, out / "reports")

    with open(out / "app-output.txt", "w", encoding="utf-8") as log:
        app = LaIdeal(exe, settings, log)
        try:
            main = app.main
            dump_tree(main, out / "control-tree.txt")
            screenshot(main, out / "01-started.png")

            result = find(main, "lblResult")
            print("lblResult at start:", text(result))
            ticket = text(find(main, "le_nr_ticket"))
            print("ticket number:", ticket)

            client = find(main, "cb_client")
            client.click_input()
            client.type_keys(CLIENT, with_spaces=True)

            table_cell(find(main, "table_ticket"), 0, GARMENT_COLUMN).click_input()
            main.type_keys(GARMENT + "{TAB}")
            wait_for(lambda: text(find(main, "le_cost_total")) == "3.50", what="the garment's price")
            screenshot(main, out / "02-filled.png")

            find(main, "pb_save").click_input()
            wait_for(lambda: "guardado" in text(result), what="the save message")
            message = text(result)
            print("lblResult after save:", message)
            screenshot(main, out / "03-saved.png")

            rows = seed.query(db_path, "SELECT cliente, prenda, importe, pagado, verifactu_estado "
                                       "FROM ingresos WHERE n_recibo = ?", ticket)
            print("stored:", rows)
            assert rows == [(CLIENT, GARMENT, "3.50", "NO", "SIN COBRAR")], rows
            assert f"Ticket {ticket} guardado" in message, message
            assert int(text(find(main, "le_nr_ticket"))) == int(ticket) + 1, "the form was not reset"
        except Exception:
            screenshot(app.main, out / "99-failure.png")
            raise
        finally:
            app.close()
    print("P0 smoke: OK")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("ui-smoke-out"))
    args = parser.parse_args()
    try:
        run(args.exe, args.out)
    except Exception as error:
        print(f"P0 smoke: FAILED - {error}", file=sys.stderr)
        sys.exit(1)
