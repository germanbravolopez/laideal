"""A Windows printer queue whose print jobs are captured, for smoke block C.

The queue uses the built-in "Generic / Text Only" driver on a Standard TCP/IP
port (RAW) pointed at 127.0.0.1:<port>, where a listener saves each job's bytes
as jobs/job-NNN.prn. laideal.exe sends RAW ESC/POS through WritePrinter, so the
spooler forwards the bytes unchanged; one connection is one job. A FILE: port
would overwrite its file on every job and lose the second copy of a ticket.

    python tests/ui/printer.py install [--name LaIdeal-Smoke] [--port 9100]   (as administrator)
    python tests/ui/printer.py remove  [--name LaIdeal-Smoke] [--port 9100]
    python tests/ui/printer.py listen --out print-jobs --tools build-uitest/tests [--port 9100]
        (for a manual run: captures and decodes every job until Ctrl+C)

Captured jobs are decoded with escpos_preview.exe (tests/tools), the reader of
test_ticket_preview: <job>.txt is an ASCII mock ("[QR wxh]" for the QR image).
"""

import argparse
import socket
import subprocess
import threading
from pathlib import Path

DRIVER = "Generic / Text Only"
QUEUE = "LaIdeal-Smoke"
PORT = 9100


def port_name(port: int) -> str:
    return f"LaIdealSmoke_{port}"


def powershell(script: str) -> None:
    subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script], check=True)


def install(name: str, port: int) -> None:
    powershell(f"""
        $ErrorActionPreference = 'Stop'
        if (-not (Get-PrinterDriver -Name '{DRIVER}' -ErrorAction SilentlyContinue)) {{ Add-PrinterDriver -Name '{DRIVER}' }}
        if (-not (Get-PrinterPort -Name '{port_name(port)}' -ErrorAction SilentlyContinue)) {{
            Add-PrinterPort -Name '{port_name(port)}' -PrinterHostAddress '127.0.0.1' -PortNumber {port} }}
        if (-not (Get-Printer -Name '{name}' -ErrorAction SilentlyContinue)) {{
            Add-Printer -Name '{name}' -DriverName '{DRIVER}' -PortName '{port_name(port)}' }}
        Get-Printer -Name '{name}' | Format-List Name, DriverName, PortName, PrinterStatus
    """)


def remove(name: str, port: int) -> None:
    powershell(f"""
        Remove-Printer -Name '{name}' -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        Remove-PrinterPort -Name '{port_name(port)}' -ErrorAction SilentlyContinue
    """)


class JobCapture:
    """Listens on 127.0.0.1:<port> and saves each connection's bytes as one job file."""

    def __init__(self, folder: Path, port: int = PORT):
        self.folder = folder
        folder.mkdir(parents=True, exist_ok=True)
        self.server = socket.create_server(("127.0.0.1", port))
        self.server.settimeout(0.5)
        self.jobs = []
        self.running = True
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while self.running:
            try:
                connection, _ = self.server.accept()
            except (socket.timeout, OSError):
                continue
            with connection:
                connection.settimeout(10)
                data = bytearray()
                while True:
                    try:
                        chunk = connection.recv(65536)
                    except socket.timeout:
                        break
                    if not chunk:
                        break
                    data += chunk
            if data:
                path = self.folder / f"job-{len(self.jobs) + 1:03d}.prn"
                path.write_bytes(bytes(data))
                self.jobs.append(path)

    def count(self) -> int:
        return len(self.jobs)

    def stop(self):
        self.running = False
        self.thread.join(timeout=2)
        self.server.close()


def decode(tools: Path, job: Path) -> str:
    """The ASCII mock of a captured job (escpos_preview writes <job>.txt and <job>.png)."""
    prefix = job.with_suffix("")
    subprocess.run([str(tools / "escpos_preview.exe"), str(job), str(prefix)], check=True)
    return prefix.with_suffix(".txt").read_text(encoding="utf-8")


def listen(out: Path, tools: Path, port: int) -> None:
    """Captures jobs for a manual run and prints each one's ASCII mock as it arrives."""
    import time
    capture = JobCapture(out, port)
    print(f"Listening on 127.0.0.1:{port}; jobs go to {out}. Ctrl+C to stop.")
    shown = 0
    try:
        while True:
            time.sleep(0.5)
            while shown < capture.count():
                job = capture.jobs[shown]
                shown += 1
                print(f"===== {job.name} =====")
                print(decode(tools, job) if tools else "(no --tools: not decoded)")
    except KeyboardInterrupt:
        pass
    finally:
        capture.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("action", choices=["install", "remove", "listen"])
    parser.add_argument("--name", default=QUEUE)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--out", type=Path, default=Path("print-jobs"), help="listen: folder for the jobs")
    parser.add_argument("--tools", type=Path, help="listen: the folder with escpos_preview.exe")
    args = parser.parse_args()
    if args.action == "listen":
        listen(args.out, args.tools, args.port)
    else:
        (install if args.action == "install" else remove)(args.name, args.port)
