"""builder/t3builder/apk_sign.py: alignment and v2 signatures, checked against the Android SDK's
own zipalign and apksigner (independent of the code under test), and against small known values.

Input: nothing but a tiny zip built here; the SDK checks are skipped where build-tools 36 or the
JetBrains Runtime are missing.
"""
import os
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "builder"))
from t3builder import apk_sign  # noqa: E402

SDK = Path(os.environ.get("ANDROID_HOME", Path.home() / "AppData/Local/Android/Sdk"))
BUILD_TOOLS = SDK / "build-tools/36.0.0"
JDK = Path(os.environ.get("T3_JDK", "C:/Program Files/Android/Android Studio/jbr"))
HAVE_SDK = (BUILD_TOOLS / "apksigner.bat").is_file() and (JDK / "bin/java.exe").is_file()


@pytest.fixture(scope="module")
def key():
    return apk_sign.new_key()


def small_apk(path: Path) -> None:
    z = apk_sign.AlignedZip(path)
    z.write_bytes("AndroidManifest.xml", b"<manifest/>" * 7, zipfile.ZIP_DEFLATED)
    z.write_bytes("assets/a.txt", b"x" * 3, zipfile.ZIP_STORED)
    z.write_bytes("lib/arm64-v8a/libt.so", b"\x7fELF" + b"\x01" * 5000, zipfile.ZIP_STORED)
    z.write_bytes("assets/b.png", bytes(range(256)) * 9000, zipfile.ZIP_STORED)   # > 1 MiB: two chunks
    z.close()


def sdk(tool: str, *args) -> subprocess.CompletedProcess:
    env = dict(os.environ, JAVA_HOME=str(JDK), PATH=str(JDK / "bin") + os.pathsep + os.environ["PATH"])
    return subprocess.run([str(BUILD_TOOLS / tool), *map(str, args)], capture_output=True, text=True, env=env)


def test_der_int_and_oid():
    # source: policzone ręcznie (X.690): 0x80 needs a leading zero; 1.2.840.113549 = 2a 86 48 86 f7 0d
    assert apk_sign.der_int(0x80) == bytes.fromhex("02020080")
    assert apk_sign.der_int(5) == bytes.fromhex("020105")
    assert apk_sign.der_oid("1.2.840.113549.1.1.1") == bytes.fromhex("06092a864886f70d010101")


def test_padding_puts_data_on_the_boundary():
    # source: policzone ręcznie: header at 100, name 5 bytes: 100 + 30 + 5 + 6 = 141, next multiple of 4 = 144
    extra = apk_sign._padding(100, b"a.txt", 4)
    assert len(extra) == 6 + 3
    assert (100 + 30 + 5 + len(extra)) % 4 == 0


def test_alignment_rules():
    # source: zipalign -P 16 4 (build-tools 36 help): stored .so on 16 KiB, other stored on 4, deflated free
    assert apk_sign.alignment("lib/x86_64/libt.so", True) == 16384
    assert apk_sign.alignment("assets/a.png", True) == 4
    assert apk_sign.alignment("classes.dex", False) == 0


def test_signature_round_trip(tmp_path, key):
    small_apk(tmp_path / "u.apk")
    assert apk_sign.check_alignment(tmp_path / "u.apk") == []
    apk_sign.sign(tmp_path / "u.apk", tmp_path / "s.apk", key)
    assert apk_sign.verify(tmp_path / "s.apk") == key.cert
    with zipfile.ZipFile(tmp_path / "s.apk") as z:
        assert z.testzip() is None
        assert z.read("assets/a.txt") == b"xxx"


def test_tampered_apk_fails(tmp_path, key):
    small_apk(tmp_path / "u.apk")
    apk_sign.sign(tmp_path / "u.apk", tmp_path / "s.apk", key)
    data = bytearray((tmp_path / "s.apk").read_bytes())
    offset = apk_sign.data_offsets(tmp_path / "s.apk")["assets/b.png"][0]
    data[offset + 2_000_000 % (256 * 9000)] ^= 1
    (tmp_path / "t.apk").write_bytes(bytes(data))
    with pytest.raises(apk_sign.SignError):
        apk_sign.verify(tmp_path / "t.apk")


def test_key_saved_and_reused(tmp_path):
    path = tmp_path / "klucz" / "key.json"
    first, made = apk_sign.load_or_create(path)
    again, made_again = apk_sign.load_or_create(path)
    assert made and not made_again
    assert (again.n, again.d, again.cert) == (first.n, first.d, first.cert)


@pytest.mark.skipif(not HAVE_SDK, reason="Android build-tools 36 / JetBrains Runtime missing")
def test_sdk_accepts_alignment_and_signature(tmp_path, key):
    small_apk(tmp_path / "u.apk")
    # source: zipalign -c (the SDK's own check), the same -P 16 4 as tools/build_apk.py
    check = sdk("zipalign.exe", "-c", "-P", "16", "4", tmp_path / "u.apk")
    assert check.returncode == 0, check.stdout + check.stderr
    apk_sign.sign(tmp_path / "u.apk", tmp_path / "s.apk", key)
    # source: apksigner verify (the SDK's verifier, the same checks as the package installer)
    result = sdk("apksigner.bat", "verify", "--min-sdk-version", "26", "-v", tmp_path / "s.apk")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "Verified using v2 scheme (APK Signature Scheme v2): true" in result.stdout


@pytest.mark.skipif(not HAVE_SDK, reason="Android build-tools 36 / JetBrains Runtime missing")
def test_sdk_rejects_tampered(tmp_path, key):
    small_apk(tmp_path / "u.apk")
    apk_sign.sign(tmp_path / "u.apk", tmp_path / "s.apk", key)
    data = bytearray((tmp_path / "s.apk").read_bytes())
    data[apk_sign.data_offsets(tmp_path / "s.apk")["assets/a.txt"][0]] ^= 1
    (tmp_path / "t.apk").write_bytes(bytes(data))
    result = sdk("apksigner.bat", "verify", "--min-sdk-version", "26", tmp_path / "t.apk")
    assert result.returncode != 0
