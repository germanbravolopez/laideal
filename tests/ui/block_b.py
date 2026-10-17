"""Block B - ticket screens and wiring (docs/testing/smoke_test.md)."""

import time

from actions import (close_dialog, cobrar, headers, visual_headers, open_dialog, open_listado, open_recogida, recogida_search,
                     rows, search_ticket, select_row, today, total_label, void_garments)
from block_a import pending_dialog, pending_tickets
from laideal_ui import click, click_in_cell, exists, find, find_text, table_cells, text, type_into, wait_for

ACTIONS_COLUMN = 4
POSTPONE, MARK_ERROR = 5 / 6, 1 / 2


def no_message_box(run):
    boxes = [w for w in run.app.windows() if exists(w, "qt_msgbox_label")]
    run.check(not boxes, "a pop-up opened: " + "; ".join(
        find(b, "qt_msgbox_label").window_text() for b in boxes))


def pending_action(run, fraction: float):
    """Presses a row button of the recovery dialog; with its only row settled it closes."""
    dialog = pending_dialog(run.app)
    click_in_cell(table_cells(find(dialog, "table"))[ACTIONS_COLUMN], fraction)
    wait_for(lambda: not any(w.window_text() == dialog.window_text() for w in run.app.windows()),
             10, "the recovery dialog to close after its last row")


def ticket_rows(run, ticket: str):
    return run.rows("SELECT prenda, pagado, estado, verifactu_estado, fecha_pago, fecha_anulacion "
                    "FROM ingresos WHERE n_recibo = ? ORDER BY rowid", ticket)


