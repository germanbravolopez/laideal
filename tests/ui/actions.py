"""Operator flows on the running app, shared by the smoke blocks and the AEAT
pre-production run: each does what a person does (menus, typing, buttons) and
returns what the screen then says. Checks belong to the callers."""

import datetime
import time

from laideal_ui import (click, click_check, click_in_cell, double_click, escape_keys, exists, find,
                        find_text, is_checked, set_date, table_cells, text, type_into, wait_for)

MAIN_TABLE_COLUMNS = 6     # Cant. | Prenda | Tamaño | Serv. | Observaciones | Importe
GARMENT_COLUMN = 1


def today() -> str:
    return datetime.date.today().strftime("%d-%m-%Y")


def wait_ready(app, timeout: float = 90.0):
    """Waits until MainWindow is not checking the AEAT chain any more (a save waits for it)."""
    result = find(app.main, "lblResult")
    wait_for(lambda: "comprobando" not in text(result).lower(), timeout, "the AEAT chain check")


# --- MainWindow -------------------------------------------------------------------------

def save_ticket(app, client: str, garments, paid: bool = False, timeout: float = 30.0):
    """Types a ticket in MainWindow and saves it. Returns (ticket number, result panel)."""
    main = app.main
    ticket = text(find(main, "le_nr_ticket"))
    client_box = find(main, "cb_client")
    client_box.click_input()
    client_box.type_keys("^a" + escape_keys(client), with_spaces=True)
    table = find(main, "table_ticket")
    for row, garment in enumerate(garments):
        table_cells(table)[row * MAIN_TABLE_COLUMNS + GARMENT_COLUMN].click_input()
        main.type_keys(escape_keys(garment) + "{TAB}", with_spaces=True)
    wait_for(lambda: text(find(main, "le_cost_total")), 5, "the ticket total")
    if paid:
        payment = find(main, "pb_payment")
        if not is_checked(payment):
            payment.click_input()
    click(main, "pb_save")
    result = find(main, "lblResult")
    wait_for(lambda: f"Ticket {ticket} guardado" in text(result) or "No se" in text(result),
             timeout, f"ticket {ticket} to be saved")
    return ticket, text(result)


# --- Recogida -------------------------------------------------------------------------------

def open_recogida(app):
    app.menu("Tickets", "Recogida de prendas")
    return app.window("RecogPrendas")


def recogida_search(window, value: str):
    type_into(window, "le_search", value, enter=True)
    result = find(window, "lblResult")
    wait_for(lambda: "encontrad" in text(result).lower() or "ningún" in text(result).lower()
             or "no se" in text(result).lower(), 10, f"the search for {value}")
    return text(result)


def headers(table):
    """The visible column titles. A hidden column keeps its header (zero width) but has
    no cells; row numbers of a vertical header are left out."""
    return [h.window_text() for h in table.descendants(control_type="Header")
            if not h.window_text().isdigit() and h.rectangle().width() > 0]


def visual_headers(table):
    """The visible column titles in the order they are shown (a moved column keeps its
    model position in the UIA tree)."""
    shown = [h for h in table.descendants(control_type="Header")
             if h.window_text() and not h.window_text().isdigit() and h.rectangle().width() > 0]
    return [h.window_text() for h in sorted(shown, key=lambda h: h.rectangle().left)]


def rows(table):
    """The table's rows as {column title: text}."""
    titles = headers(table)
    cells = [c.window_text() for c in table_cells(table)]
    return [dict(zip(titles, cells[i:i + len(titles)])) for i in range(0, len(cells), len(titles))]


def select_row(window, table_name: str, row: int):
    table = find(window, table_name)
    cells = table_cells(table)
    cells[row * len(headers(table))].click_input()


def total_label(window) -> str:
    """Recogida's 'Importe total: …' label (unnamed: found by its text)."""
    return wait_for(lambda: next((e.window_text() for e in window.descendants(control_type="Text")
                                  if e.window_text().startswith("Importe total")), None),
                    5, "the total label")


def pay_dialog(app):
    return app.window(title="Cobrar", timeout=10)


