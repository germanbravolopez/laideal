"""AEAT pre-production run: the direct client's flows against AEAT's test environment.

The automated form of the laideal-prueba-aeat kit, run LOCALLY on a PC with the
issuer's certificate (it never runs in CI: the certificate must not be a secret of
a public repository). It drives laideal.exe like a person and reports what AEAT
answered:

    1  safety: the app is in AEAT's test environment and talks only to prewww1.aeat.es
    2  a paid ticket                                  -> ENVIADA with AEAT's CSV
    3  a ticket paid in two payments (Recogida)       -> two invoices, both ENVIADA
    4  the cancellation of the first payment          -> ANULADA
    5  rectifications R5 of the step-2 ticket, by differences and by substitution
    6  the export of the AEAT records of the day
    7  the log: every request went to prewww1.aeat.es

    python tests/ui/aeat_preproduction.py --exe <laideal.exe> --settings-from <kit settings.json> --out <folder>

--settings-from is the kit's settings file with the issuer's NIF, name and certificate
(store thumbprint, or .pfx file and password). Only those are taken from it; the run
uses a new synthetic database (never shop data) and forces the test environment.
Ticket numbers start at 9DDDhhmm + 1 (DDD: day of the year), eight digits like the kit's
90000001 (Recogida reads a nine-digit search as a phone number), above the shop's and
earlier runs'.

--dry-run checks the script itself without AEAT: the UI-test build (LAIDEAL_UI_TEST)
against the fake AEAT, with a synthetic issuer (needs --tools).
"""

import argparse
import contextlib
import datetime
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import seed
from actions import (cancel_invoice, cobrar, export_aeat_records, open_recogida, recogida_search,
                     rectify_invoice, save_ticket, select_row, today, wait_ready)
from laideal_ui import find, text, wait_for
from smoke import Run, StepFailed

TEST_URL = "https://prewww1.aeat.es/"
AEAT_URL = re.compile(r'(?:endpoint: |record\(s\) to ")(https?://[^\s",]+)')
ISSUER_KEYS = ("nif", "company_name", "aeat_certificate_thumbprint", "aeat_certificate_file",
               "aeat_certificate_password")
AEAT_WAIT = 240   # AEAT asks up to 60 s between submissions; a record may wait for the next turn


def issuer_settings(path: Path) -> dict:
    verifactu = json.loads(path.read_text(encoding="utf-8")).get("verifactu", {})
    issuer = {k: verifactu[k] for k in ISSUER_KEYS if verifactu.get(k)}
    if not issuer.get("nif") or not issuer.get("company_name"):
        raise SystemExit(f"{path}: no issuer NIF / name under 'verifactu'")
    if not issuer.get("aeat_certificate_thumbprint") and not issuer.get("aeat_certificate_file"):
        raise SystemExit(f"{path}: no certificate (aeat_certificate_thumbprint or aeat_certificate_file)")
    return issuer


def write_settings(run, issuer: dict):
    seed.write_settings(run.settings, run.db, run.out / "reports", aeat=True)

    def apply(data):
        data["verifactu"].update(issuer)
        data["verifactu"].update({"connection": "aeat", "aeat_direct_production": False,
                                  "environment": "TESTING"})
    run.update_settings(apply)


def first_number() -> int:
    return int(datetime.datetime.now().strftime("9%j%H%M"))


def seed_numbering(db: Path, base: int):
    """An unpaid placeholder ticket numbered `base`: the next ticket is base + 1."""
    with seed.sqlite3.connect(db) as conn:
        conn.execute("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, "
                     "importe, pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock, "
                     "hash, verifactu_estado, verifactu_invoice_seq) VALUES (?, 'PRUEBA AEAT', ?, '', '', "
                     "'0.00', 'NO', 'En tienda', '1', 'Camisa', '', 'Limp.', 'numeracion', '0', ?, "
                     "'SIN COBRAR', 0)", (str(base), today(), f"numbering{base}"))


def wait_estado(run, where: str, params, wanted: str, what: str):
    def settled():
        row = run.rows(f"SELECT verifactu_estado, verifactu_csv, verifactu_error FROM ingresos WHERE {where}",
                       *params)
        return row[0] if row and row[0][0] in (wanted, "ERROR") else None
    estado, csv, error = wait_for(settled, AEAT_WAIT, what)
    run.check(estado == wanted, f"{what}: {estado} - {error}")
    return csv


