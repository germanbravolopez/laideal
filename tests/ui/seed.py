"""Synthetic shop data for the UI smoke test.

The repository is public, so the run never uses shop data: it builds a throwaway
database with the shop's tables (as tests/support/testschema.h does; the app's
migrateDatabase() adds the verifactu_* columns when MainWindow opens it) and a
settings file that keeps the run offline (no Verifactu credentials, no printing,
no update check, no backup).
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
    " observaciones TEXT, edit_lock TEXT DEFAULT '0', hash TEXT)",
    "CREATE TABLE gastos (id INTEGER PRIMARY KEY, n_factura TEXT, servicio TEXT, "
    "descripcion TEXT, empresa TEXT, fecha TEXT, iva TEXT, importe TEXT, "
    "edit_lock TEXT DEFAULT '0')",
    "CREATE TABLE clientes (nombre TEXT, tel_fijo TEXT, movil TEXT, direccion TEXT)",
    "CREATE TABLE prendas (nombre TEXT, precio_limpieza TEXT, precio_plancha TEXT)",
    "CREATE TABLE proveedores (nombre TEXT, cif TEXT, direccion TEXT, telefono TEXT)",
    "CREATE TABLE servicios (nombre TEXT)",
]

GARMENTS = [("Camisa", "3.5", "2"), ("Pantalon", "5", "3"), ("Alfombra (m2)", "9.5", "0")]


def create_database(path: Path) -> None:
    path.unlink(missing_ok=True)
    with sqlite3.connect(path) as db:
        for sql in TABLES:
            db.execute(sql)
        db.executemany("INSERT INTO prendas VALUES (?, ?, ?)", GARMENTS)


def write_settings(path: Path, db_path: Path, reports_root: Path) -> None:
    settings = {
        "app": {"language": "es"},
        "database": {"path": db_path.as_posix()},
        "reports": {"root": reports_root.as_posix()},
        "print": {"enable": "No"},
        "updater": {"check_on_startup": False},
        "backup": {"enabled": False},
        "verifactu": {"environment": "TESTING", "pending_recovery_enabled": False},
    }
    path.write_text(json.dumps(settings, indent=2), encoding="utf-8")


def query(db_path: Path, sql: str, *params):
    with sqlite3.connect(db_path) as db:
        return db.execute(sql, params).fetchall()