def run(run):
    with run.step("B", "B1", "Recovery dialog: Posponer writes nothing; it is offered again at the next start"):
        run.restart()
        pending_action(run, POSTPONE)
        run.check(run.scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='3'") == "PENDIENTE",
                  "Posponer changed ticket 3")
        run.restart()
        run.check(pending_tickets(pending_dialog(run.app)) == ["3"], "the dialog did not come back")

    with run.step("B", "B2", "Recovery dialog: Marcar como error; Recogida's Verifactu window offers Reintentar"):
        pending_action(run, MARK_ERROR)
        run.check(run.scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='3'") == "ERROR",
                  "ticket 3 is not ERROR (upper-case)")
        recogida = open_recogida(run.app)
        recogida_search(recogida, "3")
        select_row(recogida, "tableView", 0)
        click(recogida, "pb_verifactu")
        dialog = run.app.window("verifactuDialog")
        run.check(exists(dialog, "btnRetry"), "no 'Reintentar envío a AEAT' for an ERROR ticket")
        dialog.close()
        recogida.close()

    with run.step("B", "B3", "Recogida: ticket 1 found by number, Importe total is what is owed"):
        recogida = open_recogida(run.app)
        message = recogida_search(recogida, "1")
        table = find(recogida, "tableView")
        run.check(len(rows(table)) == 2, f"ticket 1 rows: {rows(table)}")
        total = total_label(recogida)
        run.check("20,00" in total or "20.00" in total, f"total label: {total!r}")
        for name in ("le_search", "tableView", "pb_pay_all", "pb_add", "pb_void", "pb_verifactu", "lblResult"):
            run.check(exists(recogida, name), f"Recogida has no {name}")
        no_message_box(run)

    with run.step("B", "B4", "Recogida: Añadir prendas… adds an unpaid garment to ticket 1"):
        select_row(recogida, "tableView", 0)
        click(recogida, "pb_add")
        add = run.app.window(title="Añadir nuevas prendas")
        wait_for(lambda: text(find(add, "leNRecibo")) == "1", 5, "the window to open on ticket 1")
        combo = find(add, "cbPrenda")
        combo.click_input()
        combo.type_keys("^aFalda{TAB}")
        type_into(add, "leCantidad", "1")
        wait_for(lambda: text(find(add, "leImporte")), 5, "the garment's price")
        click(add, "btnSave")
        wait_for(lambda: "añadid" in text(find(add, "lblResult")).lower(), 10, "the garment to be added")
        click(add, "btnClose")
        result = find(recogida, "lblResult")
        wait_for(lambda: "1 prenda(s) añadidas" in text(result), 10, "Recogida to report the added garment")
        garments = ticket_rows(run, "1")
        run.check(len(garments) == 3, f"ticket 1 has {len(garments)} garments")
        run.check(garments[2][0] == "Falda" and garments[2][3] == "SIN COBRAR", f"added: {garments[2]}")
        no_message_box(run)

    with run.step("B", "B5", "Recogida: Cobrar… charges only the Camisa; AEAT (fake) accepts it"):
        recogida_search(recogida, "1")
        select_row(recogida, "tableView", 0)
        cobrar(run.app, recogida, keep={"Camisa"})
        wait_for(lambda: run.scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='1' "
                                    "AND prenda='Camisa'") == "ENVIADA", 20, "the Camisa to be ENVIADA")
        for garment, paid, _, estado, paid_on, _ in ticket_rows(run, "1"):
            if garment == "Camisa":
                run.check(paid == "SI" and paid_on == today(), f"Camisa: {paid} {paid_on}")
            else:
                run.check(paid == "NO" and estado == "SIN COBRAR", f"{garment}: {paid} {estado}")
        run.check(run.scalar("SELECT verifactu_csv FROM ingresos WHERE n_recibo='1' AND prenda='Camisa'"),
                  "no CSV stored for the Camisa")
        recogida.close()

    with run.step("B", "B6", "Añadir nuevas prendas refuses a ticket with a paid garment"):
        add = open_dialog(run.app, "Herramientas", "Añadir nuevas prendas", "Añadir nuevas prendas")
        type_into(add, "leNRecibo", "1")
        click(add, "btnSearch")
        wait_for(lambda: "ya tiene prendas pagadas" in text(find(add, "lblResult")), 10, "the refusal")
        run.check(len(ticket_rows(run, "1")) == 3, "a garment was added to ticket 1")
        click(add, "btnClose")
        no_message_box(run)

    with run.step("B", "B6b", "Recogida: Anular prendas… voids ticket 2's garment"):
        recogida = open_recogida(run.app)
        recogida_search(recogida, "2")
        select_row(recogida, "tableView", 0)
        void_garments(run.app, recogida)
        garment = ticket_rows(run, "2")[0]
        run.check(garment[2] == "Anulado" and garment[1] == "NO", f"ticket 2: {garment}")
        run.check(garment[5] == today(), f"fecha_anulacion {garment[5]!r}, expected {today()}")
        run.check("anulad" in text(find(recogida, "lblResult")).lower(),
                  f"Recogida says {text(find(recogida, 'lblResult'))!r}")
        find_text(run.app.main, "Herramientas", "MenuItem").click_input()
        time.sleep(0.5)
        items = [e.window_text() for e in run.app.main.descendants(control_type="MenuItem")]
        run.app.main.type_keys("{ESC}")
        run.check("Anular prendas" not in items, "Herramientas still lists Anular prendas")

    with run.step("B", "B7", "Recogida and Listado de ingresos show Recepción, Pago, Recogida, Anulación together"):
        titles = visual_headers(find(recogida, "tableView"))
        run.check(_adjacent(titles), f"Recogida columns: {titles}")
        recogida.close()
        ingresos = open_listado(run.app, "Ingresos")
        titles = visual_headers(find(ingresos, "table_listado"))
        run.check(_adjacent(titles), f"Listado de ingresos columns: {titles}")

    with run.step("B", "B8", "Listados: estado SIN COBRAR shown, accent-insensitive search, PDF export"):
        shown = {c.window_text() for c in table_cells(find(ingresos, "table_listado"))}
        run.check("SIN COBRAR" in shown, "no 'SIN COBRAR' estado shown in the ingresos listado")
        ingresos.close()
        prendas = open_listado(run.app, "Listado de prendas")
        click(prendas, "btnPdf")
        wait_for(lambda: "generado en PDF" in text(find(prendas, "lblResult")), 30, "the PDF")
        run.check(list((run.out / "reports" / "Listados" / "Prendas").glob("*.pdf")),
                  "no PDF in reports/Listados/Prendas")
        prendas.close()
        clientes = open_listado(run.app, "Listado de clientes")
        type_into(clientes, "filter_widget", "senora alvarez")
        time.sleep(1)
        shown = [c for c in table_cells(find(clientes, "table_listado")) if "lvarez" in c.window_text()]
        run.check(shown, "accent-insensitive search did not find 'Señora Álvarez'")
        clientes.close()
        no_message_box(run)

    with run.step("B", "B9", "Anular factura and Rectificar open ticket 4's invoice (nothing sent)"):
        sent_before = run.scalar("SELECT COUNT(*) FROM ingresos WHERE verifactu_estado='ENVIADA'")
        for item, title in (("Anular factura Verifactu...", "Anular Factura"),
                            ("Rectificar factura Verifactu...", "rectificativa")):
            dialog = open_dialog(run.app, "Herramientas", item, title)
            search_ticket(dialog, "4")
            if item.startswith("Anular"):
                cells = [c.window_text() for c in table_cells(find(dialog, "table"))]
                run.check("4" in cells, f"Anular factura does not list invoice 4: {cells}")
            else:
                run.check(find(dialog, "btnRectify").is_enabled(), "Rectificar did not load ticket 4")
            close_dialog(dialog)
        run.check(run.scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='4'") == "ENVIADA",
                  "ticket 4 changed")
        run.check(run.scalar("SELECT COUNT(*) FROM ingresos WHERE verifactu_estado='ENVIADA'") == sent_before,
                  "something was sent")


def _adjacent(titles) -> bool:
    """Recepción · Pago · Recogida · Anulación side by side (the ingresos listado shows the
    column names: fecha_recepcion …)."""
    for wanted in (["Recepción", "Pago", "Recogida", "Anulación"],
                   ["fecha_recepcion", "fecha_pago", "fecha_recogida", "fecha_anulacion"]):
        if any(titles[i:i + 4] == wanted for i in range(len(titles))):
            return True
    return False