def safety(run, expected_prefix: str):
    """Stops everything unless the app is in the test environment and talks to `expected_prefix`."""
    environment = text(find(run.app.main, "lblAeatEnvironment"))
    urls = set(AEAT_URL.findall(run.log()))
    if "PRUEBAS" not in environment or not urls or not all(u.startswith(expected_prefix) for u in urls):
        run.app.process.kill()
        raise SystemExit(f"SAFETY STOP: status bar {environment!r}, AEAT URLs {sorted(urls)} "
                         f"(expected only {expected_prefix})")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--settings-from", type=Path, help="the kit's settings (issuer + certificate)")
    parser.add_argument("--out", type=Path, default=Path("aeat-preproduction-out"))
    parser.add_argument("--dry-run", action="store_true", help="against the fake AEAT (UI-test build)")
    parser.add_argument("--tools", type=Path, help="fake_aeat_server.exe folder (--dry-run)")
    args = parser.parse_args()
    if args.dry_run and not args.tools:
        parser.error("--dry-run needs --tools")
    if not args.dry_run and not args.settings_from:
        parser.error("--settings-from is needed (or --dry-run)")
    args.printer, args.installer = "", None

    if args.out.exists() and any(args.out.iterdir()):
        parser.error(f"{args.out} is not empty: give a new folder (the run creates its own database there)")
    run = Run(args)
    run.title = "AEAT pre-production run" + (" (dry run, fake AEAT)" if args.dry_run else "")
    ok = False
    try:
        issuer = {}
        if args.dry_run:
            run.start_fake_aeat()
        else:
            issuer = issuer_settings(args.settings_from)
        seed.create_database(run.db)
        write_settings(run, issuer)
        expected = "http://" + run.fake_url.split("/")[2] + "/" if args.dry_run else TEST_URL
        base = first_number()
        with contextlib.suppress(StepFailed):
            steps(run, expected, base)
        invoices = run.rows("SELECT n_recibo, prenda, verifactu_invoice_id, verifactu_estado, verifactu_csv, "
                            "verifactu_error FROM ingresos WHERE CAST(n_recibo AS INTEGER) > ? ORDER BY rowid", base)
        run.notes = ["| Ticket | Garment | Invoice | Estado | CSV | AEAT error |", "|---|---|---|---|---|---|"] + [
            "| " + " | ".join(str(v or "").replace("|", "/") for v in row) + " |" for row in invoices]
    finally:
        ok = run.report()
        run.close()
    sys.exit(0 if ok else 1)


def steps(run, expected: str, base: int):
    with run.step("P3", "1", "Safety: AEAT test environment, requests only to " + expected):
        app = run.start_app()
        wait_ready(app, timeout=180)
        safety(run, expected)
        run.stop_app()
        seed_numbering(run.db, base)
        app = run.fresh_start()
        wait_ready(app, timeout=180)
        safety(run, expected)

    with run.step("P3", "2", "A paid ticket is registered (ENVIADA, CSV)"):
        first, message = save_ticket(app, "PRUEBA AEAT", ["Camisa", "Pantalon"], paid=True, timeout=AEAT_WAIT)
        run.check(first == str(base + 1), f"ticket number {first}, expected {base + 1}")
        csv = wait_estado(run, "n_recibo=? LIMIT 1", (first,), "ENVIADA", f"ticket {first}")
        run.check(csv, f"no CSV for {first}")

    with run.step("P3", "3", "A ticket paid in two payments: two invoices, both ENVIADA"):
        second, _ = save_ticket(app, "PRUEBA AEAT", ["Camisa", "Pantalon"])
        recogida = open_recogida(app)
        recogida_search(recogida, second)
        select_row(recogida, "tableView", 0)
        cobrar(app, recogida, keep={"Camisa"}, timeout=AEAT_WAIT)
        wait_estado(run, "n_recibo=? AND prenda='Camisa'", (second,), "ENVIADA", f"{second} first payment")
        recogida_search(recogida, second)
        select_row(recogida, "tableView", 0)
        cobrar(app, recogida, keep={"Pantalon"}, timeout=AEAT_WAIT)
        wait_estado(run, "n_recibo=? AND prenda='Pantalon'", (second,), "ENVIADA", f"{second}-1")
        recogida.close()

    with run.step("P3", "4", "The cancellation of the first payment is registered (ANULADA)"):
        cancel_invoice(app, second, row=0)
        wait_estado(run, "n_recibo=? AND prenda='Camisa'", (second,), "ANULADA", f"cancellation of {second}")

    with run.step("P3", "5", "Rectifications R5 of the first ticket: by differences, then by substitution"):
        for substitution, amount in ((False, "-1,00"), (True, "5,00")):
            before = run.scalar("SELECT COUNT(*) FROM ingresos WHERE verifactu_rectifies_n_recibo=?", first)
            rectify_invoice(app, first, substitution, amount)
            wait_for(lambda: run.scalar("SELECT COUNT(*) FROM ingresos WHERE verifactu_rectifies_n_recibo=?",
                                        first) > before, 30, "the rectifying ticket")
            number = run.scalar("SELECT n_recibo FROM ingresos WHERE verifactu_rectifies_n_recibo=? "
                                "ORDER BY rowid DESC LIMIT 1", first)
            wait_estado(run, "n_recibo=?", (number,), "ENVIADA", f"rectification {number}")
        run.check(run.scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo=? LIMIT 1", first)
                  == "RECTIFICADA", f"ticket {first} is not RECTIFICADA")

    with run.step("P3", "6", "Exportar registros AEAT of the day"):
        message, path = export_aeat_records(app, today(), today())
        run.check("registros exportados" in message, f"export: {message!r}")

    with run.step("P3", "7", "Log: every AEAT request of the run went to " + expected):
        safety(run, expected)
        for forbidden in ("agenciatributaria.gob.es", "irenesolutions"):
            run.check(forbidden not in run.log().lower(), f"the log mentions {forbidden}")


if __name__ == "__main__":
    main()
