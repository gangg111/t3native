"""The builder's window: the user's disc folders and the arcade set in, one button, the APK out.

The run itself is `python -m t3builder.pipeline` in a child process (no console window), so the
window stays responsive and the run can be stopped; its stdout is the log, and its `@@ step total
text` lines move the progress bar.
"""
from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog

from t3builder import pipeline

NO_WINDOW = 0x08000000                     # CREATE_NO_WINDOW: the child is a console program
IMAGE_SUFFIXES = (".cue", ".ecm", ".bin", ".iso")   # in order of preference
# A CHD needs MAME's chdman (disc_image._chd), which the builder does not carry.
CHD_NOTE = "Obrazy .chd nie są obsługiwane: rozpakuj je do .cue/.bin (chdman extractcd z pakietu MAME)."
SETTINGS = pipeline.HOME / "ustawienia.json"

BG = "#121218"
PANEL = "#1c1c26"
FIELD = "#262633"
TEXT = "#ececf2"
MUTED = "#8a8a9e"
ACCENT = "#ffce00"
ERROR = "#ff5a5a"
OK = "#3ddc84"
FONT = "Bahnschrift"


def find_image(folder: Path) -> Path | None:
    """The disc image in a folder: a cue sheet first (it names its tracks), then an ECM, then the
    largest raw or ISO track."""
    if folder.is_file():
        return folder if folder.suffix.lower() in IMAGE_SUFFIXES else None
    if not folder.is_dir():
        return None
    files = [p for p in folder.iterdir() if p.is_file()]
    for suffix in IMAGE_SUFFIXES[:2]:
        found = sorted(p for p in files if p.suffix.lower() == suffix)
        if found:
            return found[0]
    tracks = [p for p in files if p.suffix.lower() in IMAGE_SUFFIXES[2:]]
    return max(tracks, key=lambda p: p.stat().st_size) if tracks else None


def missing_image_note(path: Path) -> str:
    """Why `path` gave no disc image: a CHD gets its own note."""
    chd = path.suffix.lower() == ".chd" or (path.is_dir() and any(p.suffix.lower() == ".chd" for p in path.iterdir()))
    return CHD_NOTE if chd else f"W {path} nie ma obrazu płyty (.cue z plikami .bin, .ecm albo .iso)."


def parse_progress(line: str) -> tuple[int, int, str] | None:
    if not line.startswith(pipeline.PROGRESS + " "):
        return None
    _, step, total, text = line.rstrip("\n").split(" ", 3)
    return int(step), int(total), text


def python_exe() -> str:
    """The console Python next to the window's pythonw (the child's output is read through a pipe)."""
    exe = Path(sys.executable)
    console = exe.with_name("python.exe")
    return str(console if console.is_file() else exe)


class Field:
    """A labelled path row: the path, and a button that picks a folder or a file."""

    def __init__(self, parent, row: int, label: str, hint: str, pick, optional: bool):
        self.pick = pick
        title = tk.Label(parent, text=label, bg=PANEL, fg=TEXT, font=(FONT, 12, "bold"), anchor="w")
        title.grid(row=row, column=0, columnspan=2, sticky="w", padx=18, pady=(14, 2))
        if optional:
            tk.Label(parent, text="opcjonalnie", bg=PANEL, fg=MUTED, font=(FONT, 10), anchor="e").grid(
                row=row, column=1, sticky="e", padx=18, pady=(14, 2))
        self.var = tk.StringVar()
        self.entry = tk.Entry(parent, textvariable=self.var, bg=FIELD, fg=TEXT, insertbackground=TEXT,
                              relief="flat", font=(FONT, 11), highlightthickness=1,
                              highlightbackground=FIELD, highlightcolor=ACCENT)
        self.entry.grid(row=row + 1, column=0, sticky="ew", padx=(18, 8), ipady=7)
        self.hint = hint
        self.placeholder()
        self.entry.bind("<FocusIn>", lambda _: self.clear_placeholder())
        self.entry.bind("<FocusOut>", lambda _: self.placeholder())
        button = tk.Button(parent, text="Wybierz", command=self.choose, bg=FIELD, fg=TEXT, activebackground=ACCENT,
                           activeforeground=BG, relief="flat", font=(FONT, 11), padx=16, cursor="hand2")
        button.grid(row=row + 1, column=1, sticky="e", padx=(0, 18), ipady=4)

    def placeholder(self) -> None:
        if not self.var.get():
            self.entry.configure(fg=MUTED, font=(FONT, 11, "italic"))
            self.var.set(self.hint)

    def clear_placeholder(self) -> None:
        if self.var.get() == self.hint:
            self.var.set("")
        self.entry.configure(fg=TEXT, font=(FONT, 11))

    def value(self) -> str:
        text = self.var.get().strip().strip('"')
        return "" if text == self.hint else text

    def set(self, text: str) -> None:
        if text:
            self.clear_placeholder()
            self.var.set(text)

    def choose(self) -> None:
        chosen = self.pick(self.value())
        if chosen:
            self.set(str(chosen))