def cobrar(app, window, keep, timeout: float = 30.0):
    """Recogida → Cobrar…: unticks every garment not in `keep`, presses Cobrar and waits for
    the dialog to close. Returns Recogida's result panel."""
    click(window, "pb_pay_all")
    dialog = wait_for(lambda: next((w for w in app.windows() if exists(w, "btnCobrar")), None),
                      10, "the Cobrar window")
    table = next(t for t in dialog.descendants(control_type="Table"))
    titles = headers(table)
    for i, row in enumerate(rows(table)):     # every garment starts ticked
        garment = next((v for k, v in row.items() if k.startswith("Prenda")), "")
        if garment not in keep:
            click_check(table_cells(table)[i * len(titles)])
    click(dialog, "btnCobrar")
    wait_for(lambda: not any(exists(w, "btnCobrar") for w in app.windows()), timeout,
             "the Cobrar window to close")
    return text(find(window, "lblResult"))


def void_garments(app, window):
    """Recogida → Anular prendas…: ticks the first garment and voids it."""
    click(window, "pb_void")
    dialog = wait_for(lambda: next((w for w in app.windows() if exists(w, "btnVoid")), None),
                      10, "the Anular prendas window")
    table = find(dialog, "table")
    click_check(table_cells(table)[0])          # every garment starts unticked
    click(dialog, "btnVoid")
    message = app.answer("Sí") if _has_message_box(app) else ""
    result = text(find(dialog, "lblResult"))
    find_text(dialog, "Cerrar", "Button").click_input()
    return message, result


def _has_message_box(app, wait: float = 3.0) -> bool:
    deadline = time.monotonic() + wait
    while time.monotonic() < deadline:
        if any(exists(w, "qt_msgbox_label") for w in app.windows()):
            return True
        time.sleep(0.25)
    return False


# --- Ticket dialogs --------------------------------------------------------------------------

def open_dialog(app, top: str, item: str, title: str):
    app.menu(top, item)
    return app.window(title=title)


def close_dialog(dialog):
    find_text(dialog, "Cerrar", "Button").click_input()


def search_ticket(dialog, ticket: str):
    type_into(dialog, "leTicketNum", ticket)
    find_text(dialog, "Buscar", "Button").click_input()
    time.sleep(1)
    return text(find(dialog, "lblResult"))


def cancel_invoice(app, ticket: str, row: int = 0):
    """Anular factura Verifactu: searches the ticket and presses the Anular button of the
    invoice in `row`. Returns the dialog's result panel; the caller waits on the database."""
    dialog = open_dialog(app, "Facturas", "Anular factura Verifactu...", "Anular Factura")
    search_ticket(dialog, ticket)
    table = find(dialog, "table")
    columns = len(headers(table))
    click_in_cell(table_cells(table)[row * columns + columns - 1], 0.5)
    if _has_message_box(app):
        app.answer("Sí")
    time.sleep(2)
    message = text(find(dialog, "lblResult"))
    close_dialog(dialog)
    return message


def rectify_invoice(app, ticket: str, substitution: bool, amount: str, date: str = None):
    """Rectificar factura Verifactu as R5 (a simplified invoice), by substitution or by
    differences. Returns the dialog's result panel; the caller waits on the database."""
    dialog = open_dialog(app, "Facturas", "Rectificar factura Verifactu...", "rectificativa")
    search_ticket(dialog, ticket)
    combo = find(dialog, "cbInvoiceType")
    combo.click_input()
    combo.type_keys("{END}{ENTER}")
    wait_for(lambda: text(find(dialog, "cbInvoiceType")).startswith("R5"), 5, "R5")
    click(dialog, "rbSubstitution" if substitution else "rbDifferences")
    if date:
        set_date(dialog, "deRectifyDate", date)
    amount_box = find(dialog, "sbAmount")
    amount_box.click_input()
    amount_box.type_keys("^a" + amount + "{TAB}")
    click(dialog, "btnRectify")
    if _has_message_box(app):
        app.answer("Sí")
    time.sleep(2)
    message = text(find(dialog, "lblResult"))
    close_dialog(dialog)
    return message


def export_aeat_records(app, date_from: str, date_to: str):
    """Facturas → Exportar registros AEAT (XML) for a range. Returns (result panel, file)."""
    dialog = open_dialog(app, "Facturas", "Exportar registros AEAT (XML)...", "Exportar")
    set_date(dialog, "deFrom", date_from)
    set_date(dialog, "deTo", date_to)
    path = text(find(dialog, "leFile"))
    click(dialog, "btnExport")
    result = find(dialog, "lblResult")
    wait_for(lambda: "exportados" in text(result) or "No se" in text(result), 30, "the export")
    message = text(result)
    close_dialog(dialog)
    return message, path


