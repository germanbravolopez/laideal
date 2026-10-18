"""Block E - backup, settings, language, installer language (docs/testing/ui_smoke.md, What it checks)."""

import json
import subprocess
import time

from actions import escape_keys, open_listado
from laideal_ui import click, exists, find, find_text, table_cells, text, wait_for


def settings_dialog(run):
    run.app.menu("Archivo", "Configuración...")
    return run.app.window(title="Configuración")


def set_language(run, language: str, ok: str):
    dialog = settings_dialog(run)
    combo = find_text(dialog, "Idioma:", "ComboBox")
    combo.click_input()
    combo.type_keys(escape_keys(language) + "{ENTER}", with_spaces=True)
    wait_for(lambda: combo.iface_value.CurrentValue == language, 5, f"Idioma = {language}")
    find_text(dialog, ok, "Button").click_input()
    wait_for(lambda: not any(w.window_text() == "Configuración" for w in run.app.windows()), 10,
             "Configuración to close")


def question_buttons(run):
    """The buttons of a Yes/No question (Listado de gastos → Eliminar fila), answered No."""
    gastos = open_listado(run.app, "Gastos")
    table_cells(find(gastos, "table_listado"))[0].click_input()
    click(gastos, "btnDelete")
    box = run.app.message_box()
    labels = sorted(b.window_text() for b in box.descendants(control_type="Button")
                    if b.window_text() and b.window_text() != "Close")   # not the title bar's
    no = next(label for label in labels if label == "No")
    find_text(box, no, "Button").click_input()
    gastos.close()
    return labels


def release_notes(run):
    run.app.menu("Ayuda", "Notas de la versión...")
    window = wait_for(lambda: next((w for w in run.app.windows()
                                    if w.window_text() in ("Release notes", "Notas de la versión")), None),
                      10, "the release notes")
    title = window.window_text()
    window.close()
    return title


def install(run, language: str):
    install_dir = run.args.exe.parent
    run.stop_app()
    result = subprocess.run([str(run.args.installer), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
                             f"/LANG={language}", f"/DIR={install_dir}"], timeout=300)
    run.check(result.returncode == 0, f"the {language} installer exited with {result.returncode}")


def first_start_language(run) -> str:
    """Starts the app on a settings file without a language, as on a machine without
    settings, and returns the language its standard buttons speak ('en' / 'es'). The
    language taken from the installer stays in memory until something is saved, so it
    is read from the screen, not from the file."""
    settings = json.loads(run.settings.read_text(encoding="utf-8"))
    settings.get("app", {}).pop("language", None)
    fresh = run.out / "settings-no-language.json"
    fresh.write_text(json.dumps(settings, indent=2), encoding="utf-8")
    saved, run.settings = run.settings, fresh
    try:
        run.fresh_start()
        labels = question_buttons(run)
        run.stop_app()
    finally:
        run.settings = saved
    return {("No", "Yes"): "en", ("No", "Sí"): "es"}.get(tuple(labels), str(labels))


def run(run):
    run.fresh_start()
    with run.step("E", "E1", "Archivo → Hacer copia de seguridad ahora: a copy next to the database"):
        before = {p for p in run.out.rglob("*.db") if p != run.db}
        run.app.menu("Archivo", "Hacer copia de seguridad ahora...")
        result = find(run.app.main, "lblResult")
        wait_for(lambda: "Copia de seguridad creada" in text(result), 60, "the backup")
        copies = {p for p in run.out.rglob("*.db") if p != run.db} - before
        run.check(copies, "no backup file next to the database")

    with run.step("E", "E2", "Configuración has no 'Tipo de IVA' (21 % is fixed)"):
        dialog = settings_dialog(run)
        shown = [e.window_text() for e in dialog.descendants() if e.window_text()]
        run.check(not any("IVA" in s for s in shown), f"Configuración shows {[s for s in shown if 'IVA' in s]}")
        find_text(dialog, "Cancelar", "Button").click_input()

    with run.step("E", "E3", "Idioma English: Yes/No and English release notes without a restart; back to Español"):
        set_language(run, "English", "Aceptar")
        labels = question_buttons(run)
        run.check(labels == ["No", "Yes"], f"question buttons in English: {labels}")
        run.check(release_notes(run) == "Release notes", "the release notes are not the English ones")
        set_language(run, "Español", "OK")
        labels = question_buttons(run)
        run.check(labels == ["No", "Sí"], f"question buttons in Spanish: {labels}")
        run.check(release_notes(run) == "Notas de la versión", "the release notes are not the Spanish ones")

    if not run.args.installer:
        run.skip("E", "E4", "Installer language becomes the app's first language", "no --installer given")
        return
    with run.step("E", "E4", "Installer language becomes the app's first language (English, then Spanish)"):
        install(run, "english")
        language = first_start_language(run)
        run.check(language == "en", f"after the English installer the app started in {language!r}")
        install(run, "spanish")
        language = first_start_language(run)
        run.check(language == "es", f"after the Spanish installer the app started in {language!r}")
