"""Synthetic shop data for the UI smoke test.

The repository is public, so the run never uses shop data: it builds a throwaway
database with the shop's tables (as tests/support/testschema.h does; the app's
migrateDatabase() adds the verifactu_* columns when MainWindow opens it) and a
settings file that keeps the run offline (no update check, no automatic backup;
Verifactu only towards the fake AEAT of the UI-test build).
"""

import json
import sqlite3
from pathlib import Path

# Same tables and column order as tests/support/testschema.h.
TABLES = [
    "CREATE TABLE ingresos ("
    " n_recibo TEXT, cliente TEXT, fecha_recepcion TEXT, fecha_pago TEXT,"
    " fecha_recogida TEXT, importe TEXT, pagado TEXT, estado TEXT,"
    " cantidad TEXT, prenda TEXT, size TEXT, servicio TEXT,"
    " observaciones TEXT, edit_lock TEXT NOT NULL, hash TEXT)",
    "CREATE TABLE gastos (id INTEGER PRIMARY KEY, n_factura TEXT, servicio TEXT, "
    "descripcion TEXT, empresa TEXT, fecha TEXT, iva TEXT, importe TEXT, "
    "edit_lock TEXT)",
    "CREATE TABLE clientes (nombre TEXT, tel_fijo TEXT, movil TEXT, direccion TEXT)",
    "CREATE TABLE prendas (nombre TEXT, precio_limpieza TEXT, precio_plancha TEXT)",
    "CREATE TABLE proveedores (nombre TEXT, cif TEXT, direccion TEXT, telefono TEXT)",
    "CREATE TABLE servicios (nombre TEXT)",
]

GARMENTS = [("Camisa", "3.5", "2"), ("Pantalon", "5", "3"), ("Falda", "4", "2"), ("Abrigo", "12", "6"),
            ("Jersey", "6", "3"), ("Vestido", "8", "4"), ("Alfombra (m2)", "9.5", "0")]


def create_database(path: Path) -> None:
    path.unlink(missing_ok=True)
    with sqlite3.connect(path) as db:
        for sql in TABLES:
            db.execute(sql)
        db.executemany("INSERT INTO prendas VALUES (?, ?, ?)", GARMENTS)


def write_settings(path: Path, db_path: Path, reports_root: Path, aeat: bool = False,
                   printer: str = "", language: str = "es") -> None:
    """Offline settings. `aeat`: the direct AEAT client with a synthetic issuer (only
    the UI-test build started with --fake-aeat can reach anything with it).
    `printer`: the queue to print to (printing stays off when empty)."""
    verifactu = {"environment": "TESTING", "pending_recovery_enabled": aeat}
    if aeat:
        verifactu.update({"nif": "B00000000", "company_name": "Tintoreria Smoke",
                          "connection": "aeat", "aeat_direct_production": False,
                          "pending_recovery_floor_date": "2026-09-01"})
    settings = {
        "app": {"language": language},
        "business": {"name": "Tintoreria Smoke", "address": "Calle Prueba 1", "city": "00000 Ciudad"},
        "database": {"path": db_path.as_posix()},
        "reports": {"root": reports_root.as_posix()},
        "print": {"enable": "Yes" if printer else "No", "printer_name": printer, "paper_width_mm": 80},
        "updater": {"check_on_startup": False},
        "backup": {"enabled": False},
        "verifactu": verifactu,
    }
    path.write_text(json.dumps(settings, indent=2), encoding="utf-8")


# The smoke tickets (docs/testing/ui_smoke.md, Safety), seeded after the first start
# (the app's migrations add the verifactu_* columns). 1: unpaid, two garments;
# 2: unpaid; 3: paid and left PENDIENTE; 4: paid and sent; 5: paid in Q3, cancelled
# in Q4; one gasto in Q3.
_INGRESOS = ("n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, "
             "cantidad, prenda, size, servicio, observaciones, edit_lock, hash, verifactu_estado, "
             "verifactu_invoice_seq, verifactu_csv, verifactu_invoice_id, fecha_anulacion")
SMOKE_TICKETS = [
    ("1", "SMOKE", "15-09-2026", "", "", "12.00", "NO", "En tienda", "1", "Camisa", "", "Limp.", "", "0",
     "smokehash000001", "SIN COBRAR", 0, None, None, None),
    ("1", "SMOKE", "15-09-2026", "", "", "8.00", "NO", "En tienda", "1", "Pantalon", "", "Plan.", "", "0",
     "smokehash000002", "SIN COBRAR", 0, None, None, None),
    ("2", "SMOKE", "15-09-2026", "", "", "10.00", "NO", "En tienda", "1", "Falda", "", "Limp.", "", "0",
     "smokehash000003", "SIN COBRAR", 0, None, None, None),
    ("3", "SMOKE", "15-09-2026", "15-09-2026", "", "30.00", "SI", "En tienda", "1", "Abrigo", "", "Limp.", "",
     "0", "smokehash000004", "PENDIENTE", 0, None, None, None),
    ("4", "SMOKE", "16-09-2026", "16-09-2026", "", "24.20", "SI", "En tienda", "1", "Jersey", "", "Limp.", "",
     "0", "smokehash000005", "ENVIADA", 0, "SMOKE-CSV-4", "4", None),
    ("5", "SMOKE", "15-09-2026", "15-09-2026", "", "12.10", "SI", "En tienda", "1", "Vestido", "", "Limp.", "",
     "0", "smokehash000006", "ANULADA", 0, "SMOKE-CSV-5", "5", "01-10-2026"),
]
SMOKE_GASTO = (1, "SMOKE-G1", "Suministros", "Smoke", "SMOKE", "20-09-2026", "21", "60.50", "0")


def seed_smoke_tickets(path: Path) -> None:
    with sqlite3.connect(path) as db:
        # The app's Req. 1 guard triggers refuse deleting registered rows; the seed resets them.
        db.execute("DROP TRIGGER IF EXISTS ingresos_registered_no_delete")
        db.execute("DROP TRIGGER IF EXISTS ingresos_accepted_identity")
        db.execute("DELETE FROM ingresos")
        db.execute("DELETE FROM gastos")
        db.executemany(f"INSERT INTO ingresos ({_INGRESOS}) VALUES ({', '.join('?' * 20)})", SMOKE_TICKETS)
        # An accepted invoice keeps the verification URL of AEAT's answer; reprints draw it.
        db.execute("UPDATE ingresos SET verifactu_url_qr = ? WHERE n_recibo = '4'",
                   ("https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?nif=B00000000&numserie=4"
                    "&fecha=16-09-2026&importe=24.20",))
        db.execute("INSERT INTO gastos VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", SMOKE_GASTO)
        db.executemany("INSERT INTO clientes VALUES (?, '', '', ?)",
                       [("SMOKE", "Calle Smoke 1"), ("Señora Álvarez", "")])
        db.execute("INSERT INTO proveedores VALUES ('SMOKE', 'B11111111', 'Calle Proveedor 1', '')")
        db.execute("INSERT INTO servicios VALUES ('Suministros')")


def query(db_path: Path, sql: str, *params):
    with sqlite3.connect(db_path) as db:
        return db.execute(sql, params).fetchall()
