"""Automated UI smoke test: the pre-release smoke test driven like a person.

Starts the UI-test build of laideal.exe (CMake LAIDEAL_UI_TEST) against the fake
AEAT (tests/tools/fake_aeat_server) on a synthetic database, and runs the smoke
test blocks in order on that one database, as a person would:

    A  start-up on a real schema (migrations, recovery dialog, AEAT safety)
    B  ticket screens and wiring (Recogida, Cobrar, Añadir / Anular prendas, listados)
    C  printing, through a printer queue whose jobs are captured (needs --printer)
    D  Contabilidad and the quarter lock
    E  backup, settings, language, installer language (installer with --installer)

    python tests/ui/smoke.py --exe <laideal.exe> --tools <build>/tests --out ui-smoke-out

Each step leaves a screenshot; a failed step also leaves every open window's
control tree. The report is report.md in --out (and the GitHub job summary).
"""

import argparse
import contextlib
import datetime
import json
import os
import subprocess
import sys
import time
import traceback
from pathlib import Path

import seed
from laideal_ui import LaIdeal, dump_tree, find, object_name, screenshot, wait_for

BLOCKS = "ABCDE"


class StepFailed(Exception):
    pass


class Run:
    def __init__(self, args):
        self.args = args
        self.out = args.out.resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        (self.out / "steps").mkdir(exist_ok=True)
        self.db = self.out / "shop.db"
        self.settings = self.out / "settings.json"
        self.results = []           # (block, step id, title, status, detail, seconds)
        self.app = None
        self.fake = None
        self.fake_url = None
        self.output = open(self.out / "app-output.txt", "w", encoding="utf-8")
        self.printer = None
        self.title = "UI smoke test"
        self.notes = []             # extra report lines (markdown)

    # --- processes -------------------------------------------------------------------------

    def start_fake_aeat(self):
        url_file = self.out / "fake-aeat-url.txt"
        url_file.unlink(missing_ok=True)
        self.fake = subprocess.Popen([str(self.args.tools / "fake_aeat_server.exe"),
                                      "--url-file", str(url_file),
                                      "--records-file", str(self.out / "fake-aeat-records.json")],
                                     stdout=self.output, stderr=subprocess.STDOUT)
        self.fake_url = wait_for(lambda: url_file.exists() and url_file.read_text(), 15,
                                 "the fake AEAT to listen")

    def start_app(self):
        self.stop_app()
        self.app = LaIdeal(self.args.exe, self.settings, self.output, self.fake_url)
        return self.app

    def stop_app(self):
        if self.app:
            self.app.close()
            self.keep_log()
            self.app = None

    def keep_log(self):
        """Appends what the app logged since it started to the run's laideal-run.log."""
        with open(self.out / "laideal-run.log", "a", encoding="utf-8") as f:
            f.write(self.app.log())

    def log(self) -> str:
        """Everything the app logged during the run, the running session included."""
        path = self.out / "laideal-run.log"
        logged = path.read_text(encoding="utf-8") if path.exists() else ""
        return logged + (self.app.log() if self.app else "")

    def restart(self):
        self.stop_app()
        return self.start_app()

    def fresh_start(self):
        """Restarts the app and closes the recovery dialog if it opens (~4 s after the start):
        it is window-modal and would block every other window."""
        app = self.restart()
        with contextlib.suppress(AssertionError):
            dialog = app.window(title="Envíos Verifactu pendientes", timeout=7)
            find(dialog, "btnClose").click_input()
        return app

    def update_settings(self, change):
        """Edits the settings file in place (the app keeps its own keys in it)."""
        data = json.loads(self.settings.read_text(encoding="utf-8"))
        change(data)
        self.settings.write_text(json.dumps(data, indent=2), encoding="utf-8")

    # --- database --------------------------------------------------------------------------

    def rows(self, sql, *params):
        return seed.query(self.db, sql, *params)

    def scalar(self, sql, *params):
        found = self.rows(sql, *params)
        return found[0][0] if found else None

    # --- steps -----------------------------------------------------------------------------

    @contextlib.contextmanager
    def step(self, block: str, step_id: str, title: str):
        print(f"[{step_id}] {title}", flush=True)
        started = time.monotonic()
        try:
            yield
        except Exception as error:
            seconds = time.monotonic() - started
            detail = f"{type(error).__name__}: {error}"
            print(f"[{step_id}] FAILED - {detail}", flush=True)
            traceback.print_exc()
            self.results.append((block, step_id, title, "FAIL", detail, seconds))
            self.capture(step_id, failed=True)
            raise StepFailed(detail) from error
        seconds = time.monotonic() - started
        self.results.append((block, step_id, title, "PASS", "", seconds))
        self.capture(step_id)

    def capture(self, step_id: str, failed: bool = False):
        if not self.app or self.app.process.poll() is not None:
            return
        try:
            screenshot(self.app.main, self.out / "steps" / f"{step_id}.png")
            if failed:
                folder = self.out / "failures" / step_id
                folder.mkdir(parents=True, exist_ok=True)
                for i, window in enumerate(self.app.windows()):
                    name = object_name(window) or window.window_text() or "window"
                    dump_tree(window, folder / f"{i:02d}-{name[:40].replace('/', '_')}.txt")
        except Exception as error:
            print(f"  (could not capture {step_id}: {error})")

    def skip(self, block: str, step_id: str, title: str, reason: str):
        print(f"[{step_id}] SKIPPED - {reason}", flush=True)
        self.results.append((block, step_id, title, "SKIP", reason, 0.0))

    @staticmethod
    def check(condition, message: str):
        if not condition:
            raise AssertionError(message)

    # --- report ----------------------------------------------------------------------------

    def report(self) -> bool:
        failed = [r for r in self.results if r[3] == "FAIL"]
        lines = [f"# {self.title} - {datetime.datetime.now():%d-%m-%Y %H:%M}", "",
                 f"{sum(r[3] == 'PASS' for r in self.results)} passed, {len(failed)} failed, "
                 f"{sum(r[3] == 'SKIP' for r in self.results)} skipped.", "",
                 "| Step | Check | Result | Time |", "|------|-------|--------|------|"]
        for block, step_id, title, status, detail, seconds in self.results:
            shown = status if not detail else f"{status}: {detail}".replace("|", "/").replace("\n", " ")
            lines.append(f"| {step_id} | {title} | {shown} | {seconds:.0f} s |")
        if self.notes:
            lines += [""] + self.notes
        report ="\n".join(lines) + "\n"
        (self.out / "report.md").write_text(report, encoding="utf-8")
        summary = os.environ.get("GITHUB_STEP_SUMMARY")
        if summary:
            with open(summary, "a", encoding="utf-8") as f:
                f.write(report)
        print(report)
        return not failed

    def close(self):
        self.stop_app()
        if self.fake:
            self.fake.kill()
        if self.printer:
            self.printer.stop()
        self.output.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, required=True, help="laideal.exe of a LAIDEAL_UI_TEST build")
    parser.add_argument("--tools", type=Path, required=True,
                        help="folder with fake_aeat_server.exe and escpos_preview.exe")
    parser.add_argument("--out", type=Path, default=Path("ui-smoke-out"))
    parser.add_argument("--blocks", default=BLOCKS, help="which blocks to run, e.g. AB")
    parser.add_argument("--printer", default="",
                        help="printer queue for block C (see printer.py); block C is skipped without it")
    parser.add_argument("--printer-port", type=int, default=9100, help="the queue's port (printer.py --port)")
    parser.add_argument("--installer", type=Path, help="laideal_setup_X.Y.exe for E4 (reinstalls in place)")
    args = parser.parse_args()

    import block_a, block_b, block_c, block_d, block_e
    modules = {"A": block_a, "B": block_b, "C": block_c, "D": block_d, "E": block_e}

    run = Run(args)
    ok = False
    try:
        run.start_fake_aeat()
        seed.create_database(run.db)
        seed.write_settings(run.settings, run.db, run.out / "reports", aeat=True)
        if "A" not in args.blocks.upper():
            block_a.prepare(run)
        for letter in BLOCKS:
            if letter not in args.blocks.upper():
                continue
            try:
                modules[letter].run(run)
            except StepFailed:
                print(f"Block {letter} stopped at its failed step; going on with the next block.")
        with contextlib.suppress(StepFailed):
            block_a.safety_check(run, "A3-end")
    finally:
        ok = run.report()
        run.close()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # a CI console is cp1252
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    sys.path.insert(0, str(Path(__file__).parent))
    main()