def print_dialog(app, kind: str):
    """Tickets → Imprimir → Recibo / Factura / Factura completa."""
    find_text(app.main, "Tickets", "MenuItem").click_input()
    find_text(app.main, "Imprimir", "MenuItem").click_input()
    find_text(app.main, kind, "MenuItem").click_input()
    return wait_for(lambda: next((w for w in app.windows() if exists(w, "le_n_ticket")), None),
                    10, f"the Imprimir {kind} window")


def print_ticket(app, kind: str, ticket: str, dni: str = "", address: str = ""):
    dialog = print_dialog(app, kind)
    if dni:
        type_into(dialog, "leDni", dni)
    if address:
        type_into(dialog, "leAddress", address)
    type_into(dialog, "le_n_ticket", ticket, enter=True)
    result = find(dialog, "lblResult")
    wait_for(lambda: text(result) and "Introduzca" not in text(result), 30, f"printing {ticket}")
    message = text(result)
    close_dialog(dialog)
    return message


# --- Contabilidad, gastos -----------------------------------------------------------------

def set_spin(dialog, name: str, value: int):
    box = find(dialog, name)
    box.click_input()
    box.type_keys("^a" + str(value) + "{TAB}")


def select_combo(dialog, name: str, item: str):
    combo = find(dialog, name)
    if text(combo) == item:
        return
    combo.click_input()
    combo.type_keys(escape_keys(item) + "{ENTER}", with_spaces=True)
    wait_for(lambda: text(find(dialog, name)) == item, 5, f"{name} = {item}")


def accounts_dialog(app, revert: bool = False):
    title = "Revertir contabilidad" if revert else "Generar contabilidad"
    return open_dialog(app, "Contabilidad", title + ("..." if revert else ""), title)


def accounts_period(dialog, kind: str, year: int, period: int = 0):
    select_combo(dialog, "cbConfig", kind)
    set_spin(dialog, "sbYear", year)
    if period:
        set_spin(dialog, "sbPeriod", period)


def set_check(dialog, name: str, wanted: bool):
    box = find(dialog, name)
    if box.is_enabled() and is_checked(box) != wanted:
        box.click_input()


def accounts_run(app, dialog, button: str = "btnGenerate", timeout: float = 60.0) -> str:
    """Presses Generar / Comprobar bloqueo and returns the result panel once it changes."""
    result = find(dialog, "lblResult")
    before = text(result)
    click(dialog, button)
    if _has_message_box(app, 2):
        app.answer("Sí")
    try:
        wait_for(lambda: text(result) != before, timeout, f"{button} to finish")
    except AssertionError:
        pass    # the same answer as before (e.g. Comprobar bloqueo twice)
    return text(result)


def add_expense(app, number: str, date: str, supplier: str, amount: str):
    dialog = open_dialog(app, "Contabilidad", "Añadir factura de gastos", "facturas de gastos")
    type_into(dialog, "leFra", number)
    set_date(dialog, "deFecha", date)
    for name, value in (("cbEmpresa", supplier), ("cbServicio", "Suministros")):
        box = find(dialog, name)
        box.click_input()
        box.type_keys("^a" + escape_keys(value), with_spaces=True)
    type_into(dialog, "leDescripcion", "Smoke")
    type_into(dialog, "leImporte", amount)
    click(dialog, "btnSave")
    time.sleep(1)
    message = text(find(dialog, "lblResult"))
    close_dialog(dialog)
    return message


def open_listado(app, item: str):
    app.menu("Ver", item)
    return app.window(item)


def edit_attempt(window, row: int, column: int):
    """Double-clicks a listado cell; returns (an editor opened, the result panel)."""
    table = find(window, "table_listado")
    columns = len(headers(table))
    double_click(table_cells(table)[row * columns + column])
    time.sleep(1)
    editor = any(e.element_info.control_type == "Edit" for e in table.descendants())
    message = text(find(window, "lblResult"))
    window.type_keys("{ESC}")
    return editor, message
