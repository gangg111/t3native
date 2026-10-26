"""The game's movies as MPEG-1 for the native player (pl_mpeg), straight from the disc.

The remake's converter decodes each STR movie into frames on a 30 fps grid and its XA sound
(tools/remake_import/media.py), then encodes them to Ogg Theora. FFmpeg 8.0.1's libtheora encoder
writes streams its own decoder (and Godot's) rejects in part (`error in unpack_block_qpis`), so
the .ogv files cannot be the source. This tool takes the same frames and sound from the remake's
own functions and encodes them to MPEG-1 video + MP2 sound instead, one file per variant beside
the .ogv names movies.json lists (`<name>.<variant>.mpg`): `large` (desktop) and `small` (Android).

MP2 has no 37 800 Hz rate (the XA rate), so the sound is resampled to 44 100 Hz. Every output is
checked: it decodes without an error, has one frame per grid slot and lasts the movie's duration.

    py -3.12 tools/movies_from_disc.py [--disc CUE] [--out DIR] [--only NAME ...]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

WORKSPACE = Path(os.environ.get("T3_WORKSPACE", Path(__file__).resolve().parents[2]))
REMAKE = Path(os.environ.get("T3_REMAKE_ROOT", WORKSPACE / "Tekken3Remake"))
DISC = Path(os.environ.get("T3_DISC", WORKSPACE / "japan rev.1 dump/Tekken 3 (Japan) (Rev 1).cue"))
# mpeg1video -q:v (1 best ... 31 worst): both at the best, the user wants the movies at their
# highest quality on the phone too (title_0: 1 = 30.6 MB, 50.8 dB PSNR against the disc's frames;
# 5 = 7.9 MB, 41.7 dB). 1 needs -qmin 1, whose default of 2 raises it silently.
QUALITIES = {"large": 1, "small": 1}
AUDIO_RATE = 44100
FPS = 30                               # media.MOVIE_FPS: the grid the frames are laid out on
DURATION_TOLERANCE = 1.0 / FPS         # seconds: the grid's last slot


@dataclass
class Check:
    frames: int
    duration: float
    errors: list[str]


def encode(folder: Path, dest: Path, quality: int) -> None:
    """`grid_%06d.png` and `audio.wav` in `folder` (media._grid_frames) to an MPEG-1 program stream."""
    run = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-y", "-v", "error", "-framerate", str(FPS),
                          "-i", str(folder / "grid_%06d.png"), "-i", str(folder / "audio.wav"),
                          "-map", "0:v", "-map", "1:a", "-c:v", "mpeg1video", "-qmin", "1", "-q:v", str(quality),
                          "-c:a", "mp2", "-b:a", "384k", "-ar", str(AUDIO_RATE), "-f", "mpeg", str(dest)],
                         capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError(f"{dest.name}: ffmpeg failed: {run.stderr.strip()[-400:]}")


def check(path: Path) -> Check:
    """The frames a full decode yields, the duration they make at 30 fps, and every error the
    decode reports. ffprobe's durations of an MPEG program stream are estimates (from the bit rate:
    0.72 s for a 1 s test movie), and the XA sound may end before the picture, so the decoded frame
    count is the measure. The `null` muxer's complaints about repeated timestamps come from the
    test output, not from decoding the movie: they are not counted."""
    out = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0", "-show_entries",
                          "stream=nb_read_frames", "-of", "json", str(path)],
                         capture_output=True, text=True, check=True).stdout
    frames = int(json.loads(out)["streams"][0]["nb_read_frames"])
    decode = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-v", "error", "-i", str(path), "-f", "null", "-"],
                            capture_output=True, text=True)
    errors = [line for line in decode.stderr.splitlines()
              if line.strip() and "non monotonically increasing dts" not in line]
    return Check(frames, frames / FPS, errors)


def problems(c: Check, slots: int, duration: float) -> list[str]:
    found = []
    if c.errors:
        found.append(f"{len(c.errors)} decode errors, first: {c.errors[0]}")
    if c.frames != slots:
        found.append(f"{c.frames} frames, the grid has {slots}")
    if abs(c.duration - duration) > DURATION_TOLERANCE:
        found.append(f"{c.duration:.3f} s, the movie lasts {duration:.3f} s")
    return found


def movie_list(media, disc) -> list[tuple[str, object]]:
    """(name, movie) of every movie the converter writes, named as it names them: the title's two,
    then ending.ovl's list without the entries that repeat a movie already listed."""
    blocks = disc.blocks()
    movies = [(f"title_{n}", media._movie(blocks["title"], media.TITLE_MOVIES, media.TITLE_SECTORS, n)) for n in range(2)]
    known = {m.key for _, m in movies}
    for n in range(media.ENDING_MOVIE_COUNT):
        m = media._movie(blocks["ending"], media.ENDING_MOVIES, media.ENDING_SECTORS, n)
        if m.key not in known:
            known.add(m.key)
            movies.append((f"ending_{n}", m))
    return movies


