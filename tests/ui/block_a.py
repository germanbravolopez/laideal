"""Block A - start-up on a real schema (docs/testing/ui_smoke.md, What it checks)."""

import re

import seed
from actions import headers, rows, wait_ready
from laideal_ui import find, text

DATA_MIGRATIONS = re.compile(r"migrateDatabase: (re-labelled|moved|relabelled|relinked|rounded|normalised)")
SQL_PROBLEM = re.compile(r"\[(WARN|CRIT)[A-Z]*\].*(migrateDatabase|SQL|sql_lite|no such)")
PENDING_TITLE = "Envíos Verifactu pendientes"


def columns(run):
    return [r[1] for r in run.rows("PRAGMA table_info(ingresos)")]


def prepare(run):
    """The first start (runs the migrations), then the smoke tickets, as in step 1.5-1.6."""
    app = run.start_app()
    wait_ready(app)
    run.stop_app()
    seed.seed_smoke_tickets(run.db)


def pending_dialog(app, timeout: float = 15.0):
    return app.window(title=PENDING_TITLE, timeout=timeout)


def pending_tickets(dialog):
    table = find(dialog, "table")
    return [r.get("Nº recibo", "") for r in rows(table)]


AEAT_URL = re.compile(r'(?:endpoint: |record\(s\) to ")(https?://[^\s",]+)')


def safety_check(run, step_id: str):
    """A3: every AEAT request of the run went to the fake AEAT, never to AEAT or the gateway."""
    with run.step("A", step_id, "Log: every AEAT request went to the fake AEAT, none to AEAT or the gateway"):
        log = run.log()
        fake = "http://" + run.fake_url.split("/")[2] + "/"
        urls = set(AEAT_URL.findall(log))
        run.check(urls, "the log names no AEAT endpoint")
        run.check(all(u.startswith(fake) for u in urls), f"AEAT URLs other than the fake AEAT: {sorted(urls)}")
        for forbidden in ("agenciatributaria.gob.es", "irenesolutions"):
            run.check(forbidden not in log.lower(), f"the log mentions {forbidden}")


def run(run):
    with run.step("A", "A1", "First start on the shop's original schema: migrations, AEAT test environment"):
        app = run.start_app()
        environment = text(find(app.main, "lblAeatEnvironment"))
        run.check("PRUEBAS" in environment, f"status bar: {environment!r}")
        wait_ready(app)
        log = app.log()
        run.check("migrateDatabase: ensuring verifactu_* columns" in log, "no migration line in the log")
        run.check(not SQL_PROBLEM.search(log), f"SQL problem in the log: {SQL_PROBLEM.search(log)}")
        run.check({"verifactu_estado", "verifactu_csv", "fecha_anulacion"} <= set(columns(run)),
                  f"migrations did not add the verifactu columns: {columns(run)}")
        run.stop_app()
        seed.seed_smoke_tickets(run.db)

    with run.step("A", "A2", "Restart after seeding: migrations idempotent, recovery dialog lists ticket 3 only"):
        before = columns(run)
        app = run.start_app()
        dialog = pending_dialog(app)
        run.check(pending_tickets(dialog) == ["3"], f"pending tickets: {pending_tickets(dialog)}")
        run.check(columns(run) == before, "the schema changed on the second start")
        run.check(not DATA_MIGRATIONS.search(app.log()),
                  f"a data migration ran again: {DATA_MIGRATIONS.search(app.log())}")
        run.check(headers(find(dialog, "table"))[:4] == ["Nº recibo", "Fecha", "Cliente", "Importe"],
                  f"columns: {headers(find(dialog, 'table'))}")

    safety_check(run, "A3")
