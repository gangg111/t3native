"""tools/movies_from_disc.py: grid frames + sound to MPEG-1, checked by an independent decode.

Input the tool expects: a folder with `grid_%06d.png` (the remake's media._grid_frames, 30 fps)
and `audio.wav`. The tests make a miniature of that by hand (PIL frames, a written WAV) and read
the result back with ffprobe / ffmpeg, not with the tool's own code.
"""
import json
import struct
import subprocess
import sys
import wave
from pathlib import Path

import pytest
from PIL import Image

TOOL = Path(__file__).resolve().parents[2] / "tools" / "movies_from_disc.py"
sys.path.insert(0, str(TOOL.parent))
import movies_from_disc  # noqa: E402

FFMPEG = subprocess.run(["where", "ffmpeg"], capture_output=True, shell=True).returncode == 0


def _folder(tmp_path, frames=15, rate=37800, seconds=0.5):
    for i in range(frames):
        Image.new("RGB", (64, 48), (i * 16 % 256, 64, 128)).save(tmp_path / f"grid_{i:06d}.png")
    with wave.open(str(tmp_path / "audio.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        n = int(rate * seconds)
        w.writeframes(b"".join(struct.pack("<hh", (k * 37) % 2000, -(k * 37) % 2000) for k in range(n)))
    return tmp_path


@pytest.mark.skipif(not FFMPEG, reason="ffmpeg is not on the PATH")
def test_encode_mpeg1_mp2(tmp_path):
    folder = _folder(tmp_path)
    dest = tmp_path / "m.mpg"
    movies_from_disc.encode(folder, dest, 2)
    out = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-show_entries",
                          "stream=codec_name,sample_rate,nb_read_frames,width,height", "-of", "json", str(dest)],
                         capture_output=True, text=True, check=True).stdout
    streams = {s["codec_name"]: s for s in json.loads(out)["streams"]}
    # source: the 15 hand-made 64x48 frames, MP2 resampled to the 44 100 Hz the tool asks for
    assert streams["mpeg1video"]["nb_read_frames"] == "15"
    assert (streams["mpeg1video"]["width"], streams["mpeg1video"]["height"]) == (64, 48)
    assert streams["mp2"]["sample_rate"] == "44100"
    decode = subprocess.run(["ffmpeg", "-v", "error", "-i", str(dest), "-f", "null", "-"], capture_output=True, text=True)
    # source: an independent full decode reports nothing
    assert decode.stderr.strip() == ""


@pytest.mark.skipif(not FFMPEG, reason="ffmpeg is not on the PATH")
def test_check_counts_frames(tmp_path):
    folder = _folder(tmp_path, frames=30, seconds=1.0)
    dest = tmp_path / "m.mpg"
    movies_from_disc.encode(folder, dest, 5)
    c = movies_from_disc.check(dest)
    # source: 30 frames written by hand at 30 fps = 1 s
    assert c.frames == 30
    assert abs(c.duration - 1.0) < 0.1
    assert c.errors == []


def test_problems():
    ok = movies_from_disc.Check(frames=30, duration=1.0, errors=[])
    # source: the rules in the module docstring, by hand
    assert movies_from_disc.problems(ok, 30, 1.0) == []
    assert movies_from_disc.problems(ok, 31, 1.0) == ["30 frames, the grid has 31"]
    bad = movies_from_disc.Check(frames=30, duration=1.3, errors=["x"])
    assert len(movies_from_disc.problems(bad, 30, 1.0)) == 2


def test_listed_names(tmp_path):
    p = tmp_path / "movies.json"
    p.write_text(json.dumps({"title": [{"video": ["title_0.large.ogv", "title_0.small.ogv"]}],
                             "ending": [{"video": ["ending_3.large.ogv"]}, {"video": ["title_0.large.ogv"]}]}))
    # source: the names in the JSON above
    assert movies_from_disc.listed_names(p) == {"title_0", "ending_3"}