def listed_names(movies_json: Path) -> set[str]:
    """The movie names movies.json refers to (its .ogv variants without the variant)."""
    data = json.loads(movies_json.read_text())
    names = set()
    for entry in data.get("title", []) + data.get("ending", []):
        for video in entry.get("video", []):
            names.add(video.split(".")[0])
    return names


def encode_movie(media, tool, raw: bytes, arrivals: list[int], variants: dict[str, int],
                 write) -> tuple[float, list[str]]:
    """One movie's frames and sound (`media._movie_sectors`) encoded to each variant and checked;
    `write(variant, part_path)` keeps a good file. Returns the duration and the problems found."""
    duration = arrivals[-1] / media.SECTORS_PER_SECOND + media.MOVIE_TAIL
    slots = math.ceil(duration * FPS)
    pcm, rate, channels = media._pcm(tool, raw)
    pcm = pcm[:min(len(pcm) // (2 * channels), round(duration * rate)) * 2 * channels]
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        media._grid_frames(tool, raw, arrivals, duration, folder)
        (folder / "audio.wav").write_bytes(media.wav_bytes(pcm, rate, channels))
        for variant, quality in variants.items():
            part = folder / f"{variant}.part.mpg"
            encode(folder, part, quality)
            found = problems(check(part), slots, duration)
            if found:
                failures.append(f"{variant}: {'; '.join(found)}")
                continue
            write(variant, part)
    return duration, failures


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--disc", type=Path, default=DISC)
    ap.add_argument("--out", type=Path, default=REMAKE / "remake/imported/movies")
    ap.add_argument("--only", nargs="*", default=None, help="movie names (title_0, ending_5, ...)")
    args = ap.parse_args(argv)
    sys.path.insert(0, str(REMAKE / "tools/remake_import"))
    import common  # noqa: PLC0415  (the remake's converter)
    import media  # noqa: PLC0415

    disc = common.Disc.open(args.disc).canonical()
    tool = media.find_ffmpeg()
    movies = movie_list(media, disc)
    expected = listed_names(args.out / "movies.json")
    if {name for name, _ in movies} != expected:
        print(f"the movie names differ from movies.json: {sorted(expected ^ {n for n, _ in movies})}", file=sys.stderr)
        return 1
    failed = 0
    for name, movie in movies:
        if args.only and name not in args.only:
            continue
        raw, arrivals = media._movie_sectors(disc, movie)

        def keep(variant: str, part: Path) -> None:
            dest = args.out / f"{name}.{variant}.mpg"
            shutil.move(part, dest)
            print(f"{dest.name}: {dest.stat().st_size / 1e6:.2f} MB")

        duration, failures = encode_movie(media, tool, raw, arrivals, QUALITIES, keep)
        for failure in failures:
            failed += 1
            print(f"FAILED {name} {failure}", file=sys.stderr)
        print(f"{name}: {duration:.3f} s")
    print("done" if not failed else f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
