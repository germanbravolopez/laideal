"""Block D - Contabilidad and the quarter lock (docs/testing/ui_smoke.md, What it checks)."""

import time

from pypdf import PdfReader

from actions import (accounts_dialog, accounts_period, accounts_run, add_expense, close_dialog,
                     open_listado, set_check, today)
from laideal_ui import double_click, exists, find, focused_control_type, table_cells, text, wait_for

YEAR = 2026


def pdf_text(path) -> str:
    return "\n".join(page.extract_text() or "" for page in PdfReader(path).pages)


def amount_in(content: str, amount: str) -> bool:
    return amount in content or amount.replace(".", ",") in content


def report(run, name: str):
    path = run.out / "reports" / "Contabilidad" / name
    wait_for(path.exists, 30, f"the report {name}")
    return path


def gastos_edit(run):
    """Double-clicks SMOKE-G1 in Listado de gastos: (an editor opened, the result panel)."""
    gastos = open_listado(run.app, "Gastos")
    cell = next(c for c in table_cells(find(gastos, "table_listado")) if c.window_text() == "SMOKE-G1")
    double_click(cell)
    time.sleep(1)
    editor = focused_control_type() == "Edit"
    message = text(find(gastos, "lblResult"))
    gastos.type_keys("{ESC}")
    gastos.close()
    return editor, message


def run(run):
    run.fresh_start()
    with run.step("D", "D1", "Contabilidad Q3 2026 with the detail: income 66.30 from tickets 3-5, gastos 60.50"):
        dialog = accounts_dialog(run.app)
        accounts_period(dialog, "Trimestral", YEAR, 3)
        set_check(dialog, "chkLock", False)
        set_check(dialog, "chkDetail", True)
        message = accounts_run(run.app, dialog)
        run.check("Informe generado" in message or "pdf" in message.lower(), f"result: {message!r}")
        content = pdf_text(report(run, f"contabilidad_trimestral_{YEAR}_3_detalle.pdf"))
        run.check(amount_in(content, "66.30"), "the Q3 report has no income of 66.30")
        run.check(amount_in(content, "60.50"), "the Q3 report has no gastos of 60.50")
        run.check(exists(run.app.window(title="Generar contabilidad"), "btnGenerate"), "the dialog closed")
        close_dialog(dialog)

    with run.step("D", "D2", "Contabilidad Q4 2026: ticket 5 cancelled in Q4 is taken back (-12.10)"):
        dialog = accounts_dialog(run.app)
        accounts_period(dialog, "Trimestral", YEAR, 4)
        set_check(dialog, "chkDetail", False)
        accounts_run(run.app, dialog)
        content = pdf_text(report(run, f"contabilidad_trimestral_{YEAR}_4.pdf"))
        run.check("Anulaciones / rectificaciones del periodo" in content, "no regularisation line in Q4")
        run.check(amount_in(content, "12.10"), "the Q4 report does not take back 12.10")
        close_dialog(dialog)

    with run.step("D", "D3", "Contabilidad Anual 2026 renders"):
        dialog = accounts_dialog(run.app)
        accounts_period(dialog, "Anual", YEAR)
        accounts_run(run.app, dialog)
        run.check(pdf_text(report(run, f"Anual/reporte_anual_{YEAR}.pdf")), "the annual report is empty")
        close_dialog(dialog)

    with run.step("D", "D4", "The lock box only in Trimestral; Q3 without detail; Comprobar bloqueo: not locked"):
        dialog = accounts_dialog(run.app)
        for kind, enabled in (("Mensual", False), ("Anual", False), ("Trimestral", True)):
            accounts_period(dialog, kind, YEAR, 3 if kind == "Trimestral" else 0)
            run.check(find(dialog, "chkLock").is_enabled() == enabled, f"lock box enabled in {kind}")
        set_check(dialog, "chkLock", False)
        set_check(dialog, "chkDetail", False)
        accounts_run(run.app, dialog)
        report(run, f"contabilidad_trimestral_{YEAR}_3.pdf")
        message = accounts_run(run.app, dialog, "btnCheckLock")
        run.check("no está bloqueado" in message, f"Comprobar bloqueo: {message!r}")
        close_dialog(dialog)
        revert = accounts_dialog(run.app, revert=True)
        run.check(not find(revert, "chkLock").is_enabled(), "the lock box is enabled in Revertir")
        close_dialog(revert)

    with run.step("D", "D5", "Bloquear Q3: gastos of Q3 cannot be edited or added; today's still can"):
        dialog = accounts_dialog(run.app)
        accounts_period(dialog, "Trimestral", YEAR, 3)
        set_check(dialog, "chkLock", True)
        message = accounts_run(run.app, dialog)
        run.check("realizada" in message, f"result: {message!r}")
        message = accounts_run(run.app, dialog, "btnCheckLock")
        run.check("está bloqueado" in message and "no está" not in message, f"Comprobar bloqueo: {message!r}")
        close_dialog(dialog)
        editor, message = gastos_edit(run)
        run.check(not editor and "Edición bloqueada" in message, f"locked expense: editor={editor} {message!r}")
        before = run.scalar("SELECT COUNT(*) FROM gastos")
        message = add_expense(run.app, "SMOKE-G2", "20-09-2026", "SMOKE", "12,10")
        run.check("Trimestre bloqueado" in message, f"expense in the closed quarter: {message!r}")
        run.check(run.scalar("SELECT COUNT(*) FROM gastos") == before, "an expense was added to a closed quarter")
        message = add_expense(run.app, "SMOKE-G3", today(), "SMOKE", "12,10")
        run.check(run.scalar("SELECT COUNT(*) FROM gastos WHERE n_factura='SMOKE-G3'") == 1,
                  f"today's expense was not saved: {message!r}")

    with run.step("D", "D6", "Revertir contabilidad Q3: SMOKE-G1 editable again"):
        dialog = accounts_dialog(run.app, revert=True)
        accounts_period(dialog, "Trimestral", YEAR, 3)
        message = accounts_run(run.app, dialog)
        run.check("revertida" in message, f"result: {message!r}")
        close_dialog(dialog)
        editor, message = gastos_edit(run)
        run.check(editor and "bloqueada" not in message, f"after the revert: editor={editor} {message!r}")