class Window:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.process: subprocess.Popen | None = None
        self.lines: queue.Queue[str | None] = queue.Queue()
        self.apk: Path | None = None
        root.title("Kompilator Tekken 3")
        root.configure(bg=BG)
        root.minsize(720, 640)

        header = tk.Frame(root, bg=BG)
        header.pack(fill="x", padx=22, pady=(18, 6))
        tk.Label(header, text="TEKKEN 3", bg=BG, fg=TEXT, font=(FONT, 26, "bold")).pack(side="left")
        tk.Label(header, text="  NATIVE ANDROID PORT", bg=BG, fg=ACCENT, font=(FONT, 14, "bold")).pack(
            side="left", pady=(10, 0))

        panel = tk.Frame(root, bg=PANEL)
        panel.pack(fill="x", padx=22, pady=8)
        panel.columnconfigure(0, weight=1)
        self.jp = Field(panel, 0, "Płyta PlayStation, wersja japońska", "folder z obrazem płyty (.cue z plikami .bin)",
                        self.pick_folder, False)
        self.usa = Field(panel, 2, "Płyta PlayStation, wersja USA", "angielskie teksty i listy ruchów",
                         self.pick_folder, True)
        self.arcade = Field(panel, 4, "Automat (zestaw MAME tekken3.zip)", "sceny i modele postaci z automatu",
                            self.pick_zip, True)
        self.out = Field(panel, 6, "Folder na gotowy plik APK", "folder docelowy", self.pick_out, False)
        tk.Frame(panel, bg=PANEL, height=14).grid(row=8, column=0)

        actions = tk.Frame(root, bg=BG)
        actions.pack(fill="x", padx=22, pady=(10, 4))
        self.build_button = tk.Button(actions, text="ZBUDUJ APK", command=self.start, bg=ACCENT, fg=BG,
                                      activebackground="#ffe066", activeforeground=BG, relief="flat",
                                      font=(FONT, 15, "bold"), padx=28, pady=8, cursor="hand2")
        self.build_button.pack(side="left")
        self.stop_button = tk.Button(actions, text="Przerwij", command=self.stop, bg=FIELD, fg=TEXT,
                                     activebackground=ERROR, relief="flat", font=(FONT, 11), padx=16, pady=8)
        self.open_button = tk.Button(actions, text="Otwórz folder z APK", command=self.open_folder, bg=FIELD,
                                     fg=TEXT, activebackground=OK, relief="flat", font=(FONT, 11), padx=16, pady=8,
                                     cursor="hand2")

        self.status = tk.Label(root, text="", bg=BG, fg=TEXT, font=(FONT, 11), anchor="w")
        self.status.pack(fill="x", padx=22, pady=(10, 2))
        self.bar = tk.Canvas(root, height=10, bg=FIELD, highlightthickness=0)
        self.bar.pack(fill="x", padx=22)
        self.bar_fill = self.bar.create_rectangle(0, 0, 0, 10, fill=ACCENT, width=0)

        log_frame = tk.Frame(root, bg=BG)
        log_frame.pack(fill="both", expand=True, padx=22, pady=(10, 18))
        self.log = tk.Text(log_frame, bg=PANEL, fg=MUTED, font=("Consolas", 9), relief="flat", wrap="word",
                           height=12, state="disabled", padx=10, pady=8)
        self.log.pack(side="left", fill="both", expand=True)

        self.load_settings()
        root.protocol("WM_DELETE_WINDOW", self.close)

    # Pickers

    def pick_folder(self, current: str) -> str:
        folder = filedialog.askdirectory(parent=self.root, initialdir=current or None,
                                         title="Folder z obrazem płyty")
        if not folder:
            return ""
        image = find_image(Path(folder))
        if image is None:
            self.say(missing_image_note(Path(folder)), ERROR)
            return ""
        return str(image)

    def pick_zip(self, current: str) -> str:
        return filedialog.askopenfilename(parent=self.root, initialdir=str(Path(current).parent) if current else None,
                                          title="Zestaw MAME automatu", filetypes=[("Zestaw MAME", "*.zip")])

    def pick_out(self, current: str) -> str:
        return filedialog.askdirectory(parent=self.root, initialdir=current or None, title="Folder na plik APK")

    # Settings

    def load_settings(self) -> None:
        try:
            data = json.loads(SETTINGS.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            data = {}
        for name in ("jp", "usa", "arcade", "out"):
            getattr(self, name).set(data.get(name, ""))
        if not self.out.value():
            self.out.set(str(pipeline.HOME / "APK"))

    def save_settings(self) -> None:
        data = {name: getattr(self, name).value() for name in ("jp", "usa", "arcade", "out")}
        try:
            SETTINGS.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        except OSError:
            pass

    # The run

    def say(self, text: str, colour: str = TEXT) -> None:
        self.status.configure(text=text, fg=colour)

    def append(self, line: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", line if line.endswith("\n") else line + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def set_progress(self, fraction: float) -> None:
        width = self.bar.winfo_width()
        self.bar.coords(self.bar_fill, 0, 0, width * max(0.0, min(1.0, fraction)), 10)

    def start(self) -> None:
        jp = find_image(Path(self.jp.value())) if self.jp.value() else None
        if jp is None:
            self.say(missing_image_note(Path(self.jp.value())) if self.jp.value()
                     else "Wskaż obraz japońskiej płyty PlayStation.", ERROR)
            return
        usa = find_image(Path(self.usa.value())) if self.usa.value() else None
        if self.usa.value() and usa is None:
            self.say(missing_image_note(Path(self.usa.value())), ERROR)
            return
        arcade = Path(self.arcade.value()) if self.arcade.value() else None
        if arcade is not None and not arcade.is_file():
            self.say("Nie ma pliku zestawu automatu.", ERROR)
            return
        out = Path(self.out.value() or pipeline.HOME / "APK")
        self.save_settings()
        command = [python_exe(), "-E", "-s", "-u", "-m", "t3builder.pipeline", "--jp", str(jp), "--out", str(out)]
        if usa:
            command += ["--usa", str(usa)]
        if arcade:
            command += ["--arcade", str(arcade)]
        self.apk = None
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        self.set_progress(0)
        self.say("Start…")
        env = dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
        self.process = subprocess.Popen(command, cwd=pipeline.APP_ROOT, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, text=True,
                                        encoding="utf-8", errors="replace", env=env, creationflags=NO_WINDOW)
        threading.Thread(target=self.read, args=(self.process,), daemon=True).start()
        self.build_button.configure(state="disabled", bg=FIELD)
        self.open_button.pack_forget()
        self.stop_button.pack(side="left", padx=10)
        self.root.after(100, self.poll)

    def read(self, process: subprocess.Popen) -> None:
        for line in process.stdout:
            self.lines.put(line)
        process.wait()
        self.lines.put(None)

    def poll(self) -> None:
        finished = False
        try:
            while True:
                line = self.lines.get_nowait()
                if line is None:
                    finished = True
                    break
                step = parse_progress(line)
                if step is None:
                    self.append(line)
                    continue
                index, total, text = step
                if text.startswith("Gotowe"):
                    self.say(text, OK)
                    self.set_progress(1)
                    self.apk = Path(text.split(": ", 1)[1])
                else:
                    self.say(f"Krok {index} z {total}: {text}")
                    self.set_progress((index - 1) / total)
        except queue.Empty:
            pass
        if finished:
            self.done()
        else:
            self.root.after(100, self.poll)

    def done(self) -> None:
        code = self.process.returncode if self.process else 1
        self.process = None
        self.stop_button.pack_forget()
        self.build_button.configure(state="normal", bg=ACCENT)
        if code == 0 and self.apk is not None:
            self.set_progress(1)
            self.open_button.pack(side="left", padx=10)
        elif code is not None and code != 0:
            self.say("Budowa nie powiodła się: powód jest na końcu logu.", ERROR)

    def stop(self) -> None:
        if self.process is not None:
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(self.process.pid)], capture_output=True,
                           creationflags=NO_WINDOW)
            self.say("Przerwano.", ERROR)

    def open_folder(self) -> None:
        if self.apk is not None:
            subprocess.Popen(["explorer", "/select,", str(self.apk)], creationflags=NO_WINDOW)

    def close(self) -> None:
        self.stop()
        self.save_settings()
        self.root.destroy()


def main() -> None:
    try:
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)      # sharp text on scaled screens
    except (AttributeError, OSError):
        pass
    root = tk.Tk()
    Window(root)
    root.mainloop()
