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

from t3builder import identify, pipeline, texts
from t3builder.texts import t

NO_WINDOW = 0x08000000                     # CREATE_NO_WINDOW: the child is a console program
IMAGE_SUFFIXES = (".cue", ".ecm", ".bin", ".iso")   # in order of preference
SETTINGS = pipeline.HOME / "settings.json"

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
    # A CHD needs MAME's chdman (disc_image._chd), which the builder does not carry.
    return t("chd") if chd else t("no_image", path=path)


def parse_progress(line: str) -> tuple | None:
    """(step, total, text) of a progress line, ("done", minutes, apk) of the last one, else None."""
    if not line.startswith(pipeline.PROGRESS + " "):
        return None
    _, first, second, rest = line.rstrip("\n").split(" ", 3)
    if first == pipeline.DONE:
        return first, second, rest
    return int(first), int(second), rest


def python_exe() -> str:
    """The console Python next to the window's pythonw (the child's output is read through a pipe)."""
    exe = Path(sys.executable)
    console = exe.with_name("python.exe")
    return str(console if console.is_file() else exe)


def check_disc(value: str, wanted: tuple[str, ...], elsewhere: str) -> tuple[bool, str, str]:
    """(ok, the text key of the problem, the release's name) of the disc image at `value`."""
    image = find_image(Path(value))
    if image is None:
        return False, "unknown_disc", ""
    key, _ = identify.disc(image)
    if key in wanted:
        return True, "", identify.DISC_LABELS[key]
    return False, (elsewhere if key in identify.DISC_LABELS else "unknown_disc"), ""


def check_jp(value: str) -> tuple[bool, str, str]:
    return check_disc(value, identify.JAPANESE, "is_usa")


def check_usa(value: str) -> tuple[bool, str, str]:
    return check_disc(value, ("usa",), "is_japanese")


def check_arcade(value: str) -> tuple[bool, str, str]:
    ok, _ = identify.arcade(Path(value))
    return (True, "", identify.ARCADE_LABEL) if ok else (False, "bad_arcade", "")


class Field:
    """A labelled path row: the path, and a button that picks a folder or a file. With `check`
    (in a thread: reading a disc takes a moment) the row's note says what was chosen: the
    release in green, or the problem in red."""

    def __init__(self, parent, row: int, key: str, pick, optional: bool, check=None):
        self.pick = pick
        self.key = key
        self.is_optional = optional
        self.check = check
        self.checked = None            # the value the result is for
        self.result = None             # None, "checking", or check's (ok, problem, name)
        self.answer = None             # (value, result) from the checking thread
        self.title = tk.Label(parent, bg=PANEL, fg=TEXT, font=(FONT, 12, "bold"), anchor="w")
        self.title.grid(row=row, column=0, columnspan=2, sticky="w", padx=18, pady=(14, 2))
        self.note = tk.Label(parent, bg=PANEL, fg=MUTED, font=(FONT, 10), anchor="e")
        self.note.grid(row=row, column=0, columnspan=2, sticky="e", padx=18, pady=(14, 2))
        self.var = tk.StringVar()
        self.entry = tk.Entry(parent, textvariable=self.var, bg=FIELD, fg=TEXT, insertbackground=TEXT,
                              relief="flat", font=(FONT, 11), highlightthickness=1,
                              highlightbackground=FIELD, highlightcolor=ACCENT)
        self.entry.grid(row=row + 1, column=0, sticky="ew", padx=(18, 8), ipady=7)
        self.hint = ""
        self.entry.bind("<FocusIn>", lambda _: self.clear_placeholder())
        self.entry.bind("<FocusOut>", lambda _: (self.placeholder(), self.recheck()))
        self.button = tk.Button(parent, command=self.choose, bg=FIELD, fg=TEXT, activebackground=ACCENT,
                           activeforeground=BG, relief="flat", font=(FONT, 11), padx=16, cursor="hand2")
        self.button.grid(row=row + 1, column=1, sticky="e", padx=(0, 18), ipady=4)
        self.retext()

    def retext(self) -> None:
        """The words in the current language; a shown hint is replaced by the new one."""
        self.title.configure(text=t(self.key))
        self.show()
        self.button.configure(text=t("choose"))
        if self.var.get() == self.hint:
            self.var.set("")
        self.hint = t(self.key + "_hint")
        if self.root_focus() is not self.entry:
            self.placeholder()

    def root_focus(self):
        try:
            return self.entry.focus_get()
        except (KeyError, tk.TclError):
            return None

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
            self.recheck()

    def show(self) -> None:
        """The note: the check's result, else "optional" on an empty optional row."""
        if self.result == "checking":
            self.note.configure(text=t("checking"), fg=MUTED)
        elif isinstance(self.result, tuple):
            ok, problem, name = self.result
            self.note.configure(text=name if ok else t(problem), fg=OK if ok else ERROR)
        else:
            self.note.configure(text=t("optional") if self.is_optional else "", fg=MUTED)

    def problem(self) -> str | None:
        """The text of what is wrong with the row's choice, "" while it is being checked, None if
        nothing is."""
        if self.result == "checking":
            return ""
        if isinstance(self.result, tuple) and not self.result[0]:
            return t("fix_field", field=t(self.key), problem=t(self.result[1]))
        return None

    def recheck(self) -> None:
        value = self.value()
        if self.check is None or value == self.checked:
            return
        self.checked = value
        self.result = "checking" if value else None
        self.show()
        if value:
            threading.Thread(target=self._work, args=(value,), daemon=True).start()
            self.entry.after(150, self._collect)

    def _work(self, value: str) -> None:
        try:
            result = self.check(value)
        except Exception:  # noqa: BLE001  (an unreadable file is just not the release)
            result = (False, "unknown_disc" if self.check is not check_arcade else "bad_arcade", "")
        self.answer = (value, result)

    def _collect(self) -> None:
        answer = self.answer
        if answer is None or answer[0] != self.checked:
            if self.result == "checking":
                self.entry.after(150, self._collect)
            return
        self.result = answer[1]
        self.show()

    def choose(self) -> None:
        chosen = self.pick(self.value())
        if chosen:
            self.set(str(chosen))


