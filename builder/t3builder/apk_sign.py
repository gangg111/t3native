"""APK packing without the Android SDK: the zip with zipalign's alignment, and APK Signature
Scheme v2 with an RSA key of the builder's own, in plain Python (no third-party packages).

- Alignment: every stored entry's data starts at a multiple of 4, the native libraries' at a
  multiple of 16 KB (zipalign -P 16), by an extra field like zipalign's (id 0xD935).
- Key: RSA-2048 with e = 65537 and a self-signed X.509 certificate, made once (`new_key`) and kept
  by the caller; every APK signed with it installs over the previous one.
- Signature: v2 only (minSdk 26 > 24: no v1 needed), RSASSA-PKCS1-v1_5 with SHA-256 (id 0x0103),
  over the 1 MiB chunk digests of the entries, the central directory and the end record.
  `verify` checks a signed APK the same way the platform does; the tests check against apksigner.
"""
from __future__ import annotations

import base64
import hashlib
import json
import math
import secrets
import shutil
import struct
import zipfile
from dataclasses import dataclass
from pathlib import Path

ALIGN = 4
SO_ALIGN = 16384
ALIGN_EXTRA_ID = 0xD935
CHUNK = 1 << 20
V2_BLOCK_ID = 0x7109871A
SIG_RSA_PKCS1_SHA256 = 0x0103
MAGIC = b"APK Sig Block 42"
EOCD_SIG = 0x06054B50
E = 65537
KEY_BITS = 2048
# DER DigestInfo prefix of a SHA-256 digest (RFC 8017, 9.2 note 1).
SHA256_DIGEST_INFO = bytes.fromhex("3031300d060960864801650304020105000420")
OID_RSA = "1.2.840.113549.1.1.1"
OID_SHA256_RSA = "1.2.840.113549.1.1.11"
OID_CN = "2.5.4.3"


class SignError(Exception):
    pass


# DER

def _len(n: int) -> bytes:
    if n < 0x80:
        return bytes([n])
    body = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(body)]) + body


def _tlv(tag: int, body: bytes) -> bytes:
    return bytes([tag]) + _len(len(body)) + body


