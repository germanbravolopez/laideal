"""Block C - printing (docs/testing/ui_smoke.md, What it checks), through a captured printer queue.

Needs the queue of printer.py (`--printer <name>`); without it the block is skipped.
Each print job is decoded back from its ESC/POS bytes, and the QR read from the
rendered image when OpenCV is installed.
"""

import time

from actions import print_ticket, save_ticket
from laideal_ui import find, text, wait_for
from printer import JobCapture, decode
from seed import TICKET_4_QR

TITLES = {"C1": "Unpaid ticket: recibo, client and shop copies",
          "C2": "Paid ticket: factura with QR and leyenda, recibo with IMPORTE PAGADO",
          "C3": "Imprimir → Factura of ticket 4, then Factura completa with a DNI"}


def qr_text(png, scales=(1, 2, 3, 0.5)) -> str:
    """The text of the QR in a rendered ticket, or None without OpenCV.
    scales=(1,) reads it at the printed size only."""
    try:
        import cv2
    except ImportError:
        return None
    image = cv2.imread(str(png))
    # OpenCV's detector misses some symbols at one size and reads them at another. C2's QR
    # carries the day's date, so its symbol changes daily: a few sizes are tried for its
    # content. The printed size itself is proved in C3 on ticket 4's fixed symbol.
    for scale in scales:
        sized = image if scale == 1 else cv2.resize(image, None, fx=scale, fy=scale,
                                                    interpolation=cv2.INTER_NEAREST)
        found, _, _ = cv2.QRCodeDetector().detectAndDecode(sized)
        if found:
            if scale != 1:
                # Said in the run's output: a QR only ever read resized deserves a look.
                print(f"  WARNING: QR of {png.name} not read at the printed size, read at x{scale}")
            return found
    return ""


def check_printed_qr(run, before: int) -> None:
    """Ticket 4's QR in the jobs printed since `before`: read at the printed size, with its stored text."""
    codes = [qr_text(job.with_suffix(".png"), scales=(1,)) for job in run.printer.jobs[before:]]
    if None not in codes:
        run.check(TICKET_4_QR in codes, f"ticket 4's QR not read at the printed size: {codes}")


def new_jobs(run, before: int, minimum: int, timeout: float = 30.0) -> str:
    """Waits for at least `minimum` new jobs (and for the queue to go quiet) and returns
    their decoded text, one job after the other."""
    capture = run.printer
    wait_for(lambda: capture.count() >= before + minimum, timeout, f"{minimum} print job(s)")
    settled = capture.count()
    time.sleep(3)
    while capture.count() != settled:
        settled = capture.count()
        time.sleep(3)
    return "\n".join(decode(run.args.tools, job) for job in capture.jobs[before:])


def run(run):
    if not run.args.printer:
        for step_id, title in TITLES.items():
            run.skip("C", step_id, title, "no --printer queue (see printer.py)")
        return
    run.printer = JobCapture(run.out / "print-jobs", run.args.printer_port)

    def enable(data):
        data.setdefault("print", {}).update({"enable": "Yes", "printer_name": run.args.printer,
                                             "use_status_api": "No"})
    run.update_settings(enable)
    app = run.fresh_start()

    with run.step("C", "C1", TITLES["C1"]):
        before = run.printer.count()
        ticket, message = save_ticket(app, "Cliente Impresion", ["Camisa"])
        run.check(f"Ticket {ticket} guardado" in message, f"result: {message!r}")
        printed = new_jobs(run, before, 1)
        for expected in ("Recibo", "(Copia para el cliente)", "(Copia para el establecimiento)", ticket):
            run.check(expected in printed, f"{expected!r} not on the printed recibo")
        run.check("[QR" not in printed, "an unpaid recibo has a QR")
        run.check(int(text(find(app.main, "le_nr_ticket"))) == int(ticket) + 1, "the number did not advance")

    with run.step("C", "C2", TITLES["C2"]):
        before = run.printer.count()
        ticket, message = save_ticket(app, "Cliente Impresion", ["Pantalon"], paid=True)
        run.check(f"Ticket {ticket} guardado" in message, f"result: {message!r}")
        printed = new_jobs(run, before, 1)
        for expected in ("FACTURA SIMPLIFICADA", "[QR", "Factura verificable en la sede", "IMPORTE PAGADO"):
            run.check(expected in printed, f"{expected!r} not printed for a paid ticket")
        pngs = [job.with_suffix(".png") for job in run.printer.jobs[before:]]
        codes = [qr_text(p) for p in pngs]
        if None in codes:
            print("  (OpenCV not installed: the QR's content is not read)")
        else:
            print(f"  QR read: {[c for c in codes if c]}")
            run.check(any("prewww2.aeat.es" in c and f"numserie={ticket}" in c for c in codes),
                      f"no QR leading to AEAT's test check page for {ticket}: {codes}")
        wait_for(lambda: run.scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo=?", ticket)
                 == "ENVIADA", 20, f"ticket {ticket} to be ENVIADA")

    with run.step("C", "C3", TITLES["C3"]):
        before = run.printer.count()
        message = print_ticket(app, "Factura", "4")
        printed = new_jobs(run, before, 1)
        run.check("FACTURA SIMPLIFICADA" in printed and "[QR" in printed, "ticket 4's factura misprinted")
        run.check("24.20" in printed or "24,20" in printed, "ticket 4's total not on its factura")
        check_printed_qr(run, before)
        before = run.printer.count()
        print_ticket(app, "Factura completa", "4", dni="12345678Z", address="Calle Smoke 1")
        printed = new_jobs(run, before, 1)
        run.check("12345678Z" in printed, "the typed DNI is not on the factura completa")
        run.check("Calle Smoke 1" in printed, "the address is not on the factura completa")
        check_printed_qr(run, before)