class Switch:
    """A labelled row of choices (the APK's variant): the chosen one in the accent colour."""

    def __init__(self, parent, row: int, key: str, choices: tuple[str, ...], changed):
        self.key = key
        self.choices = choices
        self.value = choices[0]
        self.changed = changed
        self.title = tk.Label(parent, bg=PANEL, fg=TEXT, font=(FONT, 12, "bold"), anchor="w")
        self.title.grid(row=row, column=0, sticky="w", padx=18, pady=(16, 2))
        box = tk.Frame(parent, bg=PANEL)
        box.grid(row=row, column=0, columnspan=2, sticky="e", padx=18, pady=(16, 2))
        self.buttons = {}
        for choice in choices:
            b = tk.Button(box, command=lambda c=choice: self.pick(c), relief="flat", font=(FONT, 11, "bold"),
                          padx=14, cursor="hand2", activebackground=ACCENT, activeforeground=BG)
            b.pack(side="left", padx=(6, 0), ipady=3)
            self.buttons[choice] = b
        self.retext()

    def pick(self, choice: str) -> None:
        if choice in self.choices:
            self.value = choice
        self.retext()
        self.changed()

    def retext(self) -> None:
        self.title.configure(text=t(self.key))
        for choice, b in self.buttons.items():
            chosen = choice == self.value
            b.configure(text=t(f"{self.key}_{choice}"), bg=ACCENT if chosen else FIELD, fg=BG if chosen else TEXT)


