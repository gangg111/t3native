"""The builder's window, started by the launcher with the bundled pythonw.exe."""
import os
import sys
import traceback
from pathlib import Path

# pythonw has no console: sys.stdout and sys.stderr are None, and a library writing to them fails.
for name in ("stdout", "stderr"):
    if getattr(sys, name) is None:
        setattr(sys, name, open(os.devnull, "w"))

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    from t3builder import gui
    gui.main()
except Exception:  # noqa: BLE001  (pythonw hides crashes: keep the trace and say so)
    log = Path(__file__).resolve().parents[1] / "crash.log"
    log.write_text(traceback.format_exc(), encoding="utf-8")
    import ctypes
    ctypes.windll.user32.MessageBoxW(None, f"Kompilator przerwał pracę. Szczegóły: {log}", "Kompilator Tekken 3", 0x10)