def der_int(v: int) -> bytes:
    body = v.to_bytes(v.bit_length() // 8 + 1, "big")      # a leading 0 keeps it positive
    return _tlv(0x02, body)


def der_seq(*items: bytes) -> bytes:
    return _tlv(0x30, b"".join(items))


def der_oid(dotted: str) -> bytes:
    parts = [int(p) for p in dotted.split(".")]
    body = bytearray([40 * parts[0] + parts[1]])
    for p in parts[2:]:
        chunk = [p & 0x7F]
        p >>= 7
        while p:
            chunk.append(0x80 | (p & 0x7F))
            p >>= 7
        body += bytes(reversed(chunk))
    return _tlv(0x06, bytes(body))


def _name(common_name: str) -> bytes:
    attr = der_seq(der_oid(OID_CN), _tlv(0x0C, common_name.encode()))
    return der_seq(_tlv(0x31, attr))


def _time(year: int) -> bytes:
    # UTCTime up to 2049, GeneralizedTime from 2050 (RFC 5280, 4.1.2.5).
    if year < 2050:
        return _tlv(0x17, f"{year % 100:02d}0101000000Z".encode())
    return _tlv(0x18, f"{year:04d}0101000000Z".encode())


def _alg(oid: str) -> bytes:
    return der_seq(der_oid(oid), b"\x05\x00")


# RSA

def _probable_prime(bits: int) -> int:
    while True:
        c = secrets.randbits(bits) | (1 << (bits - 1)) | (1 << (bits - 2)) | 1
        if c % E != 1 and _miller_rabin(c):
            return c


def _miller_rabin(n: int, rounds: int = 40) -> bool:
    for p in (3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d, s = n - 1, 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for _ in range(rounds):
        x = pow(secrets.randbelow(n - 3) + 2, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = pow(x, 2, n)
            if x == n - 1:
                break
        else:
            return False
    return True


@dataclass
class Key:
    n: int
    d: int
    cert: bytes            # DER X.509 certificate

    @property
    def size(self) -> int:
        return (self.n.bit_length() + 7) // 8

    def public_key_info(self) -> bytes:
        """SubjectPublicKeyInfo DER of the key."""
        rsa = der_seq(der_int(self.n), der_int(E))
        return der_seq(_alg(OID_RSA), _tlv(0x03, b"\x00" + rsa))

    def sign(self, data: bytes) -> bytes:
        return pow(_encoded(hashlib.sha256(data).digest(), self.size), self.d, self.n).to_bytes(self.size, "big")

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({"n": hex(self.n), "d": hex(self.d),
                                    "cert": base64.b64encode(self.cert).decode()}, indent=1) + "\n")

    @staticmethod
    def load(path: Path) -> "Key":
        data = json.loads(path.read_text())
        return Key(int(data["n"], 16), int(data["d"], 16), base64.b64decode(data["cert"]))


def _encoded(digest: bytes, size: int) -> int:
    """EMSA-PKCS1-v1_5 of a SHA-256 digest as an integer."""
    t = SHA256_DIGEST_INFO + digest
    return int.from_bytes(b"\x00\x01" + b"\xff" * (size - len(t) - 3) + b"\x00" + t, "big")


def new_key(common_name: str = "t3native", years: int = 30, bits: int = KEY_BITS) -> Key:
    """A fresh RSA key and its self-signed certificate (X.509 v1, valid `years` from 2026)."""
    while True:
        p, q = _probable_prime(bits // 2), _probable_prime(bits // 2)
        n = p * q
        if p != q and n.bit_length() == bits:
            break
    d = pow(E, -1, math.lcm(p - 1, q - 1))
    key = Key(n, d, b"")
    tbs = der_seq(der_int(secrets.randbits(63) + 1), _alg(OID_SHA256_RSA), _name(common_name),
                  der_seq(_time(2026), _time(2026 + years)), _name(common_name), key.public_key_info())
    key.cert = der_seq(tbs, _alg(OID_SHA256_RSA), _tlv(0x03, b"\x00" + key.sign(tbs)))
    return key


def load_or_create(path: Path) -> tuple[Key, bool]:
    """The key kept at `path`, made and saved there on first use; (key, whether it was made now)."""
    if path.is_file():
        return Key.load(path), False
    key = new_key()
    key.save(path)
    return key, True


# Zip

def _padding(offset: int, name: bytes, align: int) -> bytes:
    """The extra field that puts the data of an entry whose local header starts at `offset` on
    `align`: id 0xD935, its size, the alignment, then zeros."""
    data = offset + 30 + len(name) + 6
    pad = -data % align
    return struct.pack("<HHH", ALIGN_EXTRA_ID, 2 + pad, align) + b"\x00" * pad


def alignment(name: str, stored: bool) -> int:
    if not stored:
        return 0
    return SO_ALIGN if name.endswith(".so") else ALIGN


class AlignedZip:
    """A zip written entry by entry with the stored entries aligned (`alignment`)."""

    def __init__(self, path: Path):
        self.zip = zipfile.ZipFile(path, "w", allowZip64=True)

    def _info(self, name: str, compress: int, size: int, date_time) -> zipfile.ZipInfo:
        info = zipfile.ZipInfo(name, date_time=date_time)
        info.compress_type = compress
        info.file_size = size
        align = alignment(name, compress == zipfile.ZIP_STORED)
        if align:
            info.extra = _padding(self.zip.fp.tell(), name.encode(), align)
        return info

    def write_bytes(self, name: str, data: bytes, compress: int, date_time=(2026, 1, 1, 0, 0, 0)) -> None:
        with self.zip.open(self._info(name, compress, len(data), date_time), "w") as dst:
            dst.write(data)

    def write_file(self, name: str, path: Path, compress: int, date_time=(2026, 1, 1, 0, 0, 0)) -> None:
        with open(path, "rb") as src, \
                self.zip.open(self._info(name, compress, path.stat().st_size, date_time), "w") as dst:
            shutil.copyfileobj(src, dst, CHUNK)

    def close(self) -> None:
        self.zip.close()


def data_offsets(path: Path) -> dict[str, tuple[int, int]]:
    """Each entry's (data offset, compression) from its local header."""
    found = {}
    with zipfile.ZipFile(path) as z, open(path, "rb") as f:
        for info in z.infolist():
            f.seek(info.header_offset)
            head = f.read(30)
            name_len, extra_len = struct.unpack_from("<HH", head, 26)
            found[info.filename] = (info.header_offset + 30 + name_len + extra_len, info.compress_type)
    return found


def check_alignment(path: Path) -> list[str]:
    """The entries whose data is not aligned as `alignment` asks (empty when all are)."""
    bad = []
    for name, (offset, compress) in data_offsets(path).items():
        align = alignment(name, compress == zipfile.ZIP_STORED)
        if align and offset % align:
            bad.append(f"{name} at {offset}")
    return bad


# APK Signature Scheme v2

def _lp(data: bytes) -> bytes:
    return struct.pack("<I", len(data)) + data


def _eocd(data: bytes) -> int:
    """The offset of the end of central directory record (the last one, comment of any length)."""
    for pos in range(len(data) - 22, max(-1, len(data) - 22 - 0xFFFF - 1), -1):
        if struct.unpack_from("<I", data, pos)[0] == EOCD_SIG and \
                struct.unpack_from("<H", data, pos + 20)[0] == len(data) - pos - 22:
            return pos
    raise SignError("no end of central directory record")


def _content_digest(sections: list[bytes]) -> bytes:
    digests = []
    for section in sections:
        for start in range(0, len(section), CHUNK):
            chunk = section[start:start + CHUNK]
            digests.append(hashlib.sha256(b"\xa5" + struct.pack("<I", len(chunk)) + chunk).digest())
    return hashlib.sha256(b"\x5a" + struct.pack("<I", len(digests)) + b"".join(digests)).digest()


def _split(data: bytes) -> tuple[int, int, int]:
    """(central directory offset, its size, end record offset) of an unsigned zip."""
    eocd = _eocd(data)
    cd_size, cd_offset = struct.unpack_from("<II", data, eocd + 12)
    if cd_offset + cd_size != eocd:
        raise SignError("the central directory does not end at the end record (zip64 or a signed APK?)")
    return cd_offset, cd_size, eocd


def sign(unsigned: Path, out: Path, key: Key) -> None:
    """`unsigned` (an aligned zip) signed with v2 into `out`."""
    data = unsigned.read_bytes()
    cd_offset, _, eocd = _split(data)
    entries, central, end = data[:cd_offset], data[cd_offset:eocd], data[eocd:]
    digest = _content_digest([entries, central, end])          # the end record points at cd_offset
    signed_data = _lp(_lp(struct.pack("<I", SIG_RSA_PKCS1_SHA256) + _lp(digest))) \
        + _lp(_lp(key.cert)) + _lp(b"")
    signature = _lp(_lp(struct.pack("<I", SIG_RSA_PKCS1_SHA256) + _lp(key.sign(signed_data))))
    signer = _lp(signed_data) + signature + _lp(key.public_key_info())
    value = _lp(_lp(signer))
    pair = struct.pack("<QI", 4 + len(value), V2_BLOCK_ID) + value
    size = len(pair) + 8 + 16                                   # pairs, the second size, the magic
    block = struct.pack("<Q", size) + pair + struct.pack("<Q", size) + MAGIC
    end = bytearray(end)
    struct.pack_into("<I", end, 16, cd_offset + len(block))
    tmp = out.with_name(out.name + ".part")
    with open(tmp, "wb") as f:
        f.write(entries)
        f.write(block)
        f.write(central)
        f.write(end)
    tmp.replace(out)


def _read_lp(data: bytes, pos: int) -> tuple[bytes, int]:
    n = struct.unpack_from("<I", data, pos)[0]
    if pos + 4 + n > len(data):
        raise SignError("a length-prefixed field runs past its end")
    return data[pos + 4:pos + 4 + n], pos + 4 + n


def _lp_list(data: bytes) -> list[bytes]:
    items, pos = [], 0
    while pos < len(data):
        item, pos = _read_lp(data, pos)
        items.append(item)
    return items


def _public_modulus(info: bytes) -> tuple[int, int]:
    """(n, e) of an RSA SubjectPublicKeyInfo as `Key.public_key_info` writes it."""
    def tlv(buf: bytes, pos: int) -> tuple[int, bytes, int]:
        tag, n = buf[pos], buf[pos + 1]
        pos += 2
        if n & 0x80:
            k = n & 0x7F
            n = int.from_bytes(buf[pos:pos + k], "big")
            pos += k
        return tag, buf[pos:pos + n], pos + n
    _, spki, _ = tlv(info, 0)
    _, _, pos = tlv(spki, 0)                       # the algorithm
    _, bits, _ = tlv(spki, pos)
    _, rsa, _ = tlv(bits[1:], 0)
    _, n, pos = tlv(rsa, 0)
    _, e, _ = tlv(rsa, pos)
    return int.from_bytes(n, "big"), int.from_bytes(e, "big")


def verify(apk: Path) -> bytes:
    """Checks the v2 signature of `apk` as the platform does; returns the signer's certificate."""
    data = apk.read_bytes()
    eocd = _eocd(data)
    cd_size, cd_offset = struct.unpack_from("<II", data, eocd + 12)
    if data[cd_offset - 16:cd_offset] != MAGIC:
        raise SignError("no APK signing block")
    size = struct.unpack_from("<Q", data, cd_offset - 24)[0]
    start = cd_offset - size - 8
    if struct.unpack_from("<Q", data, start)[0] != size:
        raise SignError("the signing block's two sizes differ")
    pos, value = start + 8, None
    while pos < cd_offset - 24:
        n, pid = struct.unpack_from("<QI", data, pos)
        if pid == V2_BLOCK_ID:
            value = data[pos + 12:pos + 8 + n]
        pos += 8 + n
    if value is None:
        raise SignError("no v2 signature")
    signers = _lp_list(_read_lp(value, 0)[0])
    if len(signers) != 1:
        raise SignError(f"{len(signers)} signers")
    signed_data, pos = _read_lp(signers[0], 0)
    signatures, pos = _read_lp(signers[0], pos)
    public, _ = _read_lp(signers[0], pos)
    n, e = _public_modulus(public)
    algo_sig = _lp_list(signatures)[0]
    algo = struct.unpack_from("<I", algo_sig)[0]
    signature = _read_lp(algo_sig, 4)[0]
    if algo != SIG_RSA_PKCS1_SHA256:
        raise SignError(f"signature algorithm {algo:#x}")
    size_bytes = (n.bit_length() + 7) // 8
    if pow(int.from_bytes(signature, "big"), e, n) != _encoded(hashlib.sha256(signed_data).digest(), size_bytes):
        raise SignError("the signature does not match the signed data")
    digests, pos = _read_lp(signed_data, 0)
    certs, _ = _read_lp(signed_data, pos)
    algo_digest = _lp_list(digests)[0]
    digest = _read_lp(algo_digest, 4)[0]
    end = bytearray(data[eocd:])
    struct.pack_into("<I", end, 16, start)
    if _content_digest([data[:start], data[cd_offset:eocd], bytes(end)]) != digest:
        raise SignError("the APK's contents do not match the signed digest")
    return _lp_list(certs)[0]