class Window:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.process: subprocess.Popen | None = None
        self.lines: queue.Queue[str | None] = queue.Queue()
        self.apk: Path | None = None
        root.configure(bg=BG)
        root.minsize(720, 640)

        header = tk.Frame(root, bg=BG)
        header.pack(fill="x", padx=22, pady=(18, 6))
        tk.Label(header, text="TEKKEN 3", bg=BG, fg=TEXT, font=(FONT, 26, "bold")).pack(side="left")
        tk.Label(header, text="  NATIVE ANDROID PORT", bg=BG, fg=ACCENT, font=(FONT, 14, "bold")).pack(
            side="left", pady=(10, 0))
        # The language switch: the chosen one in the accent colour.
        self.languages = {}
        for code in reversed(texts.LANGUAGES):
            label = tk.Label(header, text=code.upper(), bg=BG, font=(FONT, 12, "bold"), padx=6, cursor="hand2")
            label.pack(side="right", pady=(10, 0))
            label.bind("<Button-1>", lambda _, c=code: self.set_language(c))
            self.languages[code] = label

        panel = tk.Frame(root, bg=PANEL)
        panel.pack(fill="x", padx=22, pady=8)
        panel.columnconfigure(0, weight=1)
        self.jp = Field(panel, 0, "jp", self.pick_folder, False, check_jp)
        self.usa = Field(panel, 2, "usa", self.pick_folder, True, check_usa)
        self.arcade = Field(panel, 4, "arcade", self.pick_zip, True, check_arcade)
        self.out = Field(panel, 6, "out", self.pick_out, False)
        self.variant = Switch(panel, 8, "variant", pipeline.VARIANTS, self.save_settings)
        tk.Frame(panel, bg=PANEL, height=14).grid(row=9, column=0)

        actions = tk.Frame(root, bg=BG)
        actions.pack(fill="x", padx=22, pady=(10, 4))
        self.build_button = tk.Button(actions, command=self.start, bg=ACCENT, fg=BG,
                                      activebackground="#ffe066", activeforeground=BG, relief="flat",
                                      font=(FONT, 15, "bold"), padx=28, pady=8, cursor="hand2")
        self.build_button.pack(side="left")
        self.stop_button = tk.Button(actions, command=self.stop, bg=FIELD, fg=TEXT,
                                     activebackground=ERROR, relief="flat", font=(FONT, 11), padx=16, pady=8)
        self.open_button = tk.Button(actions, command=self.open_folder, bg=FIELD,
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
        self.retext()
        root.protocol("WM_DELETE_WINDOW", self.close)

    def set_language(self, code: str) -> None:
        texts.set_language(code)
        self.retext()
        self.save_settings()

    def retext(self) -> None:
        """Every word of the window in the current language."""
        self.root.title(t("title"))
        for code, label in self.languages.items():
            label.configure(fg=ACCENT if code == texts.language() else MUTED)
        for field in (self.jp, self.usa, self.arcade, self.out):
            field.retext()
        self.variant.retext()
        self.build_button.configure(text=t("build"))
        self.stop_button.configure(text=t("stop"))
        self.open_button.configure(text=t("open"))

    # Pickers

    def pick_folder(self, current: str) -> str:
        folder = filedialog.askdirectory(parent=self.root, initialdir=current or None,
                                         title=t("pick_disc"))
        if not folder:
            return ""
        image = find_image(Path(folder))
        if image is None:
            self.say(missing_image_note(Path(folder)), ERROR)
            return ""
        return str(image)

    def pick_zip(self, current: str) -> str:
        return filedialog.askopenfilename(parent=self.root, initialdir=str(Path(current).parent) if current else None,
                                          title=t("pick_arcade"), filetypes=[(t("mame_set"), "*.zip")])

    def pick_out(self, current: str) -> str:
        return filedialog.askdirectory(parent=self.root, initialdir=current or None, title=t("pick_out"))

    # Settings

    def load_settings(self) -> None:
        try:
            data = json.loads(SETTINGS.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            data = {}
        texts.set_language(data.get("lang"))
        for name in ("jp", "usa", "arcade", "out"):
            getattr(self, name).set(data.get(name, ""))
        if not self.out.value():
            self.out.set(str(pipeline.HOME / "APK"))
        if data.get("variant") in pipeline.VARIANTS:
            self.variant.value = data["variant"]

    def save_settings(self) -> None:
        data = {name: getattr(self, name).value() for name in ("jp", "usa", "arcade", "out")}
        data["lang"] = texts.language()
        data["variant"] = self.variant.value
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
                     else t("need_jp"), ERROR)
            return
        usa = find_image(Path(self.usa.value())) if self.usa.value() else None
        if self.usa.value() and usa is None:
            self.say(missing_image_note(Path(self.usa.value())), ERROR)
            return
        arcade = Path(self.arcade.value()) if self.arcade.value() else None
        if arcade is not None and not arcade.is_file():
            self.say(t("no_arcade"), ERROR)
            return
        for field in (self.jp, self.usa, self.arcade):
            problem = field.problem()
            if problem is not None:
                self.say(problem or t("checking"), ERROR if problem else TEXT)
                return
        out = Path(self.out.value() or pipeline.HOME / "APK")
        self.save_settings()
        command = [python_exe(), "-E", "-s", "-u", "-m", "t3builder.pipeline", "--jp", str(jp), "--out", str(out),
                   "--lang", texts.language(), "--variant", self.variant.value]
        if usa:
            command += ["--usa", str(usa)]
        if arcade:
            command += ["--arcade", str(arcade)]
        self.apk = None
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        self.set_progress(0)
        self.say(t("starting"))
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
                if step[0] == pipeline.DONE:
                    _, minutes, apk = step
                    self.say(t("done", minutes=minutes, path=apk), OK)
                    self.set_progress(1)
                    self.apk = Path(apk)
                else:
                    index, total, text = step
                    self.say(t("step", index=index, total=total, text=text))
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
            self.say(t("failed"), ERROR)

    def stop(self) -> None:
        if self.process is not None:
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(self.process.pid)], capture_output=True,
                           creationflags=NO_WINDOW)
            self.say(t("stopped"), ERROR)

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
