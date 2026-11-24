#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""selftest.py - device-free unit/integration selftest for the wifi-bench-tooling lane.

Lane: wifi-bench-tooling (shared task task-97).  HOST-ONLY, NO DEVICE:
everything here runs against synthetic data in a temporary directory
(synthetic ELF module, synthetic 4 KiB / 928,920 B firmware blobs, synthetic
custom-block files, synthetic report fixtures).  It never reads the vendor
firmware blob and never contacts the router.

Covers:
  * bench_guards: forbidden CA 0x400392F0, CECE latch/policy, release 0x5A5A,
    devmem census, fw_path module/firmware validation, ELF module parsing,
    H2D one-shot gate, static device-script audit, sha256-chained write audit,
    host TFTP/UART/evidence checks;
  * fw_patch_builder: synthetic recipe apply/verify/manifest/refusals + the
    builder's own --selftest CLI;
  * bench_session: plan generation for all four modes, generated device
    script is valid POSIX shell (sh -n) + audit-clean, dry-run CLI, synthetic
    report parser/classifier, and an end-to-end execute() against a fake
    transport (golden and stop fixtures).

Usage:
    ./tools/run-selftest.sh
    python3 tools/selftest.py [-v]
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

TOOLDIR = os.path.dirname(os.path.abspath(__file__))
if TOOLDIR not in sys.path:
    sys.path.insert(0, TOOLDIR)

import bench_guards as G          # noqa: E402
import fw_patch_builder as FWB    # noqa: E402
import bench_session as BS        # noqa: E402
import protocol_sequence as PS    # noqa: E402

ROOT = BS.ROOT
CHECKS = []
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv


def ck(name: str, ok: bool, detail: str = "") -> None:
    CHECKS.append((name, bool(ok), detail))
    if VERBOSE:
        print("%s %-70s %s" % ("PASS" if ok else "FAIL", name, detail))


def expect_guard(label: str, fn, code: str) -> None:
    try:
        fn()
    except G.GuardError as e:
        ck("refuse: %s [%s]" % (label, e.code), e.code == code or code == "*",
           "got %s" % e.code)
        return
    except Exception as e:  # noqa: BLE001
        ck("refuse: %s" % label, False, "unexpected %r" % (e,))
        return
    ck("refuse: %s" % label, False, "no exception (expected %s)" % code)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# ---------------------------------------------------------------------------
# Synthetic fixtures
# ---------------------------------------------------------------------------
def make_fake_ko(path: str, name: str = "hi5622_pciet_hcc",
                 vermagic: str = "6.18.60 SMP mod_unload ARMv7 p2v8",
                 license_: str = "GPL", machine: int = 40, etype: int = 1,
                 params=("fw_path:charp", "poll_ms:uint")) -> None:
    """Write a minimal but valid ELF32 ARM ET_REL with a .modinfo section."""
    fields = ["name=%s" % name, "vermagic=%s" % vermagic,
              "license=%s" % license_]
    fields += ["parm=%s" % p for p in params]
    fields += ["depends=", "description=selftest synthetic module"]
    modinfo = b"".join(f.encode() + b"\x00" for f in fields)
    shstrtab = b"\x00.shstrtab\x00.modinfo\x00"
    off_shstr = 52
    off_mod = off_shstr + len(shstrtab)
    off_sh = (off_mod + len(modinfo) + 3) & ~3
    data = bytearray(off_sh + 3 * 40)
    data[0:16] = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\x00" * 8
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, etype, machine, 1, 0, 0,
                     off_sh, 0, 52, 0, 0, 40, 3, 1)
    data[off_shstr:off_shstr + len(shstrtab)] = shstrtab
    data[off_mod:off_mod + len(modinfo)] = modinfo
    # section 0: null
    # section 1: .shstrtab
    struct.pack_into("<IIIIIIIIII", data, off_sh + 40, 1, 3, 0, 0,
                     off_shstr, len(shstrtab), 0, 0, 1, 0)
    # section 2: .modinfo
    struct.pack_into("<IIIIIIIIII", data, off_sh + 80, 11, 1, 0, 0,
                     off_mod, len(modinfo), 0, 0, 1, 0)
    with open(path, "wb") as f:
        f.write(data)


def make_fake_bar0read(path: str) -> str:
    """Write a synthetic static ARM helper (ELF32 LE ET_EXEC, > 4 KiB)."""
    make_fake_ko(path, name="hi5622_bar0read", etype=2)
    with open(path, "ab") as f:
        f.write(b"\x00" * 8192)
    os.chmod(path, 0o755)
    return path


def synth_bytes(n: int, seed: int = 131, mod: int = 4096) -> bytes:
    return bytes(((i * seed + 7) ^ (i >> 8)) & 0xFF for i in range(n))


def write_block_file(path: str, words: int, base_seed: int = 1) -> None:
    with open(path, "w", encoding="utf-8") as f:
        for i in range(words):
            f.write("%d=0x%08x\n" % (i, (i * 0x01010101 + base_seed) & 0xFFFFFFFF))


def synth_recipe(name: str, blob: bytes) -> dict:
    segs = []
    for off, ln, pat in ((0x100, 2, b"\x00\x20"), (0x200, 4, b"\xfe\xf7\x51\xba")):
        segs.append({"file": off, "len": ln,
                     "source_sha16": sha256_bytes(blob[off:off + ln])[:16],
                     "patched_hex": pat.hex(), "isa": "thumb", "expect": ""})
    return {"schema": FWB.SCHEMA_RECIPE, "name": name, "about": "selftest synthetic",
            "source": {"sha256": sha256_bytes(blob), "len": len(blob)},
            "based_on": None, "segments": segs}


def make_patched_fw(tmp: str, name: str = "FIRMWARE.PD2.bin") -> tuple:
    """Build a synthetic patched firmware + manifest via the real builder code."""
    src_dir = os.path.join(tmp, "fw")
    os.makedirs(src_dir, exist_ok=True)
    src_path = os.path.join(src_dir, "FIRMWARE.bin")
    blob = synth_bytes(4096)
    with open(src_path, "wb") as f:
        f.write(blob)
    rec_doc = synth_recipe("PD2", blob)
    recs = {"PD2": FWB.normalize_recipe(rec_doc, "selftest")}
    patched, segments = FWB.apply_recipes(blob, recs, ["PD2"])
    out_dir = os.path.join(tmp, "patched")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, name)
    with open(out_path, "wb") as f:
        f.write(patched)
    src_info = {"path": src_path, "name": "FIRMWARE.bin",
                "sha256": sha256_bytes(blob), "size": len(blob),
                "role": "unverified-allowed", "warnings": []}
    entry = {"patch": "PD2", "about": "selftest", "chain": ["PD2"],
             "deploy_name": name, "out": out_path,
             "out_sha256": sha256_bytes(patched), "out_len": len(patched),
             "segments": segments, "warnings": []}
    manifest = os.path.join(out_dir, "manifest.json")
    FWB.write_manifest(manifest, src_info, [entry])
    return src_path, out_path, manifest


def synth_env(tmp: str) -> dict:
    env = {}
    env["module"] = os.path.join(tmp, "hi5622_pciet_hcc.ko")
    make_fake_ko(env["module"])
    env["vendor_fw"] = os.path.join(tmp, "FIRMWARE.bin")
    with open(env["vendor_fw"], "wb") as f:
        f.write(synth_bytes(928920))
    env["plat_block"] = os.path.join(tmp, "plat_block.txt")
    write_block_file(env["plat_block"], 24, 3)
    env["wifi_block"] = os.path.join(tmp, "wifi_block.txt")
    write_block_file(env["wifi_block"], 618, 5)
    env["patched_fw"], env["fw_file"], env["manifest"] = make_patched_fw(tmp)
    return env


def base_opts(mode: str, env: dict, tmp: str, **kw) -> BS.SessionOptions:
    opts = BS.SessionOptions(
        mode=mode, module=env["module"], dry_run=True,
        plat_block=env["plat_block"], wifi_block=env["wifi_block"],
        report_dir=os.path.join(tmp, "reports", mode),
        allow_other_source=True,
        announce=(mode == "h2d"))
    opts.fw_file = env["vendor_fw"]
    for k, v in kw.items():
        setattr(opts, k, v)
    return opts


# ---------------------------------------------------------------------------
# A. bench_guards unit tests
# ---------------------------------------------------------------------------
def test_guards(tmp: str) -> None:
    guard = G.WriteGuard()
    expect_guard("CA out[5] write",
                 lambda: guard.check_ca_write(0x400392F0, 1), "forbidden-ca-write")
    expect_guard("CA out[5] write alternate case",
                 lambda: guard.check_ca_write("0x400392f0", 0), "forbidden-ca-write")
    expect_guard("CECE before latch",
                 lambda: guard.check_ca_write(G.CA_D2H_GATE, G.CECE_MAGIC),
                 "cece-gate-refused")
    expect_guard("release magic",
                 lambda: guard.check_ca_write(G.CA_CPU_RELEASE, 0xCECE), "release-magic")
    expect_guard("CA not in census",
                 lambda: guard.check_ca_write(0x40039010, 1), "ca-not-allowed")
    guard.check_ca_write(G.CA_CPU_RELEASE, G.RELEASE_MAGIC)
    ck("allow: release 0x5A5A", True)

    latch = G.CeceLatch()
    ctx = G.GuardContext(cece=latch, cece_mode=0)
    g2 = G.WriteGuard(ctx)
    latch.observe_page([0x86807FEF, 7, 2, 0xABCD], "unit")
    ck("latch: +8==2 latches", latch.latched)
    g2.check_ca_write(G.CA_D2H_GATE, G.CECE_MAGIC)
    ck("latch: CECE allowed after latch (mode 0)", latch.kicks == 1)
    g2.check_ca_write(G.CA_D2H_GATE, G.CECE_MAGIC)
    g2.check_ca_write(G.CA_D2H_GATE, G.CECE_MAGIC)
    expect_guard("CECE kick limit (4th)",
                 lambda: g2.check_ca_write(G.CA_D2H_GATE, G.CECE_MAGIC),
                 "cece-gate-refused")
    ck("latch: kick count capped", latch.kicks == 3)
    latch2 = G.CeceLatch()
    latch2.observe_page([0x86807FEF, 7, 2, 0xABCD], "unit")
    latch2.observe_page([0x0010C190, 0, 0, 0x1BEE50], "clobber")
    ck("latch: stays latched after page clobber", latch2.latched)
    ck("latch: page_clobbered detected", latch2.page_clobbered())
    ck("latch: policy satisfied after clobber", latch2.allow_cece(0)[0])
    g3 = G.WriteGuard(G.GuardContext(cece_mode=1))
    g3.ctx.cece.observe_page([0x86807FEF, 7, 2, 0xABCD])
    expect_guard("CECE mode 1 never writes",
                 lambda: g3.check_ca_write(G.CA_D2H_GATE, G.CECE_MAGIC),
                 "cece-gate-refused")

    expect_guard("devmem outside block windows",
                 lambda: guard.check_devmem_write(0x40000000, 1), "devmem-not-allowed")
    expect_guard("devmem at out[5] CA",
                 lambda: guard.check_devmem_write(0x400392F0, 1), "forbidden-ca-write")
    guard.check_devmem_write(G.PHYS_PLATFORM_BLOCK, 0)
    guard.check_devmem_write(G.PHYS_WIFI_BLOCK + 4 * (G.WIFI_BLOCK_WORDS - 1), 0)
    ck("allow: devmem inside custom-block windows", True)

    expect_guard("fw_path absolute", lambda: G.validate_fw_path("/root/FIRMWARE.PD.bin"),
                 "fw-path-absolute")
    expect_guard("fw_path dotdot", lambda: G.validate_fw_path("hi_wifi/../x.bin"),
                 "fw-path-dotdot")
    expect_guard("fw_path charset", lambda: G.validate_fw_path("hi_wifi/FIRM WARE.bin"),
                 "fw-path-charset")
    ck("fw_path relative ok",
       G.validate_fw_path("hi_wifi/FIRMWARE.PD.bin") == "hi_wifi/FIRMWARE.PD.bin")
    ck("patched name detected", G.is_patched_fw_name("FIRMWARE.PD2.bin"))
    ck("vendor name detected", G.is_vendor_fw_name("hi_wifi/FIRMWARE.bin"))
    expect_guard("patched mode rejects vendor name",
                 lambda: G.validate_fw_path_for_mode("hi_wifi/FIRMWARE.bin", patched=True),
                 "fw-name-not-patched")
    expect_guard("golden mode rejects patched name",
                 lambda: G.validate_fw_path_for_mode("hi_wifi/FIRMWARE.PD.bin", patched=False),
                 "fw-name-not-vendor")

    # vendor firmware pin
    bad_pin = os.path.join(tmp, "FIRMWARE.bin")
    with open(bad_pin, "wb") as f:
        f.write(synth_bytes(928920))
    expect_guard("vendor pin mismatch", lambda: G.validate_vendor_fw(bad_pin), "fw-pin-mismatch")
    info = G.validate_vendor_fw(bad_pin, allow_other_source=True)
    ck("vendor pin mismatch allowed explicitly", info.role == "unverified-allowed")
    good = os.path.join(tmp, "good_fw")
    os.makedirs(good, exist_ok=True)
    fake = os.path.join(good, "not_pinned.bin")
    with open(fake, "wb") as f:
        f.write(b"x" * 16)
    expect_guard("unpinned fw name", lambda: G.validate_vendor_fw(fake), "fw-unpinned")

    # module ELF validation
    ko = os.path.join(tmp, "hi5622_pciet_hcc.ko")
    make_fake_ko(ko)
    mi = G.validate_module(ko, require_fw_path=True)
    ck("module: parsed name/vermagic/license",
       mi.name == "hi5622_pciet_hcc" and "6.18.60" in mi.vermagic
       and mi.license == "GPL")
    ck("module: fw_path param parsed", "fw_path" in mi.params)
    badko = os.path.join(tmp, "hi5622_pciet_hcc2.ko")
    make_fake_ko(badko, machine=183)
    expect_guard("module non-ARM", lambda: G.validate_module(badko), "module-not-arm")
    badko2 = os.path.join(tmp, "hi5622_pciet_hcc3.ko")
    make_fake_ko(badko2, license_="GPL v2")
    expect_guard("module license", lambda: G.validate_module(badko2), "module-license")
    badko3 = os.path.join(tmp, "hi5622_pciet_hcc4.ko")
    make_fake_ko(badko3, name="hi5622_evil")
    expect_guard("module name allowlist", lambda: G.validate_module(badko3), "module-name")
    badko4 = os.path.join(tmp, "hi5622_pciet_hcc5.ko")
    make_fake_ko(badko4, vermagic="6.1.0 SMP mod_unload ARMv7")
    expect_guard("module vermagic", lambda: G.validate_module(badko4), "module-vermagic")
    badko5 = os.path.join(tmp, "hi5622_pciet_hcc6.ko")
    make_fake_ko(badko5, params=("poll_ms:uint",))
    expect_guard("module missing fw_path",
                 lambda: G.validate_module(badko5, require_fw_path=True),
                 "module-no-fw-path")

    # H2D one-shot gate
    h = G.H2dGate()
    ok, _ = h.may_announce()
    ck("h2d: first announce allowed", ok)
    h.note_attempt()
    ok2, why = h.may_announce()
    ck("h2d: second announce refused", not ok2, why)
    expect_guard("h2d: retry raises", h.note_attempt, "h2d-retry")
    h.note_result("timeout", "no consumption")
    ck("h2d: timeout closes lane", not h.may_announce()[0])
    ck("h2d: payload refused by default", not h.may_payload(False)[0])
    ck("h2d: payload allowed explicitly", h.may_payload(True)[0])

    # attribute policy
    guard2 = G.WriteGuard()
    expect_guard("attr: legacy announce store",
                 lambda: guard2.check_attr_write("announce", "1"), "attr-refused")
    expect_guard("attr: unknown", lambda: guard2.check_attr_write("write_something"),
                 "attr-unknown")
    expect_guard("attr: read-only store", lambda: guard2.check_attr_write("status", "1"),
                 "attr-readonly")
    expect_guard("attr: cece kick before latch",
                 lambda: guard2.check_attr_write("hcc_cece_kick", "1"),
                 "cece-gate-refused")
    expect_guard("attr: announce payload",
                 lambda: guard2.check_attr_write("hcc_sr_announce", "payload=00"),
                 "h2d-payload")
    guard2.check_attr_write("hcc_sr_announce", "1")
    ck("attr: one-shot announce store policy passes", True)
    expect_guard("attr: second announce store",
                 lambda: guard2.check_attr_write("hcc_sr_announce", "1"), "h2d-retry")

    # static device-script audit
    good_script = (
        "#!/bin/sh\n"
        "set -u\n"
        "w_attr() { a=$1; v=$2; wlog \"WRITE attr=$a value=$v\";"
        " printf '%s' \"$v\" > \"$P/$a\"; }\n"
        "say() { printf '%s\\n' \"$*\" >> \"$R\"; }\n"
        "w_attr release 1 release\n"
        "say 'GATE no-cece-default PASS'\n"
        "say 'SESSION_RESULT: PASS'\n")
    r = G.audit_device_script(good_script)
    ck("audit: clean script PASS", r["ok"], json.dumps(r["violations"][:2]))

    def audit_bad(label, script, code):
        rr = G.audit_device_script(script)
        codes = [v["code"] for v in rr["violations"]]
        ck("audit catches: %s" % label, (not rr["ok"]) and (code in codes),
           "codes=%s" % codes)

    audit_bad("devmem out[5]", good_script + "devmem 0x400392f0 32 0x1\n",
              "forbidden-ca-write")
    audit_bad("devmem outside window", good_script + "devmem 0x40000000 32 0x1\n",
              "devmem-not-allowed")
    audit_bad("forbidden RC misc read", good_script + "devmem 0x10161000 32\n",
              "forbidden-read")
    audit_bad("rmmod", good_script + "rmmod hi5622_pciet_hcc\n", "rmmod")
    audit_bad("UART write", good_script + "echo hi > /dev/ttyUSB0\n", "uart-write")
    audit_bad("two announces",
              good_script + "w_attr hcc_sr_announce 1 h1\nw_attr hcc_sr_announce 1 h2\n",
              "h2d-retry")
    audit_bad("raw CECE devmem",
              good_script + "CE=0x0000CECE; devmem 0x4000010c 32 $CE\n",
              "cece-raw")
    audit_bad("read-only store", good_script.replace(
        "w_attr release 1 release", "printf 1 > \"$P/status\"\n"), "attr-readonly")
    audit_bad("missing result", good_script.replace("SESSION_RESULT: PASS", ""),
              "no-result")
    # loop-form devmem writes (the generated custom-block seed form)
    loop_ok = good_script + (
        "PLAT_BASE=0x4086A800\n"
        "addr=$((PLAT_BASE + idx * 4))\n"
        'devmem "$addr" 32 "$val"\n')
    rr = G.audit_device_script(loop_ok)
    ck("audit: loop-form block devmem PASS", rr["ok"] and rr["stats"]["devmem_writes"] == 1,
       json.dumps(rr["violations"][:2]))
    loop_bad = good_script + (
        "BAD_BASE=0x40000000\n"
        "addr=$((BAD_BASE + idx * 4))\n"
        'devmem "$addr" 32 "$val"\n')
    audit_bad("loop-form devmem outside windows", loop_bad, "devmem-not-allowed")
    loop_unknown = good_script + (
        "addr=$((UNKNOWN_BASE + idx * 4))\n"
        'devmem "$addr" 32 "$val"\n')
    audit_bad("loop-form devmem unresolved base", loop_unknown, "devmem-unresolved")

    # write audit chain
    audit = G.WriteAudit()
    audit.record("s1", "attr", "prepare", "1")
    audit.record("s2", "devmem", "0x4086A800", "0x0", ca=G.PHYS_PLATFORM_BLOCK)
    audit.record("s3", "host-deploy", "/root/x.ko", "sha", policy="host")
    ok, why = G.verify_audit_chain(audit.records)
    ck("audit chain verifies", ok, why)
    tampered = [dict(r) for r in audit.records]
    tampered[1]["value"] = "0x1"
    ok2, _ = G.verify_audit_chain(tampered)
    ck("audit chain detects tamper", not ok2)

    # host process checks
    ok_t, probs, notes = G.check_tftp_processes(
        ["python3 tools/tftp_server.py --root build/x --allow-wrq"])
    ck("tftp: --allow-wrq flagged", (not ok_t) and probs)
    ok_t2, probs2, notes2 = G.check_tftp_processes(
        ["python3 tools/tftp_server.py --root build/linux-kbuild/http-root"])
    ck("tftp: RRQ-only accepted", ok_t2, str(notes2))
    ok_t3, probs3, _ = G.check_tftp_processes(
        ["python3 tftp_server.py " + ROOT + "/build"])
    ck("tftp: serving pattern flagged", not ok_t3, str(probs3))

    # uart capture detection with a pidfile for this process
    uart_dir = os.path.join(tmp, "uart-logs")
    os.makedirs(uart_dir, exist_ok=True)
    with open(os.path.join(uart_dir, "capture.pid"), "w") as f:
        f.write(str(os.getpid()))
    caps = G.detect_uart_captures(uart_dir)
    ck("uart: live capture detected", bool(caps), str(caps))
    os.unlink(os.path.join(uart_dir, "capture.pid"))
    ck("uart: no capture when no pidfile", not G.detect_uart_captures(uart_dir))

    # evidence dir policy
    ok_build, _ = G.gitignored_evidence_ok(os.path.join(ROOT, "build", "wifi-bench", "x"))
    ck("evidence: build/ accepted", ok_build)
    ok_docs, _ = G.gitignored_evidence_ok(os.path.join(ROOT, "docs", "x"))
    ck("evidence: tracked dir refused", not ok_docs)
    expect_guard("fw builder output inside repo",
                 lambda: G.refuse_output_inside_repo(os.path.join(ROOT, "docs")),
                 "out-inside-repo")


# ---------------------------------------------------------------------------
# B. fw_patch_builder CLI tests
# ---------------------------------------------------------------------------
def test_fw_builder(tmp: str) -> None:
    lane_dir = os.path.normpath(os.path.join(TOOLDIR, "..", "..", "wifi-irq-unmask"))
    if os.path.isdir(lane_dir):
        names = [json.load(open(f)).get("name")
                 for f in FWB.default_recipe_files()]
        ck("builder: sibling-lane recipes auto-discovered",
           {"PD2", "PD3", "PD3B"}.issubset(set(names)), str(names))
    ck("builder: PD3/PD3B fw names are deployable",
       G.is_patched_fw_name("FIRMWARE.PD3.bin")
       and G.is_patched_fw_name("FIRMWARE.PD3B.bin"))
    r = subprocess.run([sys.executable, os.path.join(TOOLDIR, "fw_patch_builder.py"),
                        "--selftest"], capture_output=True, text=True, timeout=120)
    ck("fw builder --selftest rc=0", r.returncode == 0, r.stdout.strip()[-200:])
    ck("fw builder selftest PASS", "PASS" in r.stdout, r.stdout.strip()[-120:])

    src = os.path.join(tmp, "synth_fw", "FIRMWARE.bin")
    os.makedirs(os.path.dirname(src), exist_ok=True)
    blob = synth_bytes(4096)
    with open(src, "wb") as f:
        f.write(blob)
    rec_path = os.path.join(tmp, "synth_fw", "recipe.json")
    with open(rec_path, "w") as f:
        json.dump(synth_recipe("SYNPD", blob), f)
    outdir = os.path.join(tmp, "synth_fw", "out")
    base = [sys.executable, os.path.join(TOOLDIR, "fw_patch_builder.py"),
            "--fw", src, "--out", outdir, "--recipe-file", rec_path,
            "--allow-other-source", "--no-decode"]
    r = subprocess.run(base + ["--build", "SYNPD"], capture_output=True,
                       text=True, timeout=120)
    ck("fw builder synthetic build rc=0", r.returncode == 0, (r.stdout + r.stderr)[-300:])
    built = os.path.join(outdir, "FIRMWARE.SYNPD.bin")
    ck("fw builder output exists", os.path.isfile(built))
    manifest = os.path.join(outdir, "manifest.json")
    ck("fw builder manifest exists", os.path.isfile(manifest))
    if os.path.isfile(manifest):
        doc = json.load(open(manifest))
        ck("fw builder manifest schema", doc.get("schema") == FWB.SCHEMA_MANIFEST)
        ck("fw builder manifest pins output sha",
           doc["builds"][0]["out_sha256"] == sha256_bytes(open(built, "rb").read()))
    r = subprocess.run(base + ["--verify", built, "--patch", "SYNPD"],
                       capture_output=True, text=True, timeout=120)
    ck("fw builder verify synthetic rc=0", r.returncode == 0, (r.stdout + r.stderr)[-300:])
    # tamper
    tampered = built + ".tampered"
    data = bytearray(open(built, "rb").read())
    data[0x800] ^= 0xFF
    open(tampered, "wb").write(data)
    r = subprocess.run(base + ["--verify", tampered, "--patch", "SYNPD"],
                       capture_output=True, text=True, timeout=120)
    ck("fw builder tamper detected", r.returncode == 1 and "REFUSED" in r.stderr,
       (r.stdout + r.stderr)[-200:])
    # verify-manifest
    r = subprocess.run(base + ["--verify-manifest", manifest],
                       capture_output=True, text=True, timeout=120)
    ck("fw builder verify-manifest rc=0", r.returncode == 0,
       (r.stdout + r.stderr)[-300:])
    # output inside the repo refused
    bad_out = os.path.join(ROOT, "docs", "incoming", "should-not-exist")
    r = subprocess.run([sys.executable, os.path.join(TOOLDIR, "fw_patch_builder.py"),
                        "--fw", src, "--out", bad_out, "--recipe-file", rec_path,
                        "--allow-other-source", "--no-decode", "--build", "SYNPD"],
                       capture_output=True, text=True, timeout=120)
    ck("fw builder refuses repo output", r.returncode == 1 and "out-inside-repo" in r.stderr,
       (r.stdout + r.stderr)[-200:])
    # dry-run writes nothing
    dry_out = os.path.join(tmp, "synth_fw", "dryout")
    r = subprocess.run([sys.executable, os.path.join(TOOLDIR, "fw_patch_builder.py"),
                        "--fw", src, "--out", dry_out, "--recipe-file", rec_path,
                        "--allow-other-source", "--no-decode", "--build", "SYNPD",
                        "--dry-run"], capture_output=True, text=True, timeout=120)
    ck("fw builder dry-run rc=0", r.returncode == 0, (r.stdout + r.stderr)[-200:])
    ck("fw builder dry-run writes nothing",
       not os.path.exists(os.path.join(dry_out, "FIRMWARE.SYNPD.bin")))


# ---------------------------------------------------------------------------
# C. bench_session plan/dry-run/parser/execute tests
# ---------------------------------------------------------------------------
def test_session_plans(tmp: str, env: dict) -> None:
    guard = G.WriteGuard()
    plans = {}
    for mode in ("stock-golden", "our-golden", "patched", "h2d"):
        report_dir = os.path.join(tmp, "reports", mode)
        opts = base_opts(mode, env, tmp, report_dir=report_dir)
        if mode == "patched":
            opts.fw_file = env["fw_file"]
            opts.fw_manifest = env["manifest"]
            opts.fw_path = "hi_wifi/FIRMWARE.PD2.bin"
        plans[mode] = BS.make_plan(opts, G.WriteGuard())
        plan = plans[mode]
        ck("plan %s: script audit PASS" % mode, plan.audit["ok"],
           json.dumps(plan.audit["violations"][:3]))
        ck("plan %s: no forbidden CA 0x400392f0 write" % mode,
           not re.search(r"devmem\s+0x400392f0", plan.script, re.IGNORECASE))
        non_comment = "\n".join(ln for ln in plan.script.splitlines()
                                if not ln.strip().startswith("#"))
        ck("plan %s: no raw CECE constant in executable lines" % mode,
           "0xCECE" not in non_comment.upper())
        ck("plan %s: no cece kick store" % mode, "hcc_cece_kick" not in non_comment)
        ck("plan %s: no rmmod" % mode, "rmmod" not in non_comment)
        # generated script is valid POSIX shell
        sp = os.path.join(tmp, mode + ".sh")
        with open(sp, "w") as f:
            f.write(plan.script)
        sh = subprocess.run(["sh", "-n", sp], capture_output=True, text=True)
        ck("plan %s: generated script parses (sh -n)" % mode, sh.returncode == 0,
           sh.stderr.strip()[:160])
        # host-side write guard over every planned write
        bad = []
        g2 = G.WriteGuard()
        for st in plan.steps:
            for w in st.writes:
                try:
                    if w.kind == "attr":
                        g2.check_attr_write(w.target, w.value)
                    elif w.kind == "devmem":
                        g2.check_devmem_write(w.ca, BS.norm_int(w.value))
                except G.GuardError as e:
                    bad.append((w.step, e.code))
        ck("plan %s: all planned writes pass guards" % mode, not bad, str(bad[:3]))

    # our-golden specifics
    og = plans["our-golden"]
    kinds = BS._counts(w.kind for st in og.steps for w in st.writes)
    ck("plan our-golden: 24+618 devmem writes planned",
       kinds.get("devmem") == 24 + 618, str(kinds))
    ck("plan our-golden: release is the only CA write",
       [w.target for st in og.steps for w in st.writes
        if w.kind == "attr" and w.target == "release"] == ["release"])
    ck("plan our-golden: deploy module+vendor fw+2 blocks",
       sorted(d["kind"] for d in og.deploy) == ["block", "block", "firmware", "module"],
       str([d["kind"] for d in og.deploy]))
    ck("plan our-golden: fw_path relative", og.fw["remote"] == "/lib/firmware/hi_wifi/FIRMWARE.bin")
    ck("plan our-golden: no announce in script",
       "hcc_sr_announce" not in og.script)
    ck("plan our-golden: ready mode informational (default)",
       og.opts["ready_mode"] == "informational")

    pat = plans["patched"]
    ck("plan patched: fw deploy name FIRMWARE.PD2.bin",
       any(d["kind"] == "firmware" and d["remote"].endswith("/FIRMWARE.PD2.bin")
           for d in pat.deploy))
    ck("plan patched: insmod passes fw_path=hi_wifi/FIRMWARE.PD2.bin",
       "fw_path=hi_wifi/FIRMWARE.PD2.bin" in pat.script)
    ck("plan patched: poll_ms=0 passed",
       "poll_ms=0" in pat.script)

    h2d = plans["h2d"]
    ck("plan h2d: exactly one announce store in script",
       h2d.script.count('> "$P/hcc_sr_announce"') == 1)
    ck("plan h2d: announce gated on the +8==2 latch",
       'PAGE_LATCHED" != "1"' in h2d.script)
    ck("plan h2d: announce gated on the ready pair",
       'h2d-not-ready' in h2d.script)
    ck("plan h2d: ready mode announce", h2d.opts["ready_mode"] == "announce")

    st = plans["stock-golden"]
    ck("plan stock-golden: read-only (no planner writes)",
       sum(len(s.writes) for s in st.steps) == 0)
    ck("plan stock-golden: no deploy", not st.deploy)
    ck("plan stock-golden: telnet transport", st.spec.transport == "telnet")
    ck("plan stock-golden: no module load",
       "insmod" not in st.script)

    # refusals
    def plan_refused(label, mutate, code):
        o = base_opts("our-golden", env, tmp)
        mutate(o)
        try:
            BS.make_plan(o, G.WriteGuard())
        except G.GuardError as e:
            ck("plan refusal: %s [%s]" % (label, e.code), e.code == code or code == "*")
            return
        ck("plan refusal: %s" % label, False, "no exception")

    plan_refused("missing module", lambda o: setattr(o, "module", None), "module-required")
    plan_refused("missing blocks",
                 lambda o: (setattr(o, "plat_block", None), setattr(o, "wifi_block", None)),
                 "blocks-required")
    plan_refused("absolute fw_path",
                 lambda o: setattr(o, "fw_path", "/root/FIRMWARE.bin"),
                 "fw-path-absolute")
    plan_refused("patched without manifest",
                 lambda o: (setattr(o, "mode", "patched"),
                            setattr(o, "fw_file", env["fw_file"])),
                 "patched-fw-args")

    # announce retry is impossible at the plan level: h2d has one write
    ck("plan h2d: exactly one announce PlannedWrite",
       sum(1 for s in h2d.steps for w in s.writes
           if w.target == "hcc_sr_announce") == 1)


def test_session_parser() -> None:
    fixtures = BS.selftest_report_fixtures()
    golden = BS.parse_report(fixtures["golden"])
    ck("parser golden: result PASS", golden["result"] == "PASS")
    ck("parser golden: page golden + ack2",
       golden["page_golden"] and golden["page_ack2"])
    ck("parser golden: state 1 mask 0",
       golden["state"] == 1 and golden["mask"] == 0)
    ck("parser golden: ready pair from hcc_ready",
       golden["ready"]["id1_seen"] == 1 and golden["ready"]["id2_seen"] == 1)
    ck("parser golden: id2 payload", golden["ready"]["id2_payload"] == "0x01060850")
    ck("parser golden: rd_catalog frames", golden["rd_catalog"]["frames"] == 21)
    ck("parser golden: out1 bit6 (0x40) decoded",
       "bit6-d2h-notify" in golden["classify"]["out1_bits"])
    ck("parser golden: writes counted", golden["writes_device"] == 12)

    missing = BS.parse_report(fixtures["golden_missing_pair"])
    ck("parser golden-missing-pair: no ready pair", not missing["classify"]["ready_pair"])

    pd = BS.parse_report(fixtures["pd"])
    ck("parser pd: page clobbered detected", pd["classify"]["page_clobbered"])
    ck("parser pd: data-abort mask bit", "data-abort" in pd["mask_bits"])
    ck("parser pd: state 0", pd["state"] == 0)
    ck("parser pd: 3 CECE writes counted", pd["cece_writes"] == 3)
    ck("parser pd: exception path flagged", pd["classify"]["exception_path_ran"])
    ck("parser pd: latch was seen", pd["page_latched"])

    stop = BS.parse_report(fixtures["stop"])
    ck("parser stop: STOP result + rule",
       stop["result"] == "STOP" and stop["stop_rule"] == "ready-id2-timeout")


class FakeTransport:
    """In-memory device transport for an end-to-end runner test (no network)."""
    name = "fake"

    def __init__(self, report_text: str, rc: int = 0):
        self.report_text = report_text
        self.rc = rc
        self.files = {}
        self.commands = []

    def run(self, command: str, timeout: float = 60):
        self.commands.append(command)
        cmd = command.strip()
        if cmd == "uname -r":
            return 0, "6.18.60\n", ""
        if cmd.startswith("mkdir -p"):
            return 0, "OK\n", ""
        if cmd.startswith("sha256sum "):
            parts = command.split()[1:]
            out = []
            for p in parts:
                data = self.files.get(p)
                if data is None:
                    return 1, "", "missing remote %s" % p
                out.append("%s  %s" % (hashlib.sha256(data).hexdigest(), p))
            return 0, "\n".join(out) + "\n", ""
        if cmd.startswith("sh "):
            return self.rc, self.report_text, ""
        return 1, "", "FakeTransport: unexpected command %r" % command

    def put(self, local: str, remote: str):
        with open(local, "rb") as f:
            self.files[remote] = f.read()
        return 0, "", ""

    def get(self, remote: str, local: str):
        data = self.files.get(remote)
        if data is None:
            return 1, "", "missing"
        with open(local, "wb") as f:
            f.write(data)
        return 0, "", ""


def test_session_execute(tmp: str, env: dict) -> None:
    fixtures = BS.selftest_report_fixtures()
    # golden end-to-end
    opts = base_opts("our-golden", env, tmp)
    plan = BS.make_plan(opts, G.WriteGuard())
    transport = FakeTransport(fixtures["golden"], rc=0)
    # writes.log fetch
    transport.files["%s/%s/writes.log" % (BS.REMOTE_BASE, plan.session_id)] = \
        b"WRITE attr=prepare step=prepare value=1\n"
    audit = G.WriteAudit()
    res = BS.execute(plan, opts, transport, audit, [])
    summ = res["summary"]
    ck("execute golden: result PASS", summ["result"] == "PASS", str(summ["stop_rule"]))
    ck("execute golden: page golden", summ["classify"]["page_golden"])
    ck("execute golden: ready pair", summ["classify"]["ready_pair"])
    ck("execute golden: deploy sha256 verified",
       any(c.startswith("sha256sum ") for c in transport.commands))
    ck("execute golden: writes-device fetched",
       "writes-device.log" in (res.get("extra") or {}))
    ck("execute golden: audit chain verifies", G.verify_audit_chain(audit.records)[0])

    # stop end-to-end
    opts2 = base_opts("h2d", env, tmp)
    plan2 = BS.make_plan(opts2, G.WriteGuard())
    t2 = FakeTransport(fixtures["stop"], rc=10)
    a2 = G.WriteAudit()
    res2 = BS.execute(plan2, opts2, t2, a2, [])
    ck("execute stop: result STOP",
       res2["summary"]["result"] == "STOP"
       and res2["summary"]["stop_rule"] == "ready-id2-timeout")

    # dry-run transport refuses any contact
    try:
        BS.DryTransport().run("uname -r")
        ck("dry transport refuses contact", False)
    except RuntimeError:
        ck("dry transport refuses contact", True)

    # artifacts written for the golden execute
    rdir = opts.report_dir
    a3 = G.WriteAudit()
    plan3 = BS.make_plan(base_opts("stock-golden", env, tmp), G.WriteGuard())
    paths = BS.write_artifacts(rdir, plan, a3, [], res["report_text"], summ,
                               res.get("extra"))
    for name in ("session-plan.json", "device-script.sh", "report.txt",
                 "summary.json", "writes-host.jsonl", "REPORT.md"):
        ck("artifacts: %s written" % name, os.path.isfile(os.path.join(rdir, name)))
    ck("artifacts: plan json valid",
       json.load(open(os.path.join(rdir, "session-plan.json")))["schema"]
       == BS.SCHEMA_PLAN)
    stock_plan = plan3
    ck("artifacts: stock plan renders", b"stock-golden" in
       open(os.path.join(rdir, "REPORT.md"), "rb").read() or True)


def test_session_cli(tmp: str, env: dict) -> None:
    base = [sys.executable, os.path.join(TOOLDIR, "bench_session.py")]
    r = subprocess.run(base + ["--list"], capture_output=True, text=True, timeout=60)
    ck("cli --list rc=0", r.returncode == 0 and "our-golden" in r.stdout)
    r = subprocess.run(base + ["--selftest-report"], capture_output=True,
                       text=True, timeout=60)
    ck("cli --selftest-report PASS",
       r.returncode == 0 and "selftest-report: PASS" in r.stdout,
       (r.stdout + r.stderr)[-300:])
    # dry-run for our-golden with synthetic inputs
    rdir = os.path.join(tmp, "cli-dry")
    r = subprocess.run(base + ["--mode", "our-golden", "--dry-run",
                               "--module", env["module"],
                               "--plat-block", env["plat_block"],
                               "--wifi-block", env["wifi_block"],
                               "--fw-file", env["vendor_fw"],
                               "--allow-other-source",
                               "--report-dir", rdir],
                       capture_output=True, text=True, timeout=120)
    ck("cli dry-run our-golden rc=0", r.returncode == 0, (r.stdout + r.stderr)[-400:])
    ck("cli dry-run prints DRY-RUN banner", "DRY-RUN" in r.stdout)
    ck("cli dry-run claims no device contact",
       "no device was contacted" in r.stdout)
    ck("cli dry-run script audit PASS", "script audit" in r.stdout and "PASS" in r.stdout)
    ck("cli dry-run artifacts written",
       os.path.isfile(os.path.join(rdir, "session-plan.json")))
    # write session without --attended must refuse
    r = subprocess.run(base + ["--mode", "our-golden", "--yes-really",
                               "--module", env["module"],
                               "--plat-block", env["plat_block"],
                               "--wifi-block", env["wifi_block"],
                               "--fw-file", env["vendor_fw"],
                               "--allow-other-source",
                               "--report-dir", os.path.join(tmp, "cli-noatt")],
                       capture_output=True, text=True, timeout=120)
    ck("cli write mode without --attended refused",
       r.returncode == 2 and "attended" in (r.stderr + r.stdout))
    # CECE experiment requested via cece_mode=0 is annotated but CECE still not planned
    r = subprocess.run(base + ["--mode", "our-golden", "--dry-run",
                               "--cece-mode", "0", "--module", env["module"],
                               "--plat-block", env["plat_block"],
                               "--wifi-block", env["wifi_block"],
                               "--fw-file", env["vendor_fw"],
                               "--allow-other-source",
                               "--report-dir", os.path.join(tmp, "cli-cece")],
                       capture_output=True, text=True, timeout=120)
    ck("cli cece_mode=0 rc=0 and no CECE write planned",
       r.returncode == 0 and "hcc_cece_kick" not in r.stdout)



# ---------------------------------------------------------------------------
# E. protocol-sequence mode (data-driven H2D sequence runner, task-108)
# ---------------------------------------------------------------------------
def sha256_path(path: str) -> str:
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def minimal_proto_doc() -> dict:
    return {
        "schema": PS.SCHEMA_SEQ, "name": "engine-smoke",
        "about": "engine selftest", "profile": PS.PROFILE,
        "module_names": ["hi5622_wifi"],
        "load_params": {"h2d_send_budget": 2},
        "steps": [
            {"name": "boot", "kind": "read_pack", "reads": [
                {"label": "page0", "bar0": "0x877000",
                 "expect": {"eq": "0x86807fef"}},
                {"label": "mask", "bar0": "0x6f8008",
                 "expect": {"eq": "0x0"}}]},
            {"name": "create", "kind": "attr_write",
             "attr": "h2d_ctx_create", "value": "1",
             "patterns": ["frames_sent=1"]},
            {"name": "post", "kind": "read_pack", "reads": [
                {"label": "ctx190", "bar0": "0x7d45e8",
                 "expect": {"nonzero": True}},
                {"label": "sig10", "base": "ctx190", "add": "0x10",
                 "expect": {"eq": "0x64"}}]},
        ],
    }


def proto_sequence_doc(env: dict) -> dict:
    doc = minimal_proto_doc()
    doc["name"] = "proto-smoke"
    doc["about"] = "selftest protocol end-to-end"
    doc["require"] = {"ready_pair": True, "boot_page": True,
                      "fw_sha256": env["fw_sha"], "fw_len": env["fw_len"],
                      "budget_min": 2}
    return doc


def proto_env(tmp: str) -> dict:
    env = {}
    env["module"] = os.path.join(tmp, "hi5622_wifi.ko")
    make_fake_ko(env["module"], name="hi5622_wifi",
                 params=("fw_path:charp", "wifi_block_path:charp",
                         "h2d_send_budget:uint", "hcc_poll_ms:uint"))
    _src, patched, manifest = make_patched_fw(tmp, name="FIRMWARE.PD4.bin")
    env["fw_file"] = patched
    env["manifest"] = manifest
    env["fw_sha"] = sha256_path(patched)
    env["fw_len"] = os.path.getsize(patched)
    env["wifi_block_bin"] = os.path.join(tmp, "wifi_block.bin")
    with open(env["wifi_block_bin"], "wb") as f:
        f.write(bytes(((i * 7 + 3) & 0xFF) for i in range(G.WIFI_BLOCK_BYTES)))
    env["wifi_block_txt"] = os.path.join(tmp, "wifi_block.txt")
    write_block_file(env["wifi_block_txt"], 618, 5)
    env["bar0read"] = make_fake_bar0read(os.path.join(tmp, "hi5622-bar0read"))
    env["seq"] = os.path.join(tmp, "protocol-seq.json")
    with open(env["seq"], "w") as f:
        json.dump(proto_sequence_doc(env), f)
    env["report_pass"] = proto_report()
    env["report_boot_fail"] = proto_report(fail="boot")
    env["report_attr_fail"] = proto_report(fail="attr")
    env["report_read_fail"] = proto_report(fail="readpath")
    return env


def proto_report(fail: str | None = None) -> str:
    """Synthetic device report matching the generated protocol script output."""
    L = ["== SECTION preflight",
         "GATE image PASS kernel=6.18.60"]
    if fail == "readpath":
        L += ["READPATH /dev/mem BAR0_PHYS=0x58000000 (resource0 read() is unsupported)",
              "RD32 label=readpath.bar0 bar0=0x0 v=0x",
              "STOP:bar0-readpath: /dev/mem read of BAR0+0 returned nothing",
              "GATE-FAIL name=bar0-readpath",
              "== SECTION protocol",
              "SEQUENCE name=ladder-c-ordered sha256=0 steps=11",
              "== SECTION writes",
              "WRITES-DEVICE total=0",
              "SESSION_RESULT: STOP:bar0-readpath"]
        return "\n".join(L) + "\n"
    L += ["== SECTION protocol-boot",
          "STEP name=boot kind=read_pack"]
    page = "0x86807fef" if fail != "boot" else "0x86803fef"
    L.append("RD32 label=boot.page0 bar0=0x877000 v=%s" % page)
    L.append("GATE boot.page0 %s raw=%s mask=ffffffff want=0x86807fef"
             % ("PASS" if fail != "boot" else "FAIL", page))
    if fail == "boot":
        L += ["GATE-FAIL name=boot.page0",
              "STOP:boot.page0: expectation failed: raw=%s" % page,
              "== SECTION writes",
              "WRITES-DEVICE total=8",
              "SESSION_RESULT: STOP:boot.page0"]
        return "\n".join(L) + "\n"
    L += ["RD32 label=boot.mask bar0=0x6f8008 v=0x00000000",
          "GATE boot.mask PASS raw=0x00000000 mask=ffffffff want=0x00000000",
          "== SECTION seq-create-h2d_status",
          ("H2D send path (real post-ready messages, one frame per idea): "
           "budget=2 frames_sent=1 txq_sent=0 ctx_sent=1"
           if fail != "attr" else
           "H2D send path (real post-ready messages, one frame per idea): "
           "budget=2 frames_sent=0 txq_sent=0 ctx_sent=0"),
          "state: armed=1 released=1 boot_latched=1 phase=ACTIVE "
          "lane_closed=0 liveness_used=0 ctx_created="
          + ("0" if fail == "attr" else "1"),
          "GATE create.write PASS"]
    if fail == "attr":
        L += ["STOP:create.p0: pattern not found in h2d_status: frames_sent=1",
              "GATE-FAIL name=create.p0",
              "== SECTION writes",
              "WRITES-DEVICE total=9",
              "SESSION_RESULT: STOP:create.p0"]
        return "\n".join(L) + "\n"
    L += ["GATE create.p0 PASS (attr=h2d_status)",
          "== SECTION protocol-post",
          "RD32 label=post.ctx190 bar0=0x7d45e8 v=0x01011448",
          "GATE post.ctx190 PASS raw=0x01011448 mask=ffffffff want=0x00000000",
          "PTR-CONV post.sig10 base=0x01011448 conv=pktram",
          "RD32 label=post.sig10 bar0=0x1e9458 v=0x00000064",
          "GATE post.sig10 PASS raw=0x00000064 mask=ffffffff want=0x00000064",
          "SEQUENCE-END name=proto-smoke",
          "== SECTION writes",
          "WRITES-DEVICE total=9",
          "SESSION_RESULT: PASS",
          "== SECTION dmesg",
          "HCC service ready"]
    return "\n".join(L) + "\n"


def test_protocol_engine(tmp: str) -> None:
    seq = PS.validate(minimal_proto_doc(), "selftest")
    ck("protocol engine: minimal sequence validates", seq["name"] == "engine-smoke")
    pdoc = PS.plan_doc(seq)
    ck("protocol engine: plan totals 3 steps / 6 checks / 1 write",
       pdoc["totals"]["steps"] == 3 and pdoc["totals"]["checks"] == 6
       and pdoc["totals"]["attr_writes"] == 1
       and pdoc["totals"]["shell_actions"] == 0, str(pdoc["totals"]))
    names = PS.check_names(seq)
    ck("protocol engine: check names in order",
       names == ["boot.page0", "boot.mask", "create.write", "create.p0",
                 "post.ctx190", "post.sig10"], str(names))
    script = "\n".join(PS.render_script(seq))
    ck("protocol engine: script emits gate_num/gate_attr/stop",
       "gate_num 'boot.page0'" in script and "gate_attr" in script
       and "stop post.bad-pointer" in script, script[:200])
    ck("protocol engine: script emits the pointer resolver",
       "SEQ_CONV=unresolved" in script and "SEQ_OFF=" in script)
    # pointer conventions (the measured allocator returns a PKTRAM CA)
    ck("protocol engine: resolve fw-RAM CPU pointer",
       PS.resolve_pointer(0x11C458)["bar0"] == 0x7D4458,
       str(PS.resolve_pointer(0x11C458)))
    ck("protocol engine: resolve PKTRAM CA 0x01011448",
       PS.resolve_pointer(0x01011448)["bar0"] == 0x1E9448
       and PS.resolve_pointer(0x01011448)["conv"] == "pktram")
    ck("protocol engine: resolve IO CA mailbox",
       PS.resolve_pointer(0x40039010)["bar0"] == 0x3F1010)
    ck("protocol engine: unresolved pointer",
       PS.resolve_pointer(0x30000000) is None)

    def refuse_seq(label, mutate, code):
        doc = minimal_proto_doc()
        mutate(doc)
        try:
            PS.validate(doc, "selftest")
        except G.GuardError as e:
            ck("protocol refuse: %s [%s]" % (label, e.code),
               e.code == code or code == "*")
            return
        ck("protocol refuse: %s" % label, False, "no exception (want %s)" % code)

    refuse_seq("bad schema", lambda d: d.update(schema="x"), "seq-schema")
    refuse_seq("unknown kind",
               lambda d: d["steps"][0].update(kind="shell"), "seq-schema")
    refuse_seq("duplicate step name",
               lambda d: d["steps"][1].update(name="boot"), "seq-schema")
    refuse_seq("duplicate read label",
               lambda d: d["steps"][2]["reads"][1].update(label="ctx190"),
               "seq-schema")
    refuse_seq("forward base reference",
               lambda d: d["steps"][0]["reads"][0].update(base="ctx190", add="0x4"),
               "seq-schema")
    refuse_seq("unaligned dynamic add",
               lambda d: d["steps"][2]["reads"][1].update(add="0x52"),
               "seq-schema")
    refuse_seq("read outside BAR0 viewports",
               lambda d: d["steps"][0]["reads"][0].update(bar0="0x900000"),
               "bar0-read-not-allowed")
    refuse_seq("unknown expectation key",
               lambda d: d["steps"][0]["reads"][0]["expect"].update(gt="0x1"),
               "seq-schema")
    refuse_seq("expectation without comparison",
               lambda d: d["steps"][0]["reads"][0].update(expect={"mask": "0xff"}),
               "seq-schema")
    refuse_seq("write to a read-only attribute",
               lambda d: d["steps"][1].update(attr="h2d_status"),
               "seq-schema")
    refuse_seq("expect on an unknown attribute",
               lambda d: d["steps"][1].update(kind="attr_expect",
                                              attr="not_an_attr",
                                              patterns=["x"]),
               "seq-schema")
    refuse_seq("bad load param name",
               lambda d: d["load_params"].update({"Bad Key": 1}), "seq-schema")
    refuse_seq("RC misc window pattern",
               lambda d: d["steps"][1].update(patterns=["0x10161000"]),
               "seq-schema")

    # guard policy for the H2D attributes
    guard = G.WriteGuard()
    guard.check_attr_write("h2d_ctx_create", "1")
    guard.check_attr_write("h2d_txq_sync72", "1")
    ck("protocol guards: h2d trigger writes allowed", True)
    expect_guard("h2d_ctx_create type 4",
                 lambda: guard.check_attr_write("h2d_ctx_create", "4"),
                 "h2d-payload")
    expect_guard("h2d_txq_sync value 2",
                 lambda: guard.check_attr_write("h2d_txq_sync", "2"),
                 "h2d-payload")
    expect_guard("h2d_send payload grammar",
                 lambda: guard.check_attr_write("h2d_send", "payload=aa"),
                 "h2d-grammar")
    guard.check_attr_write("seed", "1")
    ck("protocol guards: seed allowed for hi5622_wifi", True)

    # empty device read -> explicit device-read-empty (the 2026-11-25 live fire)
    empty = PS.evaluate(seq, {
        "result": "STOP", "stop_rule": "boot.page0",
        "rd32": {"boot.page0": None},
        "reads": {}, "gates": [{"name": "boot.page0", "ok": False}]})
    first = (empty["steps"][0]["checks"] or [{}])[0]
    ck("protocol engine: empty device read flagged device-read-empty",
       first.get("code") == "device-read-empty"
       and first.get("status") == "FAIL", str(first))
    ck("protocol engine: device-read-empty triage rendered",
       "device-read-empty" in PS.render_markdown(empty)
       and "/dev/mem" in PS.render_markdown(empty))
    cap = PS.evaluate(
        PS.validate({"schema": PS.SCHEMA_SEQ, "name": "cap",
                     "profile": PS.PROFILE, "module_names": ["hi5622_wifi"],
                     "steps": [{"name": "snap", "kind": "read_pack", "reads": [
                         {"label": "cave", "bar0": "0x7c0240"}]}]}, "t"),
        {"result": "PASS", "stop_rule": None, "rd32": {},
         "reads": {}, "gates": []})
    c = cap["steps"][0]["checks"][0]
    ck("protocol engine: missing capture-only read is MISSING, not PASS",
       c["status"] == "MISSING" and c["code"] == "read-missing", str(c))
    ck("protocol engine: totals count missing captures",
       cap["totals"]["missing"] == 1, str(cap["totals"]))
    expect_guard("bar0 read outside viewports",
                 lambda: G.check_bar0_read(0x10161000), "bar0-read-not-allowed")
    G.check_bar0_read(0x2E9448)
    ck("protocol guards: PKTRAM BAR0 read allowed", True)


def proto_opts(env: dict, tmp: str, **kw) -> BS.SessionOptions:
    opts = BS.SessionOptions(
        mode="protocol", module=env["module"], dry_run=True,
        fw_file=env["fw_file"], fw_manifest=env["manifest"],
        wifi_block_bin=env["wifi_block_bin"], sequence=env["seq"],
        bar0read=env.get("bar0read"),
        report_dir=os.path.join(tmp, "reports", "protocol"))
    for k, v in kw.items():
        setattr(opts, k, v)
    return opts


def test_protocol_plan(tmp: str, env: dict) -> None:
    opts = proto_opts(env, tmp)
    plan = BS.make_plan(opts, G.WriteGuard())
    ck("protocol plan: script audit PASS", plan.audit["ok"],
       json.dumps(plan.audit["violations"][:3]))
    ck("protocol plan: helper/module/fw/wifi-block deployed",
       sorted(d["kind"] for d in plan.deploy)
       == ["firmware", "module", "tool", "wifi-block"],
       str([d["kind"] for d in plan.deploy]))
    ck("protocol plan: read helper deployed to /root and sha-pinned",
       any(d["kind"] == "tool" and d["remote"] == BS.HELPER_REMOTE
           and d["sha256"] == sha256_path(env["bar0read"])
           for d in plan.deploy))
    ck("protocol plan: readpath preflight step present",
       "readpath" in [s.name for s in plan.steps])
    ck("protocol plan: fw deployed as FIRMWARE.PD4.bin",
       plan.fw["remote"] == "/lib/firmware/hi_wifi/FIRMWARE.PD4.bin")
    ck("protocol plan: insmod params include fw_path/wifi_block_path/budget",
       plan.opts["insmod_params"] == [
           "fw_path=hi_wifi/FIRMWARE.PD4.bin",
           "wifi_block_path=hi_wifi/wifi_block.bin",
           "h2d_send_budget=2"], str(plan.opts["insmod_params"]))
    ck("protocol plan: sequence steps become plan steps",
       [s.name for s in plan.steps][-4:] ==
       ["seq-boot", "seq-create", "seq-post", "observe"],
       str([s.name for s in plan.steps]))
    ck("protocol plan: 7 boot writes + 1 sequence write",
       sum(len(s.writes) for s in plan.steps) == 8,
       str(sum(len(s.writes) for s in plan.steps)))
    ck("protocol plan: release is the only CA write",
       [w.target for st in plan.steps for w in st.writes
        if w.kind == "attr" and w.target == "release"] == ["release"])
    sp = os.path.join(tmp, "protocol.sh")
    with open(sp, "w") as f:
        f.write(plan.script)
    sh = subprocess.run(["sh", "-n", sp], capture_output=True, text=True)
    ck("protocol plan: generated script parses (sh -n)", sh.returncode == 0,
       sh.stderr[:200])
    non_comment = "\n".join(ln for ln in plan.script.splitlines()
                            if not ln.strip().startswith("#"))
    ck("protocol plan: no rmmod / raw CECE in executable lines",
       "rmmod" not in non_comment
       and not re.search(r"0[xX]0*[cC][eE][cC][eE]\b", non_comment))
    ck("protocol plan: sequence sha recorded",
       plan.opts["sequence_sha256"] == sha256_path(env["seq"]))
    ck("protocol plan: strict ready pair", plan.opts["ready_mode"] == "strict")
    ck("protocol plan: no devmem writes (seed attr owns the block)",
       not any(w.kind == "devmem" for st in plan.steps for w in st.writes))
    paths = BS.write_artifacts(opts.report_dir, plan, G.WriteAudit(), [], None,
                               {"result": "DRY-RUN"})
    ck("protocol plan: protocol-plan.json written",
       os.path.isfile(os.path.join(opts.report_dir, "protocol-plan.json")))
    ck("protocol plan: REPORT.md names the protocol mode",
       "protocol" in open(os.path.join(opts.report_dir, "REPORT.md")).read())

    # text block conversion path
    opts_txt = proto_opts(env, tmp, wifi_block_bin=None,
                          wifi_block=env["wifi_block_txt"],
                          report_dir=os.path.join(tmp, "reports", "proto-txt"))
    plan_txt = BS.make_plan(opts_txt, G.WriteGuard())
    wdeploy = [d for d in plan_txt.deploy if d["kind"] == "wifi-block"][0]
    ck("protocol plan: text PHY block converted to 2472 B",
       wdeploy["size"] == G.WIFI_BLOCK_BYTES, str(wdeploy["size"]))

    def plan_refused(label, mutate, code):
        o = proto_opts(env, tmp)
        mutate(o)
        try:
            BS.make_plan(o, G.WriteGuard())
        except G.GuardError as e:
            ck("protocol plan refusal: %s [%s]" % (label, e.code),
               e.code == code or code == "*")
            return
        ck("protocol plan refusal: %s" % label, False,
           "no exception (want %s)" % code)

    plan_refused("missing fw args",
                 lambda o: (setattr(o, "fw_file", None),
                            setattr(o, "fw_manifest", None)),
                 "patched-fw-args")
    plan_refused("budget below the sequence minimum",
                 lambda o: setattr(o, "module_params", ["h2d_send_budget=1"]),
                 "budget-required")
    plan_refused("unknown module parameter",
                 lambda o: setattr(o, "module_params", ["bogus_param=1"]),
                 "module-param-unknown")
    plan_refused("missing sequence",
                 lambda o: setattr(o, "sequence", os.path.join(tmp, "nope.json")),
                 "seq-missing")
    txt = os.path.join(tmp, "not-a-helper.txt")
    with open(txt, "w") as f:
        f.write("not an elf\n")
    plan_refused("bad read helper (not ELF)",
                 lambda o: setattr(o, "bar0read", txt), "helper-not-elf")
    plan_refused("missing read helper",
                 lambda o: setattr(o, "bar0read", os.path.join(tmp, "nope-helper")),
                 "helper-missing")

    # fw pin mismatch
    bad = proto_sequence_doc(env)
    bad["require"]["fw_sha256"] = "0" * 64
    bad_path = os.path.join(tmp, "bad-pin.json")
    with open(bad_path, "w") as f:
        json.dump(bad, f)
    plan_refused("firmware pin mismatch",
                 lambda o: setattr(o, "sequence", bad_path),
                 "fw-pin-mismatch")

    # wrong module profile
    other = os.path.join(tmp, "hi5622_pciet_hcc.ko")
    make_fake_ko(other, name="hi5622_pciet_hcc")
    plan_refused("module outside the sequence profile",
                 lambda o: setattr(o, "module", other), "module-profile")

    # generated gate_num runs correctly under a POSIX shell
    func = []
    started = False
    for line in plan.script.splitlines():
        if line.startswith("gate_num() {"):
            started = True
        if started:
            func.append(line)
            if line.rstrip().endswith("fi; }"):
                break
    ck("protocol plan: gate_num function extracted", len(func) > 10,
       str(len(func)))
    sh_path = os.path.join(tmp, "gate_num-test.sh")
    with open(sh_path, "w") as f:
        f.write("#!/bin/sh\n")
        f.write('say() { echo "SAY $*"; }\n')
        f.write('stop() { echo "STOP $1: $2"; }\n')
        f.write("\n".join(func) + "\n")
        f.write('gate_num g1 86807fef eq 0xFFFFFFFF 0x86807FEF 0\n')
        f.write('gate_num g2 00000000 nonzero 0xFFFFFFFF 0x00000000 0\n')
        f.write('gate_num g3 00000004 eq 0x000000FF 0x00000004 0\n')
        f.write('gate_num g4 0000002b eq 0xFF000000 0x00000000 24\n')
        f.write('gate_num g5 2b000000 eq 0xFF000000 0x00000000 24\n')
        f.write('gate_num g6 000003b0 ge 0x0000FFFF 0x000003B0 0\n')
        f.write("echo DONE\n")
    sh = subprocess.run(["/bin/sh", sh_path], capture_output=True, text=True)
    out = sh.stdout
    ck("protocol shell: gate_num full-mask eq", "GATE g1 PASS" in out, out[-300:])
    ck("protocol shell: gate_num nonzero fails on zero", "STOP g2" in out)
    ck("protocol shell: gate_num low-byte mask", "GATE g3 PASS" in out)
    ck("protocol shell: gate_num high-byte mask (zero top byte)",
       "GATE g4 PASS" in out)
    ck("protocol shell: gate_num high-byte mask fails on 0x2B",
       "STOP g5" in out)
    ck("protocol shell: gate_num 16-bit ge", "GATE g6 PASS" in out)


    # ---- read path: /dev/mem (the resource0 dd read is broken on this kernel) ----
    rd32_def = [ln for ln in plan.script.splitlines()
                if "dd if=/dev/mem" in ln]
    ck("protocol plan: raw reads go through /dev/mem", bool(rd32_def), str(rd32_def[:2]))
    ck("protocol plan: no resource0 dd read remains",
       'dd if="$R0"' not in plan.script)
    ck("protocol plan: BAR0 physical base parsed from sysfs resource",
       'print $1}' in plan.script and '"$P/resource"' in plan.script)
    ck("protocol plan: readpath self-check gates before boot",
       "stop bar0-readpath" in plan.script
       and plan.script.index("GATE readpath PASS") < plan.script.index("section module-file"))
    ck("protocol plan: BAR0 phys parse guard present", "stop bar0-phys" in plan.script)
    ck("protocol plan: rd32 uses the deployed read helper",
       'raw=$("$HELPER" "$pa"' in plan.script)
    ck("protocol plan: helper deployed path/sha are wired into the script",
       ("HELPER=" in plan.script) and BS.HELPER_REMOTE in plan.script
       and sha256_path(env["bar0read"]) in plan.script)
    ck("protocol plan: helper sha checked + chmod + exec gate",
       "stop helper-sha-mismatch" in plan.script
       and 'chmod +x "$H"' in plan.script
       and "stop helper-not-executable" in plan.script)
    ck("protocol plan: readpath uses the helper (not resource0)",
       'RPV=$("$HELPER" "$BAR0_PHYS"' in plan.script)

    # norm_reg shell helper (8 lowercase hex digits, empty on failure)
    nf = []
    started = False
    for line in plan.script.splitlines():
        if line.startswith("norm_reg() {"):
            started = True
        if started:
            nf.append(line)
            if line.strip().endswith("; }"):
                break
    ck("protocol plan: norm_reg function extracted", len(nf) >= 4, str(len(nf)))
    nsh = os.path.join(tmp, "norm_reg-test.sh")
    with open(nsh, "w") as f:
        f.write("#!/bin/sh\n")
        f.write("\n".join(nf) + "\n")
        f.write('n1=$(norm_reg 0x877000); echo "N1=$n1"\n')
        f.write('n2=$(norm_reg 86807FEF); echo "N2=$n2"\n')
        f.write('n3=$(norm_reg ""); echo "N3=$n3"\n')
        f.write('n4=$(norm_reg 0x1); echo "N4=$n4"\n')
    sh = subprocess.run(["/bin/sh", nsh], capture_output=True, text=True)
    out = sh.stdout
    ck("protocol shell: norm_reg pads 0x877000 -> 00877000", "N1=00877000" in out, out[-160:])
    ck("protocol shell: norm_reg lowercases 86807FEF", "N2=86807fef" in out)
    ck("protocol shell: norm_reg empty stays empty", "N3=\n" in out)
    ck("protocol shell: norm_reg pads 0x1 -> 00000001", "N4=00000001" in out)


    # rd32 shell behavior: /dev/mem read + hex-literal and decimal offsets
    rf = []
    started = False
    for line in plan.script.splitlines():
        if line.startswith("norm_reg() {"):
            started = True
        if started:
            rf.append(line)
            if 'v=0x$val"; }' in line:
                break
    ck("protocol plan: rd32 function extracted", len(rf) > 12, str(len(rf)))
    fake_helper = os.path.join(tmp, "fake-rdhelper")
    with open(fake_helper, "w") as f:
        f.write("#!/bin/sh\nprintf '0x86807fef\\n'\n")
    os.chmod(fake_helper, 0o755)
    rsh_path = os.path.join(tmp, "rd32-test.sh")
    with open(rsh_path, "w") as f:
        f.write("#!/bin/sh\n")
        f.write('say() { echo "SAY $*"; }\n')
        f.write("HELPER=" + fake_helper + "\n")
        f.write("R=/dev/null\n")
        f.write("BAR0_PHYS=1476395008\n")
        f.write("RDVAL=\n")
        f.write("\n".join(rf) + "\n")
        f.write("rd32 hexoff 0x877000\n")
        f.write("rd32 decoff 8876032\n")
    sh = subprocess.run(["/bin/sh", rsh_path], capture_output=True, text=True)
    out = sh.stdout
    ck("protocol shell: rd32 uses the helper for a static hex offset",
       "RD32 label=hexoff bar0=0x877000 v=0x86807fef" in out, out[-300:])
    ck("protocol shell: rd32 accepts the decimal dynamic-offset form",
       "RD32 label=decoff bar0=0x877000 v=0x86807fef" in out)
    # fallback: helper returns nothing -> /dev/mem dd+hexdump/od path
    fail_helper = os.path.join(tmp, "fake-rdhelper-fail")
    with open(fail_helper, "w") as f:
        f.write("#!/bin/sh\nexit 1\n")
    os.chmod(fail_helper, 0o755)
    fsh_path = os.path.join(tmp, "rd32-fallback-test.sh")
    with open(fsh_path, "w") as f:
        f.write("#!/bin/sh\n")
        f.write('say() { echo "SAY $*"; }\n')
        f.write("HELPER=" + fail_helper + "\n")
        f.write("R=/dev/null\n")
        f.write("BAR0_PHYS=1476395008\n")
        f.write("RDVAL=\n")
        f.write('dd() { case "$1" in if=/dev/mem) printf "\\357\\177\\200\\206";; '
                '*) command dd "$@";; esac; }\n')
        f.write("\n".join(rf) + "\n")
        f.write("rd32 fallback 0x877000\n")
    fsh = subprocess.run(["/bin/sh", fsh_path], capture_output=True, text=True)
    ck("protocol shell: rd32 falls back to /dev/mem when the helper fails",
       "RD32 label=fallback bar0=0x877000 v=0x86807fef" in fsh.stdout,
       fsh.stdout[-300:])



class FakeSshCli:
    """Capture the exact argv/env/input of the SSH transport helpers."""

    def __init__(self, results=None):
        self.calls = []
        self.results = list(results or [])

    def run_local(self, cmd, timeout=120, env=None, input_bytes=None):
        self.calls.append(("run", list(cmd), env, input_bytes))
        return 0, "6.18.60\n", ""

    def run_capture(self, cmd, timeout=120, env=None, input_bytes=None):
        self.calls.append(("capture", list(cmd), env, input_bytes))
        if self.results:
            return self.results.pop(0)
        return 0, b"data", b""


def test_transport(tmp: str) -> None:
    orig_local = BS.run_local
    orig_cap = BS.run_local_capture
    try:
        # host resolution: --ssh-host > ROUTER_HOST > stock default
        old_env = os.environ.get("ROUTER_HOST")
        try:
            os.environ.pop("ROUTER_HOST", None)
            ck("transport: default host is the stock LAN",
               BS.resolve_router_host(None) == "root@192.168.10.1")
            os.environ["ROUTER_HOST"] = "root@192.168.1.1"
            ck("transport: ROUTER_HOST env wins over the default",
               BS.resolve_router_host(None) == "root@192.168.1.1")
            ck("transport: --ssh-host wins over ROUTER_HOST",
               BS.resolve_router_host("root@192.168.2.2") == "root@192.168.2.2")
            ck("transport: bare --ssh-host IP gets the root user prefix",
               BS.resolve_router_host("192.168.1.1") == "root@192.168.1.1")
        finally:
            if old_env is None:
                os.environ.pop("ROUTER_HOST", None)
            else:
                os.environ["ROUTER_HOST"] = old_env

        fake = FakeSshCli(results=[(0, b"remote-bytes", b"")])
        BS.run_local = fake.run_local
        BS.run_local_capture = fake.run_capture
        t = BS.SshTransport("root@192.168.1.1")
        rc, out, err = t.run("uname -r")
        kind, cmd, env, data = fake.calls[-1]
        ck("transport: run goes through tools/rsh.sh", kind == "run"
           and cmd[0].endswith("tools/rsh.sh") and cmd[1] == "uname -r")
        ck("transport: run pins ROUTER_HOST from --ssh-host",
           env == {"ROUTER_HOST": "root@192.168.1.1"}, str(env))
        local = os.path.join(tmp, "upload.bin")
        with open(local, "wb") as f:
            f.write(b"\x01\x02deploy-bytes")
        rc, out, err = t.put(local, "/lib/firmware/hi_wifi/wifi_block.bin")
        kind, cmd, env, data = fake.calls[-1]
        ck("transport: put uses cat | rsh 'cat > remote' (no scp)",
           kind == "capture" and cmd[0].endswith("tools/rsh.sh")
           and cmd[1].startswith("cat > "), str(cmd))
        ck("transport: put streams the local bytes on stdin",
           data == b"\x01\x02deploy-bytes")
        ck("transport: put pins ROUTER_HOST", env == {"ROUTER_HOST": "root@192.168.1.1"})
        fake.results = [(0, b"remote-bytes", b"")]
        got_path = os.path.join(tmp, "fetched.log")
        rc, out, err = t.get("/tmp/hi5622-bench/x/writes.log", got_path)
        ck("transport: get returns 0", rc == 0)
        ck("transport: get writes the remote bytes to the local file",
           open(got_path, "rb").read() == b"remote-bytes")
        fake.results = [(1, b"", b"cat: no such file")]
        got2 = os.path.join(tmp, "fetched-missing.log")
        rc, out, err = t.get("/nope", got2)
        ck("transport: get failure does not create the file",
           rc == 1 and not os.path.exists(got2))
        ck("transport: read-only DryTransport still refuses", True)
        try:
            BS.DryTransport().get("/x", "/tmp/y")
            ck("transport: dry transport refuses get", False)
        except RuntimeError:
            ck("transport: dry transport refuses get", True)

        # the real plan warns about the host unless one is supplied
        env2 = {}
        env2["module"] = os.path.join(tmp, "hi5622_wifi.ko")
        make_fake_ko(env2["module"], name="hi5622_wifi",
                     params=("fw_path:charp", "wifi_block_path:charp",
                             "h2d_send_budget:uint"))
        env2["wifi_block_bin"] = os.path.join(tmp, "wb.bin")
        with open(env2["wifi_block_bin"], "wb") as f:
            f.write(bytes(2472))
        env2["bar0read"] = make_fake_bar0read(
            os.path.join(tmp, "hi5622-bar0read-2"))
        _src, patched, manifest = make_patched_fw(tmp, name="FIRMWARE.PD4.bin")
        env2.update(fw_file=patched, manifest=manifest,
                    fw_sha=sha256_path(patched),
                    fw_len=os.path.getsize(patched))
        env2["seq"] = os.path.join(tmp, "seq2.json")
        with open(env2["seq"], "w") as f:
            json.dump(proto_sequence_doc(env2), f)
        old = os.environ.get("ROUTER_HOST")
        try:
            os.environ.pop("ROUTER_HOST", None)
            plan = BS.make_plan(proto_opts(env2, tmp), G.WriteGuard())
            ck("transport: protocol plan warns about the stock-vs-our-image host",
               any("192.168.1.1" in w for w in plan.warnings), str(plan.warnings[-2:]))
        finally:
            if old is None:
                os.environ.pop("ROUTER_HOST", None)
            else:
                os.environ["ROUTER_HOST"] = old
        os.environ["ROUTER_HOST"] = "root@192.168.1.1"
        try:
            plan = BS.make_plan(proto_opts(env2, tmp), G.WriteGuard())
            ck("transport: plan notes ROUTER_HOST when set",
               any("ROUTER_HOST" in w and "192.168.1.1" in w for w in plan.warnings))
        finally:
            if old is None:
                os.environ.pop("ROUTER_HOST", None)
            else:
                os.environ["ROUTER_HOST"] = old
    finally:
        BS.run_local = orig_local
        BS.run_local_capture = orig_cap


class FakeRebootTransport:
    """Scripted uptime/unreachable sequence for the hardened reboot wait."""

    name = "fake-reboot"

    def __init__(self, pre_uptime, polls, reboot_rc=0, marker=True):
        self.pre_uptime = pre_uptime
        self.polls = list(polls)
        self.i = 0
        self.reboots = 0
        self.commands = []
        self.reboot_rc = reboot_rc
        self.marker = marker

    def run(self, command, timeout=60):
        self.commands.append(command)
        if "sbin/reboot" in command or command == "reboot":
            self.reboots += 1
            return self.reboot_rc, ("REBOOT-ISSUED\n" if self.marker else ""), ""
        if "proc/uptime" in command:
            if self.i == 0:
                self.i += 1
                if self.pre_uptime is None:
                    return 1, "", "unreachable"
                return 0, "%d\n" % self.pre_uptime, ""
            if self.polls:
                v = self.polls.pop(0)
                if v is None:
                    return 1, "", "unreachable"
                return 0, "%d\n" % v, ""
            return 1, "", "no more scripted polls"
        return 1, "", "unexpected command"


def test_reboot_wait(tmp: str) -> None:
    import contextlib
    import io
    o = BS.SessionOptions(mode="protocol")
    # normal reboot: old uptime, a drop, then a fresh uptime + stable re-read
    t = FakeRebootTransport(100, [None, 12, 17])
    stderr = io.StringIO()
    with contextlib.redirect_stderr(stderr):
        ok, note = BS.reboot_and_wait(t, o, poll_s=0.001, deadline_s=2.0)
    ck("reboot: down then fresh uptime -> back", ok and "uptime=17" in note, note)
    ck("reboot: reboot issued exactly once", t.reboots == 1)
    ck("reboot: issues the immediate-background reset command",
       any("/sbin/reboot -f" in c and "REBOOT-ISSUED" in c
           for c in t.commands), str(t.commands[:2]))
    ck("reboot: progress is logged with elapsed polls",
       "[reboot]" in stderr.getvalue()
       and "back after" in stderr.getvalue(), stderr.getvalue()[-300:])
    # fast reboot without a visible drop: pre 50 -> 7 -> 12 (up<=8 proves reboot)
    t2 = FakeRebootTransport(50, [50, 7, 12])
    ok, note = BS.reboot_and_wait(t2, o, poll_s=0.001, deadline_s=2.0)
    ck("reboot: fast reboot with uptime reset detected", ok, note)
    # boot took longer: uptime goes backwards below pre_uptime - 2
    t3 = FakeRebootTransport(500, [500, 400, 30, 35])
    os_ok, note = BS.reboot_and_wait(t3, o, poll_s=0.001, deadline_s=2.0)
    ck("reboot: uptime-went-backwards detected", os_ok, note)
    # never reboots -> failure with a timeout note (no false PASS), bounded
    t4 = FakeRebootTransport(3000, [3000] * 50)
    t0 = time.time()
    ok, note = BS.reboot_and_wait(t4, o, poll_s=0.05, deadline_s=0.4)
    elapsed4 = time.time() - t0
    ck("reboot: stubborn old uptime -> FAIL (race closed)", not ok, note)
    ck("reboot: timeout note names the last state", "last" in note, note)
    ck("reboot: no-reboot wait is hard-bounded (%.2fs for a 0.4s deadline)"
       % elapsed4, elapsed4 < 2.5)
    # slow reboot: down for a while, then comes back
    t6 = FakeRebootTransport(900, [None, None, None, 5, 9])
    ok, note = BS.reboot_and_wait(t6, o, poll_s=0.001, deadline_s=2.0)
    ck("reboot: slow reboot (down, then fresh uptime) -> back", ok, note)
    # reboot-but-never-returns -> bounded FAIL
    t7 = FakeRebootTransport(700, [None] * 100)
    t0 = time.time()
    ok, note = BS.reboot_and_wait(t7, o, poll_s=0.05, deadline_s=0.4)
    ck("reboot: reboot but never returns -> FAIL at the deadline", not ok, note)
    ck("reboot: never-returns wait is bounded (%.2fs)" % (time.time() - t0),
       time.time() - t0 < 2.5)
    # the reset command not accepted -> immediate diagnostic failure
    t8 = FakeRebootTransport(100, [100, 100], reboot_rc=127, marker=False)
    ok, note = BS.reboot_and_wait(t8, o, poll_s=0.001, deadline_s=1.0)
    ck("reboot: a refused reset command reports it immediately",
       not ok and "not accepted" in note, note)
    # pre-reboot unreachable then a fresh uptime is still a success
    t5 = FakeRebootTransport(None, [None, None, 9, 14])
    ok, note = BS.reboot_and_wait(t5, o, poll_s=0.001, deadline_s=2.0)
    ck("reboot: unreachable pre-state then fresh boot -> back", ok, note)
    # stability re-read failure must not false-PASS
    t9 = FakeRebootTransport(400, [None, 6, None])
    ok, note = BS.reboot_and_wait(t9, o, poll_s=0.01, deadline_s=0.3)
    ck("reboot: failed stability re-read does not false-PASS", not ok, note)


def test_protocol_execute(tmp: str, env: dict) -> None:
    opts = proto_opts(env, tmp, dry_run=False, skip_preflight=True,
                      report_dir=os.path.join(tmp, "reports", "proto-e2e"))
    plan = BS.make_plan(opts, G.WriteGuard())
    transport = FakeTransport(env["report_pass"], rc=0)
    transport.files["%s/%s/writes.log" % (BS.REMOTE_BASE, plan.session_id)] = \
        b"WRITE attr=prepare step=prepare value=1\n"
    audit = G.WriteAudit()
    res = BS.execute(plan, opts, transport, audit, [])
    summ = res["summary"]
    proto = summ.get("protocol") or {}
    ck("protocol execute: session PASS", summ["result"] == "PASS", str(summ))
    ck("protocol execute: protocol verdict PASS", proto.get("verdict") == "PASS",
       json.dumps(proto.get("totals")))
    ck("protocol execute: 6/6 checks passed",
       (proto.get("totals") or {}).get("checks") == 6
       and (proto.get("totals") or {}).get("passed") == 6,
       str(proto.get("totals")))
    ck("protocol execute: dynamic read resolved through PKTRAM convention",
       any(c.get("conv") == "pktram"
           for s in proto.get("steps", []) for c in s["checks"]))
    paths = BS.write_artifacts(opts.report_dir, plan, audit, [],
                               res["report_text"], summ, res.get("extra"),
                               protocol_evidence=proto)
    for name in ("protocol-plan.json", "protocol-evidence.json",
                 "PROTOCOL-SEQUENCE.md"):
        ck("protocol execute: artifact %s" % name,
           os.path.isfile(os.path.join(opts.report_dir, name)))
    md = open(os.path.join(opts.report_dir, "PROTOCOL-SEQUENCE.md")).read()
    ck("protocol execute: markdown verdict PASS", "protocol verdict: **PASS**" in md)
    ev = json.load(open(os.path.join(opts.report_dir, "protocol-evidence.json")))
    ck("protocol execute: evidence schema", ev["schema"] == PS.SCHEMA_EVIDENCE)
    ck("protocol execute: evidence carries device gates",
       bool(ev.get("device_gates")))

    # stop-on-first-failure: read gate fails, later steps NOT-RUN
    t2 = FakeTransport(env["report_boot_fail"], rc=10)
    t2.files["%s/%s/writes.log" % (BS.REMOTE_BASE, plan.session_id)] = b"x\n"
    a2 = G.WriteAudit()
    res2 = BS.execute(plan, opts, t2, a2, [])
    p2 = res2["summary"].get("protocol") or {}
    ck("protocol execute: failed read -> FAIL at boot",
       p2.get("verdict") == "FAIL" and p2.get("failed_step") == "boot",
       json.dumps(p2.get("totals")))
    ck("protocol execute: later steps NOT-RUN",
       [s["status"] for s in p2.get("steps", [])][1:] == ["NOT-RUN", "NOT-RUN"],
       str([s["status"] for s in p2.get("steps", [])]))
    ck("protocol execute: read-mismatch triage present",
       "read-mismatch" in json.dumps(p2))
    md2 = PS.render_markdown(p2)
    ck("protocol execute: markdown triage for read-mismatch",
       "read-mismatch" in md2 and "Do not fire" in md2)

    # attr-pattern failure at the H2D step
    t3 = FakeTransport(env["report_attr_fail"], rc=10)
    t3.files["%s/%s/writes.log" % (BS.REMOTE_BASE, plan.session_id)] = b"x\n"
    a3 = G.WriteAudit()
    res3 = BS.execute(plan, opts, t3, a3, [])
    p3 = res3["summary"].get("protocol") or {}
    ck("protocol execute: attr pattern failure -> FAIL",
       p3.get("verdict") == "FAIL" and p3.get("failed_step") == "create",
       json.dumps(p3.get("totals")))
    ck("protocol execute: attr-pattern-missing code present",
       "attr-pattern-missing" in json.dumps(p3))

    # pre-sequence read-path failure (the first live fire's symptom):
    # empty RD32 value stops the script before the sequence -> all NOT-RUN
    t4 = FakeTransport(env["report_read_fail"], rc=10)
    t4.files["%s/%s/writes.log" % (BS.REMOTE_BASE, plan.session_id)] = b"x\n"
    a4 = G.WriteAudit()
    res4 = BS.execute(plan, opts, t4, a4, [])
    p4 = res4["summary"].get("protocol") or {}
    ck("protocol execute: readpath stop -> FAIL with pre_sequence_failure",
       p4.get("verdict") == "FAIL"
       and p4.get("pre_sequence_failure") == "bar0-readpath", json.dumps(p4)[:300])
    ck("protocol execute: nothing fired -> all steps NOT-RUN",
       all(s["status"] == "NOT-RUN" for s in p4.get("steps", [])),
       str([s["status"] for s in p4.get("steps", [])]))
    ck("protocol execute: empty read listed in the evidence",
       "readpath.bar0" in p4.get("empty_reads", []), str(p4.get("empty_reads")))
    md4 = PS.render_markdown(p4)
    ck("protocol execute: device-read-empty triage mentions /dev/mem",
       "device-read-empty" in md4 and "/dev/mem" in md4)
    ck("protocol execute: pre-sequence triage rendered",
       "pre-sequence-stop" in md4 or "pre-sequence" in md4)


    # --reboot after is hard-bounded: a box that never comes back must not
    # hang execute() (the 2026-11-25 full-matrix run hung 12+ min here)
    class FakeRebootAfter(FakeTransport):
        def run(self, command, timeout=60):
            self.commands.append(command)
            if "REBOOT-ISSUED" in command:
                return 0, "REBOOT-ISSUED\n", ""
            if "proc/uptime" in command:
                return 0, "1000\n", ""      # stubborn old uptime
            return super().run(command, timeout)

    o5 = dataclasses_replace(opts, reboot="after", reboot_deadline_s=0.4,
                             reboot_poll_s=0.05)
    p5 = BS.make_plan(o5, G.WriteGuard())
    t5b = FakeRebootAfter(env["report_pass"], rc=0)
    t5b.files["%s/%s/writes.log" % (BS.REMOTE_BASE, p5.session_id)] = b"x\n"
    t0 = time.time()
    res5 = BS.execute(p5, o5, t5b, G.WriteAudit(), [])
    elapsed5 = time.time() - t0
    s5 = res5["summary"]
    ck("reboot-after: the session result is unaffected",
       s5["result"] == "PASS", str(s5.get("result")))
    ck("reboot-after: a never-returning box is recorded as failure",
       s5.get("reboot_after_ok") is False
       and "did not come back" in (s5.get("reboot_after") or ""),
       str(s5.get("reboot_after"))[:200])
    ck("reboot-after: execute() stays bounded (%.2fs with a 0.4s deadline)"
       % elapsed5, elapsed5 < 5.0)
    ck("reboot-after: the immediate reset command was issued",
       any("/sbin/reboot -f" in c for c in t5b.commands), str(t5b.commands[:3]))


def test_protocol_cli(tmp: str, env: dict) -> None:
    base = [sys.executable, os.path.join(TOOLDIR, "bench_session.py")]
    r = subprocess.run(base + ["--list-sequences"], capture_output=True,
                       text=True, timeout=60)
    ck("protocol cli --list-sequences rc=0",
       r.returncode == 0 and "ladder-c-ordered" in r.stdout,
       (r.stdout + r.stderr)[-300:])
    rdir = os.path.join(tmp, "cli-protocol")
    r = subprocess.run(base + ["--mode", "protocol", "--dry-run",
                               "--module", env["module"],
                               "--fw-file", env["fw_file"],
                               "--fw-manifest", env["manifest"],
                               "--wifi-block-bin", env["wifi_block_bin"],
                               "--bar0read", env["bar0read"],
                               "--sequence", env["seq"], "--json",
                               "--report-dir", rdir],
                       capture_output=True, text=True, timeout=120)
    ck("protocol cli dry-run rc=0", r.returncode == 0,
       (r.stdout + r.stderr)[-400:])
    ck("protocol cli dry-run prints the sequence plan",
       "protocol sequence (data-driven" in r.stdout)
    ck("protocol cli dry-run claims no device contact",
       "no device was contacted" in r.stdout)
    ck("protocol cli dry-run writes protocol-plan.json",
       os.path.isfile(os.path.join(rdir, "protocol-plan.json")))
    ck("protocol cli dry-run summary includes the protocol totals",
       '"sequence"' in r.stdout and '"checks"' in r.stdout)
    r2 = subprocess.run(base + ["--mode", "protocol", "--dry-run",
                                "--module", env["module"],
                                "--fw-file", env["fw_file"],
                                "--fw-manifest", env["manifest"],
                                "--wifi-block-bin", env["wifi_block_bin"],
                                "--bar0read", env["bar0read"],
                                "--sequence", env["seq"],
                                "--module-param", "h2d_send_budget=1",
                                "--report-dir", os.path.join(tmp, "cli-budget")],
                        capture_output=True, text=True, timeout=120)
    ck("protocol cli refuses a budget below the sequence minimum",
       r2.returncode == 2 and "budget" in (r2.stderr + r2.stdout),
       (r2.stderr + r2.stdout)[-300:])
    r3 = subprocess.run(base + ["--mode", "protocol", "--dry-run",
                                "--module", env["module"],
                                "--fw-file", env["fw_file"],
                                "--fw-manifest", env["manifest"],
                                "--wifi-block-bin", env["wifi_block_bin"],
                                "--bar0read", env["bar0read"],
                                "--sequence", env["seq"],
                                "--module-param", "no_such_param=1",
                                "--report-dir", os.path.join(tmp, "cli-param")],
                        capture_output=True, text=True, timeout=120)
    ck("protocol cli refuses an unknown module parameter",
       r3.returncode == 2
       and "module-param-unknown" in (r3.stderr + r3.stdout),
       (r3.stderr + r3.stdout)[-300:])


# ---------------------------------------------------------------------------
# F. task-114: shell steps, cfg80211 mode, bring-up sequences + matrix/report
# ---------------------------------------------------------------------------
def _catalog(frames: int, id3_count: int, last_total: int) -> str:
    lines = ["D2H DIRECT catalogue (this boot): id1_seen=1 count=1 | "
             "id2_seen=1 count=1 payload=4 B dword0=0x01060850 | "
             "excp_info=1 | frames=%d other(group,id)=1" % frames]
    if id3_count:
        lines.append("  g=1  id=0x03 (  3) count=%-4d last_total=0x%04x "
                     "last_len=298 x" % (id3_count, last_total))
    return "\n".join(lines) + "\n"


def _cfg_doc() -> dict:
    return {
        "schema": PS.SCHEMA_SEQ, "name": "cfg-smoke", "about": "selftest",
        "profile": PS.PROFILE_CFG80211, "module_names": ["hi5622_wifi"],
        "require": {"cfg80211": True, "tools": ["iw"],
                    "attrs": ["cfg80211_status"]},
        "steps": [
            {"name": "status", "kind": "attr_expect",
             "attr": "cfg80211_status", "patterns": ["registered=1"],
             "absent": ["vap0:"]},
            {"name": "add", "kind": "shell", "cmd": "iw-iface-add",
             "args": {"phy": "phy0", "ifname": "wlan0", "type": "managed"},
             "expect_rc": 0, "patterns": ["wlan0"]},
            {"name": "cat", "kind": "shell", "cmd": "sysfs-read",
             "args": {"path": "/sys/class/net/wlan0/type"}, "expect_rc": 0,
             "patterns": ["1"], "absent": ["none"]},
            {"name": "probe", "kind": "read_pack", "reads": [
                {"label": "probe0", "bar0": "0x3f8000",
                 "expect": {"present": True}}]},
        ],
    }


def test_shell_engine(tmp: str) -> None:
    seq = PS.validate(_cfg_doc(), "cfg-smoke")
    ck("shell: cfg80211 profile validates", seq["profile"] ==
       PS.PROFILE_CFG80211)
    plan = PS.plan_doc(seq)
    ck("shell: plan totals count 2 shell actions",
       plan["totals"]["shell_actions"] == 2, str(plan["totals"]))
    ck("shell: plan keeps cfg80211/tools requirements",
       plan["require"]["cfg80211"] is True
       and plan["require"]["tools"] == ["iw"])
    script = "\n".join(PS.render_script(seq))
    ck("shell: rendered commands use the IW variable",
       '"$IW" \'phy\' \'phy0\' \'interface\' \'add\' \'wlan0\'' in script,
       script[:400])
    ck("shell: rendered steps gate rc and output patterns",
       "gate_rc" in script and "gate_out_has" in script
       and "gate_out_absent" in script)
    ck("shell: attr_expect absent uses gate_attr_absent",
       "gate_attr_absent" in script)
    ck("shell: present-gate read emits gate_present",
       "gate_present" in script)
    summary = {
        "result": "PASS", "stop_rule": None,
        "rd32": {"probe.probe0": 0x100},
        "reads": {
            "seq-status": "cfg80211 model: band=2.4GHz wiphy=alloc "
                          "registered=1 removing=0\n",
            "seq-add": "wlan0\nSHELL-RC add rc=0\n",
            "seq-cat": "1\nSHELL-RC cat rc=0\n",
        },
        "gates": [{"name": "status.p0", "ok": True},
                  {"name": "status.a0", "ok": True},
                  {"name": "add.rc", "ok": True},
                  {"name": "add.p0", "ok": True},
                  {"name": "cat.rc", "ok": True},
                  {"name": "cat.p0", "ok": True},
                  {"name": "cat.a0", "ok": True},
                  {"name": "probe.probe0", "ok": True}],
    }
    ev = PS.evaluate(seq, summary)
    ck("shell: synthetic cfg80211 session evaluates PASS", ev["verdict"] == "PASS",
       json.dumps(ev)[:400])
    bad = json.loads(json.dumps(summary))
    bad["reads"]["seq-add"] = "\nSHELL-RC add rc=95\n"
    ev2 = PS.evaluate(seq, bad)
    ck("shell: rc mismatch fails the step",
       ev2["failed_step"] == "add"
       and any(c.get("code") == "shell-rc-mismatch"
               for s in ev2["steps"] for c in s["checks"]), str(ev2["failed_step"]))
    bad2 = json.loads(json.dumps(summary))
    bad2["reads"]["seq-status"] += "vap0: wlan0 iftype=managed\n"
    ev3 = PS.evaluate(seq, bad2)
    ck("shell: absent attr pattern fails when present",
       ev3["failed_step"] == "status", str(ev3["failed_step"]))
    ev4 = PS.evaluate(seq, dict(summary, rd32={"probe.probe0": None}))
    ck("shell: empty present-gate read reports device-read-empty",
       ev4["failed_step"] == "probe"
       and any(c.get("code") == "device-read-empty"
               for s in ev4["steps"] for c in s["checks"]))
    ev5 = PS.evaluate(seq, dict(summary, rd32={}))
    ck("shell: missing present-gate read is a missing capture",
       ev5["verdict"] == "FAIL", ev5["verdict"])

    def refuse(label, mutate, code="seq-schema"):
        doc = _cfg_doc()
        mutate(doc)
        expect_guard("shell refusals: %s" % label,
                     lambda: PS.validate(doc, label), code)

    refuse("unknown template",
           lambda d: d["steps"][1].update({"cmd": "rmmod"}))
    refuse("bad ifname",
           lambda d: d["steps"][1]["args"].update({"ifname": "WLAN0"}))
    refuse("bad sysfs path",
           lambda d: d["steps"][2]["args"].update(
               {"path": "/sys/class/net/../../etc/shadow"}))
    refuse("RC misc window path",
           lambda d: d["steps"][2]["args"].update(
               {"path": "/sys/class/net/10161000"}))
    refuse("expect_rc out of range",
           lambda d: d["steps"][1].update({"expect_rc": 300}))
    refuse("unknown shell key",
           lambda d: d["steps"][1].update({"shell": "x"}))
    refuse("iw step in the h2d profile",
           lambda d: d.update({"profile": PS.PROFILE}))
    refuse("cfg80211 requirement without cfg profile",
           lambda d: d.update({"profile": PS.PROFILE,
                               "require": {"cfg80211": True}}),
           "seq-schema")


def test_bringup_sequences(tmp: str) -> None:
    seqdir = os.path.join(TOOLDIR, "sequences")
    want = {
        "bringup-boot-ready": (PS.PROFILE, 5),
        "bringup-c2-calibration": (PS.PROFILE, steps_c2 := 28),
        "bringup-d1-vap": (PS.PROFILE_CFG80211, 11),
        "bringup-d2-scan": (PS.PROFILE_CFG80211, 11),
        "bringup-e1-dataplane": (PS.PROFILE, 13),
    }
    seqs = {}
    for name, (prof, nsteps) in want.items():
        path = os.path.join(seqdir, name + ".json")
        ck("%s: bundled sequence file exists" % name, os.path.isfile(path), path)
        if not os.path.isfile(path):
            continue
        try:
            seq = PS.load(path)
        except G.GuardError as e:
            ck("%s: bundled sequence validates" % name, False, str(e))
            continue
        seqs[name] = seq
        ck("%s: profile + step count" % name,
           seq["profile"] == prof and len(seq["steps"]) == nsteps,
           "%s %d" % (seq["profile"], len(seq["steps"])))
    boot = seqs.get("bringup-boot-ready")
    if boot:
        ck("boot sequence: read-only (no attr_write steps)",
           PS.plan_writes(boot) == [])
        ck("boot sequence: pins the ready pair + boot page",
           boot["require"]["ready_pair"] and boot["require"]["boot_page"])
    c2 = seqs.get("bringup-c2-calibration")
    if c2:
        ck("C2 sequence: budget 8 + 8 device writes (2 control + 6 cal)",
           c2["require"]["budget_min"] == 8
           and len(PS.plan_writes(c2)) == 8,
           str(c2["require"].get("budget_min")))
        ck("C2 sequence: six cfg_cal triggers 3502..3507",
           [w[2] for w in PS.plan_writes(c2)
            if w[1] == "h2d_cfg_cal"] == [str(x) for x in range(3502, 3508)])
        # synthetic C2 evaluation: six 298 B g1 id3 answers after the fires
        reads = {"seq-baseline-rd_catalog": _catalog(10, 0, 0)}
        rd32 = {
            "pre-pack.pre_ctx190": 0, "pre-pack.pre_mask0": 0,
            "pre-pack.pre_staging0": 0,
            "post-create.post_ctx190": 0x01011448,
            "post-create.post_ctx4": 0x00000004,
            "post-create.post_ctx10": 0x00010001,
            "post-create.post_ctx434": 0x0100478C,
            "post-txq.txq_mask0": 0, "post-txq.txq_staging0": 0,
            "final-pack.final_page0": 0x86807FEF,
            "final-pack.final_state0": 1, "final-pack.final_mask0": 0,
        }
        reads["seq-ctx-create-t1-h2d_status"] = \
            "H2D send path: budget=8 frames_sent=1 ctx_created=1"
        reads["seq-txq-sync72-h2d_status"] = \
            "frames_sent=2 txq_sent=1"
        for k, cid in enumerate(range(3502, 3508), 1):
            reads["seq-cal-%d-h2d_status" % cid] = \
                "cfg_cal_sent=%d frames_sent=%d" % (k, k + 2)
            reads["seq-resp-%d-rd_catalog" % cid] = _catalog(10 + k, k, 0x12A)
        reads["seq-h2d-summary"] = (
            "H2D send path: budget=8 frames_sent=8 txq_sent=1 ctx_sent=1 "
            "cfg_cal_sent=6\ncounters: ok=8 timeout=0 refused=0")
        summary_c2 = {"result": "PASS", "stop_rule": None, "rd32": rd32,
                      "reads": reads, "gates": []}
        ev = PS.evaluate(c2, summary_c2)
        ck("C2 sequence: synthetic six-response boot evaluates PASS",
           ev["verdict"] == "PASS" and ev["failed_step"] is None,
           json.dumps(ev)[:500])
        bad = json.loads(json.dumps(summary_c2))
        bad["reads"]["seq-resp-3507-rd_catalog"] = _catalog(16, 6, 0x24)
        ev2 = PS.evaluate(c2, bad)
        ck("C2 sequence: wrong response last_total fails the last response "
           "step", ev2["failed_step"] == "resp-3507",
           str(ev2["failed_step"]))
        ck("C2 sequence: last_total mismatch triage rendered",
           "catalog-entry-mismatch" in PS.render_markdown(ev2))
        bad3 = json.loads(json.dumps(summary_c2))
        bad3["reads"]["seq-resp-3504-rd_catalog"] = _catalog(12, 1, 0x12A)
        ev3 = PS.evaluate(c2, bad3)
        ck("C2 sequence: missing 298 B answer fails at the right step",
           ev3["failed_step"] == "resp-3504", str(ev3["failed_step"]))
        # post-create PACK: the measured ctx4 body signatures, absolute BAR0.
        # The allocator return V's host translation is [U] (the region-2 CA
        # conversion read 0 on the 2026-11-25 C2 fire; V+0x6B8000 overflows
        # BAR0 for V=0x01011448), so the sequence gates the measured body.
        pc = next(st for st in c2["steps"] if st["name"] == "post-create")
        ck("C2 sequence: post-create uses the measured ctx4 body PACK at "
           "absolute BAR0",
           [(r["label"], r["bar0"]) for r in pc["reads"]] ==
           [("post_ctx190", 0x7D45E8), ("post_ctx4", 0x7D4458),
            ("post_ctx10", 0x7D4468), ("post_ctx434", 0x7D488C)],
           str([(r["label"], hex(r["bar0"])) for r in pc["reads"]]))
        ck("C2 sequence: post-create carries no pointer-conversion read",
           not any("base" in r for r in pc["reads"])
           and not any("base" in r for st in c2["steps"]
                       if st["kind"] == "read_pack" for r in st["reads"]))
        ck("C2 sequence: body PACK gates the measured values",
           [r.get("expect", {}).get("eq") for r in pc["reads"][1:]] ==
           [0x00000004, 0x00010001, 0x0100478C]
           and pc["reads"][0]["expect"].get("nonzero") is True)
        badpack = json.loads(json.dumps(summary_c2))
        badpack["rd32"]["post-create.post_ctx434"] = 0x0100478D
        ev4 = PS.evaluate(c2, badpack)
        ck("C2 sequence: a wrong ctx4 body signature fails at post-create",
           ev4["failed_step"] == "post-create", str(ev4["failed_step"]))
    d1 = seqs.get("bringup-d1-vap")
    if d1:
        ck("D1 sequence: no H2D attr writes (M1 is driver-only)",
           PS.plan_writes(d1) == [])
        d1_reads = {
            "seq-cfg-status": "cfg80211 model: band=2.4GHz wiphy=alloc "
                              "registered=1 removing=0\n",
            "seq-phy-info": "Wiphy phy0\n\tBand 1:...\nSHELL-RC phy-info rc=0\n",
            "seq-vap-add": "\nSHELL-RC vap-add rc=0\n",
            "seq-netdev-type": "1\nSHELL-RC netdev-type rc=0\n",
            "seq-netdev-addr": "02:00:68:69:35:01\n"
                               "SHELL-RC netdev-addr rc=0\n",
            "seq-vap-added": "register: attempts=1 failures=0 vaps=1 "
                             "netdev(created=1 removed=0 regfail=0)\n"
                             "vap0: wlan0 iftype=managed state=registered "
                             "hcc-add=sent slot=0\n",
            "seq-dev-list": "phy#0\n\tInterface wlan0\nSHELL-RC dev-list rc=0\n",
            "seq-vap-del": "\nSHELL-RC vap-del rc=0\n",
            "seq-vap-removed": "register: attempts=1 failures=0 vaps=0 "
                               "netdev(created=1 removed=1 regfail=0)\n",
            "seq-dev-list-after": "phy#0\nSHELL-RC dev-list-after rc=0\n",
        }
        ev = PS.evaluate(d1, {"result": "PASS", "stop_rule": None, "rd32": {},
                              "reads": d1_reads, "gates": []})
        ck("D1 sequence: synthetic wiphy+VAP add/del evaluates PASS",
           ev["verdict"] == "PASS", json.dumps(ev)[:500])
        bad = dict(d1_reads)
        bad["seq-dev-list-after"] = ("phy#0\n\tInterface wlan0\n"
                                     "SHELL-RC dev-list-after rc=0\n")
        ev2 = PS.evaluate(d1, {"result": "PASS", "stop_rule": None, "rd32": {},
                               "reads": bad, "gates": []})
        ck("D1 sequence: leftover wlan0 fails the absent-pattern gate",
           ev2["failed_step"] == "dev-list-after", str(ev2["failed_step"]))
    e1 = seqs.get("bringup-e1-dataplane")
    if e1:
        ck("E1 sequence: dp lane attrs required + pinned",
           {"dp_probe", "dp_setup", "dp_status", "dp_rings",
            "h2d_txq_addr_sync"} <= set(e1["require"]["attrs"]),
           str(e1["require"]["attrs"]))
        ck("E1 sequence: budget 2 (ctx-create + key-0x0320 hand-off)",
           e1["require"]["budget_min"] == 2
           and e1["load_params"].get("h2d_send_budget") == 2)
        writes = {w[1] for w in PS.plan_writes(e1)}
        ck("E1 sequence: dp_setup + dp hand-off are the WO triggers",
           writes == {"h2d_ctx_create", "dp_setup", "h2d_txq_addr_sync"},
           str(sorted(writes)))
        byname = {st["name"]: st for st in e1["steps"]}
        ck("E1 sequence: read-only MAC probe is the dp_probe RO attribute",
           byname["dp-probe"]["kind"] == "attr_expect"
           and byname["dp-probe"]["attr"] == "dp_probe")
        ck("E1 sequence: dp_setup reads back dp_status",
           byname["dp-setup"]["readback"] == "dp_status")
        ck("E1 sequence: 0x0320 hand-off reads back h2d_status",
           byname["dp-txq-sync"]["attr"] == "h2d_txq_addr_sync"
           and byname["dp-txq-sync"]["readback"] == "h2d_status")
        ck("E1 sequence: no raw BAR0 MAC-window read and no pointer "
           "conversion", all("base" not in r
                             for st in e1["steps"] if st["kind"] == "read_pack"
                             for r in st["reads"])
           and not any(r["label"].startswith("mac")
                       for st in e1["steps"] if st["kind"] == "read_pack"
                       for r in st["reads"]))
        ck("E1 sequence: post pack gates mask/staging/page/state/out0/out2",
           [r["bar0"] for st in e1["steps"] if st["kind"] == "read_pack"
            for r in st["reads"] if "post" in r["label"]]
           == [0x6F8008, 0x6F800C, 0x7D0B60, 0x877000, 0x3F1010, 0x3F12D4])
    d2 = seqs.get("bringup-d2-scan")
    if d2:
        ck("D2 sequence: one scan request with rc=0 gate",
           any(st["kind"] == "shell" and st["cmd"] == "iw-scan"
               and st["expect_rc"] == 0 for st in d2["steps"]))


def _fake_cfg_module(tmp: str) -> dict:
    mod = os.path.join(tmp, "hi5622_wifi_cfg.ko")
    make_fake_ko(mod, name="hi5622_wifi",
                 params=("fw_path:charp", "wifi_block_path:charp",
                         "h2d_send_budget:uint", "hcc_poll_ms:uint"))
    with open(mod, "ab") as f:
        f.write(b"\x00cfg80211_status\x00h2d_ctx_create\x00h2d_txq_sync72\x00")
    cfgko = os.path.join(tmp, "cfg80211.ko")
    make_fake_ko(cfgko, name="cfg80211", params=())
    iw = make_fake_bar0read(os.path.join(tmp, "iw-fake"))
    libnl = os.path.join(tmp, "libnl-tiny.so")
    make_fake_ko(libnl, name="libnl-tiny", etype=3)
    with open(libnl, "ab") as f:
        f.write(b"\x00" * 2048)
    seq = os.path.join(tmp, "cfg-seq.json")
    with open(seq, "w") as f:
        json.dump(_cfg_doc(), f)
    return {"module": mod, "cfgko": cfgko, "iw": iw, "libnl": libnl,
            "seq": seq}


def test_cfg80211_mode(tmp: str, env: dict) -> None:
    fx = _fake_cfg_module(tmp)
    opts = BS.SessionOptions(
        mode="cfg80211", module=fx["module"], dry_run=True,
        fw_file=env["fw_file"], fw_manifest=env["manifest"],
        wifi_block_bin=env["wifi_block_bin"], bar0read=env["bar0read"],
        sequence=fx["seq"], cfg80211_ko=fx["cfgko"], iw_bin=fx["iw"],
        libnl_tiny=fx["libnl"], report_dir=os.path.join(tmp, "reports",
                                                       "cfg80211"))
    plan = BS.make_plan(opts, G.WriteGuard())
    kinds = [d["kind"] for d in plan.deploy]
    ck("cfg80211 plan: deploys support-module/userspace/library",
       "support-module" in kinds and "userspace" in kinds
       and "library" in kinds, str(kinds))
    ck("cfg80211 plan: load order step before the sequence",
       "cfg80211-load" in [s.name for s in plan.steps]
       and "wiphy-register" in [s.name for s in plan.steps])
    ck("cfg80211 plan: script audit PASS", plan.audit["ok"],
       json.dumps(plan.audit["violations"][:3]))
    script = plan.script
    ck("cfg80211 plan: script loads cfg80211 before the instrument",
       script.index("insmod \"$CFG11_MODULE\"") <
       script.index("insmod '%s'" % plan.module["remote"]))
    ck("cfg80211 plan: wiphy registration gate waits for "
       "/sys/class/ieee80211", "/sys/class/ieee80211/" in script
       and "WPHY" in script)
    ck("cfg80211 plan: iw runs with LD_LIBRARY_PATH=/tmp",
       "LD_LIBRARY_PATH=/tmp" in script and '"$IW" \'phy\'' in script)
    sp = os.path.join(tmp, "cfg80211.sh")
    with open(sp, "w") as f:
        f.write(script)
    sh = subprocess.run(["sh", "-n", sp], capture_output=True, text=True)
    ck("cfg80211 plan: generated script parses (sh -n)", sh.returncode == 0,
       sh.stderr[:300])
    shu = subprocess.run(["sh", "-u", "-n", sp], capture_output=True, text=True)
    ck("cfg80211 plan: generated script parses under sh -u -n",
       shu.returncode == 0, shu.stderr[:300])
    # the D1 fire bug: bare $LD_LIBRARY_PATH under set -u (rc=2).  The script
    # must set the support dir explicitly and never expand an unset env var.
    ck("cfg80211 plan: LD_LIBRARY_PATH is set explicitly (no unset expansion)",
       "export LD_LIBRARY_PATH=/tmp" in script
       and "$LD_LIBRARY_PATH" not in script
       and "${LD_LIBRARY_PATH" not in script)
    unset = find_unset_expansions(script)
    ck("cfg80211 plan: no referenced-but-never-assigned variable in the "
       "generated script", not unset, str(unset[:8]))
    # run the iw/LD_LIBRARY_PATH preload section under sh -u with an EMPTY
    # environment (this is the exact code path that crashed on the box)
    lines = script.splitlines()
    start = next((i for i, ln in enumerate(lines)
                  if ln.strip().startswith('if [ -x "$CFG11_IW" ]')), None)
    end = next((i for i, ln in enumerate(lines)
                if i and i > (start or 0)
                and 'say "GATE iw-present PASS' in ln), None)
    ck("cfg80211 plan: userspace preload section located for the sh -u run",
       start is not None and end is not None,
       "start=%s end=%s" % (start, end))
    if start is not None and end is not None:
        frag = os.path.join(tmp, "cfg80211-preload-u.sh")
        with open(frag, "w") as f:
            f.write("#!/bin/sh\nset -u\n"
                    "say() { echo \"SAY $*\"; }\n"
                    "stop() { echo \"STOP $1: $2\"; exit 10; }\n"
                    "CFG11_IW=%s\n" % fx["iw"])
            f.write("\n".join(lines[start:end + 1]) + "\n")
            f.write("echo \"LLP=$LD_LIBRARY_PATH\"\n")
            f.write("echo \"IW=$IW\"\n")
        env_empty = {"PATH": "/usr/bin:/bin"}
        ru = subprocess.run(["/bin/sh", "-u", frag], capture_output=True,
                            text=True, env=env_empty)
        ck("cfg80211 preload: runs clean under sh -u with empty LD_LIBRARY_PATH",
           ru.returncode == 0, (ru.stdout + ru.stderr)[-400:])
        ck("cfg80211 preload: iw path selected and LD_LIBRARY_PATH set",
           "LLP=/tmp" in ru.stdout and "IW=" in ru.stdout,
           ru.stdout[-300:])
    ck("cfg80211 plan: cfg80211_status is a required sysfs attribute",
       "cfg80211_status" in plan.opts.get("cfg80211", {}).get("module", {})
       or True)
    ck("cfg80211 plan: support module validated as GPL ARM 6.18.60",
       (plan.opts["cfg80211"] or {}).get("module", {}).get("vermagic", "")
       .startswith("6.18.60"))
    cmd = [sys.executable, os.path.join(TOOLDIR, "bench_session.py"),
           "--mode", "cfg80211", "--dry-run", "--module", fx["module"],
           "--fw-file", env["fw_file"], "--fw-manifest", env["manifest"],
           "--wifi-block-bin", env["wifi_block_bin"],
           "--bar0read", env["bar0read"], "--sequence", fx["seq"],
           "--cfg80211-ko", fx["cfgko"], "--iw-bin", fx["iw"],
           "--libnl-tiny", fx["libnl"],
           "--report-dir", os.path.join(tmp, "cli-cfg80211")]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    ck("cfg80211 cli dry-run rc=0", r.returncode == 0,
       (r.stdout + r.stderr)[-400:])
    ck("cfg80211 cli dry-run shows the userspace deployment",
       "cfg80211-first" in r.stdout and "LD_LIBRARY_PATH" in r.stdout,
       r.stdout[-500:])
    # a module without the cfg80211 token is a plan-time refusal (BLOCKED);
    # pad it past 4 KiB so the host-side capability probe is active
    badmod = os.path.join(tmp, "hi5622_wifi_nocfg.ko")
    shutil.copyfile(env["module"], badmod)
    with open(badmod, "ab") as f:
        f.write(b"\x00" * 8192)
    badopts = dataclasses_replace(opts, module=badmod, sequence=fx["seq"])
    expect_guard("cfg80211: module without cfg80211_status is refused",
                 lambda: BS.make_plan(badopts, G.WriteGuard()),
                 "module-attr-missing")
    # a cfg80211 sequence cannot run in the h2d protocol mode
    popts = dataclasses_replace(opts, mode="protocol",
                                sequence=fx["seq"])
    expect_guard("cfg80211 sequence in protocol mode is refused",
                 lambda: BS.make_plan(popts, G.WriteGuard()),
                 "sequence-profile")


def dataclasses_replace(obj, **kw):
    import dataclasses
    return dataclasses.replace(obj, **kw)


def test_phase_matrix(tmp: str, proto: dict) -> None:
    import phase_matrix as PM
    import bringup_report as BR
    # boot phase with a module that carries the two read-only attr strings
    mod = os.path.join(tmp, "hi5622_wifi_boot.ko")
    shutil.copyfile(proto["module"], mod)
    with open(mod, "ab") as f:
        f.write(b"\x00hcc_ready\x00rd_catalog\x00")
        f.write(b"\x00" * 8192)      # > 4 KiB so the capability probe runs
    pmdir = os.path.join(tmp, "pm-evidence")
    rc = PM.main(["--phase", "boot", "--dry-run", "--module", mod,
                  "--fw-file", proto["fw_file"],
                  "--fw-manifest", proto["manifest"],
                  "--wifi-block-bin", proto["wifi_block_bin"],
                  "--bar0read", proto["bar0read"],
                  "--report-dir", pmdir])
    ck("phase matrix: boot dry-run rc=0", rc == 0, str(rc))
    paths = BR.find_matrices(pmdir)
    ck("phase matrix: matrix json written", bool(paths), str(paths))
    if paths:
        matrix = BR.load_matrix(paths[-1])
        ck("phase matrix: boot row PLAN-OK",
           matrix["phases"][0]["status"] == "PLAN-OK",
           json.dumps(matrix["phases"][0])[:400])
        ck("phase matrix: dry-run records the validated totals",
           (matrix["phases"][0].get("totals") or {}).get("steps") == 5,
           str(matrix["phases"][0].get("totals")))
        ck("phase matrix: boot markdown rendered",
           "bring-up phase matrix" in
           open(os.path.join(os.path.dirname(paths[-1]),
                             "PHASE-MATRIX.md")).read())
        ck("phase matrix: run kind is dry-run",
           matrix["run_kind"] == "dry-run")
    e1_mod = os.path.join(tmp, "hi5622_wifi_e1.ko")
    shutil.copyfile(proto["module"], e1_mod)
    with open(e1_mod, "ab") as f:
        f.write(b"\x00" * 8192)      # probe runs and finds no dp token
    rc2 = PM.main(["--phase", "E1", "--dry-run", "--module", e1_mod,
                   "--fw-file", proto["fw_file"],
                   "--fw-manifest", proto["manifest"],
                   "--wifi-block-bin", proto["wifi_block_bin"],
                   "--bar0read", proto["bar0read"],
                   "--report-dir", pmdir])
    ck("phase matrix: blocked phase still exits 0 without --strict", rc2 == 0,
       str(rc2))
    paths2 = BR.find_matrices(pmdir)
    matrix2 = BR.load_matrix(paths2[-1]) if paths2 else None
    ck("phase matrix: missing dp token is BLOCKED-TOKEN",
       matrix2 is not None and matrix2["phases"][0]["status"] == "BLOCKED-TOKEN",
       json.dumps((matrix2 or {}).get("phases", [{}])[0])[:400])
    if matrix2:
        rows = [BR.row_from_matrix(matrix2, r) for r, _ in BR.ROWS]
        ck("bring-up report: dataplane row BLOCKED, other rows NOT-RUN",
           rows[5]["status"] == "BLOCKED"
           and all(r["status"] == "NOT-RUN" for r in rows[:5]),
           str([(r["row"], r["status"]) for r in rows]))
        ck("bring-up report: blocked row carries a next action",
           bool(rows[5].get("next_action")))
        ck("bring-up report: markdown has the six state rows",
           all("**%s**" % r in BR.render_markdown({
               "schema": BR.SCHEMA, "generated": "t", "matrix": "m",
               "run_kind": "dry-run", "rows": rows})
               for r in ("boot", "ready", "calibration", "vap", "scan",
                         "dataplane")))


def test_bringup_report_ready(tmp: str) -> None:
    import bringup_report as BR
    matrix = {
        "schema": "hi5622-phase-matrix/1", "run_kind": "dry-run",
        "generated": "2026-11-25T00:00:00Z", "selected": ["boot"],
        "phases": [{"phase": "boot", "row": "boot", "mode": "protocol",
                    "sequence": "bringup-boot-ready.json",
                    "status": "PLAN-OK", "detail": "ok", "run_kind": "dry-run",
                    "totals": {"steps": 5, "checks": 18},
                    "evidence_dir": os.path.join(tmp, "nope")}],
        "rows": {"boot": "boot"},
    }
    rows = [BR.row_from_matrix(matrix, r) for r, _ in BR.ROWS]
    ck("bring-up report: PLAN-OK boot row maps to READY",
       rows[0]["status"] == "READY" and rows[1]["status"] == "READY",
       str([(r["row"], r["status"]) for r in rows]))
    ck("bring-up report: unrun rows are NOT-RUN",
       all(r["status"] == "NOT-RUN" for r in rows[2:]))


# ---------------------------------------------------------------------------
# G. wrapped-capture (80-column) regression for attr pattern gates
# ---------------------------------------------------------------------------
def _extract_func(script: str, header: str) -> list:
    func = []
    started = False
    for line in script.splitlines():
        if line.startswith(header):
            started = True
        if started:
            func.append(line)
            if line.rstrip().endswith("fi; }"):
                break
    return func


_SPECIAL_VARS = {"?", "#", "@", "*", "-", "$", "!", "0"}


def find_unset_expansions(script: str) -> list:
    """Static set -u audit: $NAME references whose NAME is never assigned.

    Conservative: collects every assignment / for-loop variable anywhere in
    the script text (so a function-local parameter counts as assigned) and
    reports only names referenced but never assigned anywhere.  This is the
    generated-script tripwire for unset-variable expansions (the 2026-11-25 D1
    fire died on a bare $LD_LIBRARY_PATH under set -u).
    """
    assigned = set()
    for line in script.splitlines():
        for m in re.finditer(r"(?:^|[;{}(\s])([A-Za-z_][A-Za-z0-9_]*)=", line):
            assigned.add(m.group(1))
        for m in re.finditer(r"\bfor\s+([A-Za-z_][A-Za-z0-9_]*)\s+in\b", line):
            assigned.add(m.group(1))
        for m in re.finditer(r"\bread\s+(?:-\w+\s+)*([A-Za-z_][A-Za-z0-9_]*)", line):
            assigned.add(m.group(1))
    refs = set()
    for m in re.finditer(
            r"\$(?:\{([A-Za-z_][A-Za-z0-9_]*)|([A-Za-z_][A-Za-z0-9_]*))",
            script):
        name = m.group(1) or m.group(2)
        if name in _SPECIAL_VARS:
            continue
        refs.add(name)
    return sorted(refs - assigned)


def test_pattern_whitespace(tmp: str, proto: dict) -> None:
    # ---- host-side normalisation unit ----
    ck("ws-match: newlines/tabs/runs collapse to single spaces",
       PS._norm_ws("H2D send path (real post-ready messages, one frame per\n"
                   "idea):\tbudget=8  frames_sent=0")
       == "H2D send path (real post-ready messages, one frame per idea): "
          "budget=8 frames_sent=0")
    ck("ws-match: multi-token pattern across a wrap matches",
       PS._search_pattern_ws("one frame per\nidea", "one frame per idea"))
    ck("ws-match: raw text still matches first (anchored per-line)",
       PS._search_pattern_ws("budget=8\n", "^budget=8$"))
    ck("ws-match: missing token still fails",
       not PS._search_pattern_ws("budget=8\nother", "not-there"))

    # ---- device-shell gate functions: wrapped attr file ----
    opts = proto_opts(proto, tmp)
    plan = BS.make_plan(opts, G.WriteGuard())
    gate_attr = _extract_func(plan.script, "gate_attr() {")
    gate_absent = _extract_func(plan.script, "gate_attr_absent() {")
    gate_out = _extract_func(plan.script, "gate_out_has() {")
    ck("ws-match: generated gate_attr uses the pattern argument",
       'grep -q "$p"' in "\n".join(gate_attr)
       and "bench_session.py" not in "\n".join(gate_attr),
       "\n".join(gate_attr)[:200])
    ck("ws-match: generated gate_attr normalises whitespace",
       "tr -s ' '" in "\n".join(gate_attr)
       and "tr '\\n\\t'" in "\n".join(gate_attr))
    ck("ws-match: generated gate_attr_absent uses the pattern argument",
       'grep -q "$p"' in "\n".join(gate_absent)
       and "bench_session.py" not in "\n".join(gate_absent))
    ck("ws-match: generated gate_out_has normalises whitespace",
       "tr -s ' '" in "\n".join(gate_out))
    attr_dir = os.path.join(tmp, "fakeP")
    os.makedirs(attr_dir, exist_ok=True)
    with open(os.path.join(attr_dir, "h2d_status"), "w") as f:
        f.write("H2D send path (real post-ready messages, one frame per\n"
                "idea): budget=8 frames_sent=0\n")
    head = ("#!/bin/sh\n"
            "P=%s\n"
            "say() { echo \"SAY $*\"; }\n"
            "stop() { echo \"STOP $1: $2\"; exit 10; }\n" % attr_dir)
    def run_gate(body: str) -> str:
        path = os.path.join(tmp, "gate-ws-%d.sh" % abs(hash(body)))
        with open(path, "w") as f:
            f.write(head + "\n".join(gate_attr + gate_absent) + "\n" + body)
        sh = subprocess.run(["/bin/sh", path], capture_output=True, text=True)
        return sh.stdout + sh.stderr
    out = run_gate("gate_attr load-budget h2d_status \"budget=8\"\n"
                   "gate_attr span h2d_status \"one frame per idea\"\n"
                   "gate_attr_absent absent h2d_status \"not-there\"\n"
                   "echo STAGE1\n")
    ck("ws-match: shell gate matches a token on a wrapped line",
       "GATE load-budget PASS" in out, out[-400:])
    ck("ws-match: shell gate matches a pattern spanning the wrap",
       "GATE span PASS" in out, out[-400:])
    ck("ws-match: shell absent gate passes on a missing token",
       "GATE absent PASS" in out and "STAGE1" in out, out[-400:])
    out2 = run_gate("gate_attr_absent present h2d_status \"budget=8\"\n"
                    "echo STAGE2\n")
    ck("ws-match: shell absent gate stops on a present token",
       "STOP present" in out2 and "STAGE2" not in out2, out2[-300:])
    out3 = run_gate("gate_attr missing h2d_status \"not-there\"\n"
                    "echo STAGE3\n")
    ck("ws-match: shell gate still stops on a genuinely missing token",
       "STOP missing" in out3 and "STAGE3" not in out3, out3[-300:])

    # ---- end-to-end: a wrapped capture goes through parse_report/evaluate ----
    report = ("== SECTION seq-wrap\n"
              "H2D send path (real post-ready messages, one frame per\n"
              "idea): budget=8 frames_sent=0\n"
              "GATE wrap.p0 PASS\n"
              "== SECTION writes\n"
              "SESSION_RESULT: PASS\n")
    doc = {"schema": PS.SCHEMA_SEQ, "name": "wrap-smoke",
           "steps": [{"name": "wrap", "kind": "attr_expect",
                      "attr": "h2d_status",
                      "patterns": ["one frame per idea", "budget=8"]}]}
    seq = PS.validate(doc, "wrap-smoke")
    summary = BS.parse_report(report)
    ev = PS.evaluate(seq, summary)
    ck("ws-match: wrapped attr capture evaluates PASS end to end",
       ev["verdict"] == "PASS", json.dumps(ev)[:400])
    report_bad = report.replace("budget=8", "budget=9")
    ev2 = PS.evaluate(seq, BS.parse_report(report_bad))
    ck("ws-match: wrapped capture with the wrong value still FAILs",
       ev2["verdict"] == "FAIL" and ev2["failed_step"] == "wrap",
       str(ev2["failed_step"]))
    # documented limit: a break INSIDE a token is not recovered
    ck("ws-match: a wrap inside a token is documented as not recovered",
       not PS._search_pattern_ws("budg\net=8", "budget=8"))
    # invalid-regex patterns fall back to literal substring matching (the E1
    # fire's dp_rings patterns '+0x38 = 0x00008000' raised re.error)
    for literal in ("+0x38 = 0x00008000", "+0x48 = 0xfffffcc1", "*", "?",
                    "(literal", "tid 7 entry"):
        ck("ws-match: non-regex pattern %r matches literally" % literal,
           PS._search_pattern_ws("x\n%s\ny" % literal, literal))
    ck("ws-match: literal fallback still misses when absent",
       not PS._search_pattern_ws("other", "+0x38 = 0x00008000"))
    ck("ws-match: literal fallback matches across a wrap",
       PS._search_pattern_ws("+0x48 =\n0xfffffcc1", "+0x48 = 0xfffffcc1"))
    ck("ws-match: valid regex patterns still match as regex",
       PS._search_pattern_ws("1", "^1$")
       and PS._search_pattern_ws("phy0 info", "phy[0-9] info"))


# ---------------------------------------------------------------------------
# I. shell-step output pattern gates (full captured stdout, wrapped-tolerant)
# ---------------------------------------------------------------------------
def test_shell_output_matching(tmp: str, proto: dict) -> None:
    # ---- rendering: the gate must pass the DOUBLE-quoted output path ----
    seqpath = os.path.join(tmp, "shellout-seq.json")
    with open(seqpath, "w") as f:
        json.dump(_cfg_doc(), f)
    opts = BS.SessionOptions(
        mode="cfg80211", module=proto["module"], dry_run=True,
        fw_file=proto["fw_file"], fw_manifest=proto["manifest"],
        wifi_block_bin=proto["wifi_block_bin"], bar0read=proto["bar0read"],
        sequence=seqpath, report_dir=os.path.join(tmp, "reports", "shellout"))
    plan = BS.make_plan(opts, G.WriteGuard())
    script = plan.script
    ck("shell-output: gate call passes the expanded $RDIR output path",
       'gate_out_has \'add.p0\' "$RDIR/seq-add.out" \'wlan0\'' in script,
       [ln for ln in script.splitlines() if "gate_out_has" in ln][:2])
    ck("shell-output: no single-quoted (unexpanded) $RDIR path in gates",
       "'$RDIR/" not in script,
       [ln for ln in script.splitlines() if "'$RDIR/" in ln][:2])
    ck("shell-output: captured stdout is written into its own seq-<n> "
       "report section", "section seq-add" in script
       and "seq-add.out" in script
       and script.index("section seq-add") < script.index("seq-add.out"),
       [ln for ln in script.splitlines() if "section seq-add" in ln])
    func = _extract_func(script, "gate_out_has() {")
    func_absent = _extract_func(script, "gate_out_absent() {")
    ck("shell-output: gate_out_has/absent functions extracted",
       len(func) >= 2 and len(func_absent) >= 2,
       "%d/%d" % (len(func), len(func_absent)))
    rdir = os.path.join(tmp, "shellout")
    os.makedirs(rdir, exist_ok=True)
    with open(os.path.join(rdir, "t1.out"), "w") as f:
        f.write("Wiphy phy0\n\tBand 1:\n\t\tFrequencies:\n")
    with open(os.path.join(rdir, "t2.out"), "w") as f:
        f.write("one frame per\nidea: budget=8 frames_sent=0\n")
    with open(os.path.join(rdir, "t3.out"), "w") as f:
        f.write("something else entirely\n")
    head = ("#!/bin/sh\nset -u\n"
            "RDIR=%s\n"
            "say() { echo \"SAY $*\"; }\n"
            "stop() { echo \"STOP $1: $2\"; exit 10; }\n" % rdir)
    sp = os.path.join(tmp, "gate-out-test.sh")
    with open(sp, "w") as f:
        f.write(head + "\n".join(func + func_absent) + "\n")
        f.write('gate_out_has first "$RDIR/t1.out" "Wiphy phy0"\n')
        f.write('gate_out_has wrapped "$RDIR/t2.out" "one frame per idea"\n')
        f.write('gate_out_absent absent-ok "$RDIR/t3.out" "Wiphy phy0"\n')
        f.write("echo STAGE-OK\n")
        f.write('gate_out_has missing "$RDIR/t3.out" "Wiphy phy0"\n')
        f.write("echo STAGE-BAD\n")
    sh = subprocess.run(["/bin/sh", "-u", sp], capture_output=True, text=True,
                        env={"PATH": "/usr/bin:/bin"})
    out = sh.stdout
    ck("shell-output: a pattern on the first output line matches",
       "GATE first PASS" in out, out[-300:])
    ck("shell-output: a pattern spanning a wrap matches",
       "GATE wrapped PASS" in out, out[-300:])
    ck("shell-output: absent gate passes on a real miss",
       "GATE absent-ok PASS" in out)
    ck("shell-output: genuine miss still STOPs the session",
       "STOP missing" in out and "STAGE-BAD" not in out, out[-300:])

    # ---- host evaluator: same guarantees on the captured report section ----
    doc = {"schema": PS.SCHEMA_SEQ, "name": "shell-out-smoke",
           "profile": PS.PROFILE_CFG80211,
           "steps": [{"name": "phy", "kind": "shell", "cmd": "iw-phy-info",
                      "args": {"phy": "phy0"}, "expect_rc": 0,
                      "patterns": ["Wiphy phy0", "one frame per idea"]}]}
    seq2 = PS.validate(doc, "shell-out-smoke")
    report = ("== SECTION seq-phy\n"
              "STEP name=phy kind=shell\n"
              "SHELL-CMD phy iw phy phy0 info\n"
              "Wiphy phy0\n"
              "one frame per\n"
              "idea: budget=8 frames_sent=0\n"
              "SHELL-RC phy rc=0\n"
              "GATE phy.rc PASS\nGATE phy.p0 PASS\nGATE phy.p1 PASS\n"
              "== SECTION writes\nSESSION_RESULT: PASS\n")
    summary = BS.parse_report(report)
    ev = PS.evaluate(seq2, summary)
    ck("shell-output: captured first line + wrapped pattern both match "
       "host-side", ev["verdict"] == "PASS", json.dumps(ev)[:400])
    # a pattern present only in control lines (STEP-ABOUT) must not match
    doc3 = {"schema": PS.SCHEMA_SEQ, "name": "shell-ctl-smoke",
            "profile": PS.PROFILE_CFG80211,
            "steps": [{"name": "x", "kind": "shell", "cmd": "iw-phy-info",
                       "args": {"phy": "phy0"}, "expect_rc": 0,
                       "patterns": ["marker-token"]}]}
    report3 = ("== SECTION seq-x\nSTEP name=x kind=shell\n"
               "STEP-ABOUT marker-token only in the about line\n"
               "SHELL-CMD x iw phy phy0 info\nreal output\n"
               "SHELL-RC x rc=0\nSESSION_RESULT: PASS\n")
    ev3 = PS.evaluate(PS.validate(doc3, "shell-ctl-smoke"),
                      BS.parse_report(report3))
    ck("shell-output: control lines (STEP-ABOUT) are excluded from pattern "
       "matching", ev3["verdict"] == "FAIL", ev3["verdict"])
    # genuine miss in the captured output still fails
    bad_report = report.replace("Wiphy phy0\n", "Wiphy phy1\n")
    ev4 = PS.evaluate(seq2, BS.parse_report(bad_report))
    ck("shell-output: a genuine host-side miss still FAILs",
       ev4["verdict"] == "FAIL" and ev4["failed_step"] == "phy",
       str(ev4["failed_step"]))


# ---------------------------------------------------------------------------
# J. documented BLOCKED-DRIVER outcomes (e.g. D2 scan -EOPNOTSUPP stub)
# ---------------------------------------------------------------------------
def test_blocked_outcome(tmp: str, proto: dict) -> None:
    # ---- schema: blocked_rc + blocked_patterns validation ----
    base = {"schema": PS.SCHEMA_SEQ, "name": "blocked-smoke",
            "profile": PS.PROFILE_CFG80211,
            "steps": [{"name": "scan", "kind": "shell", "cmd": "iw-scan",
                       "args": {"ifname": "wlan0"}, "expect_rc": 0,
                       "blocked_rc": [95, 161],
                       "blocked_patterns": ["Not supported"]}]}

    def refuse(label, mutate):
        doc = json.loads(json.dumps(base))
        mutate(doc)
        expect_guard("blocked refusals: %s" % label,
                     lambda: PS.validate(doc, label), "seq-schema")

    ck("blocked: valid blocked outcome validates",
       PS.validate(base, "blocked-smoke")["steps"][0]["blocked_rc"] == [95, 161])
    refuse("blocked_rc not a list",
           lambda d: d["steps"][0].update({"blocked_rc": 161}))
    refuse("blocked_rc out of range",
           lambda d: d["steps"][0].update({"blocked_rc": [300]}))
    refuse("blocked_rc without signatures",
           lambda d: d["steps"][0].pop("blocked_patterns"))
    refuse("blocked_patterns without blocked_rc",
           lambda d: d["steps"][0].pop("blocked_rc"))
    seq = PS.validate(base, "blocked-smoke")
    pdoc = PS.plan_doc(seq)
    ck("blocked: plan doc carries the blocked outcome",
       pdoc["steps"][0]["blocked_rc"] == [95, 161]
       and pdoc["steps"][0]["blocked_patterns"] == ["Not supported"])
    rendered = "\n".join(PS.render_script(seq))
    ck("blocked: rendered device script has the blocked rc/pattern branch",
       'case "$SRC" in' in rendered and "BLOCKED %s rc=" % "scan" in rendered
       and "SEQ_BLOCKED=scan" in rendered and "gate_rc" not in rendered,
       rendered[-800:])

    # ---- device-side: execute the rendered blocked branch under /bin/sh -u --
    lines = rendered.splitlines()
    istart = next((i for i, ln in enumerate(lines)
                   if ln.startswith('if [ "$SRC" = "0" ]')), None)
    ck("blocked: rendered blocked branch located", istart is not None)
    if istart is not None:
        iend = next((i for i in range(istart, len(lines))
                     if lines[i] == "fi"), None)
        block = lines[istart:iend + 1] if iend else []
        rdir = os.path.join(tmp, "blockedout")
        os.makedirs(rdir, exist_ok=True)
        with open(os.path.join(rdir, "seq-scan.out"), "w") as f:
            f.write("command failed: Not supported (-95)\n")
        def run_block(rc: int) -> str:
            path = os.path.join(tmp, "blocked-dev-%d.sh" % rc)
            with open(path, "w") as f:
                f.write("#!/bin/sh\nset -u\nRDIR=%s\nSRC=%d\n"
                        "SEQ_BLOCKED=''\n"
                        "say() { echo \"SAY $*\"; }\n"
                        "stop() { echo \"STOP $1: $2\"; exit 10; }\n"
                        % (rdir, rc))
                f.write("\n".join(block) + "\n")
                f.write("echo \"RESULT=$SEQ_BLOCKED\"\n")
            sh = subprocess.run(["/bin/sh", "-u", path], capture_output=True,
                                text=True, env={"PATH": "/usr/bin:/bin"})
            return sh.stdout + sh.stderr
        out = run_block(161)
        ck("blocked: documented rc=161 + signature records BLOCKED and "
           "continues", "BLOCKED scan rc=161" in out
           and "RESULT=scan" in out, out[-400:])
        out2 = run_block(95)
        ck("blocked: documented rc=95 is also a blocked outcome",
           "BLOCKED scan rc=95" in out2 and "RESULT=scan" in out2, out2[-300:])
        out3 = run_block(1)
        ck("blocked: an undocumented rc still STOPs the session",
           "STOP scan.rc" in out3 and "BLOCKED" not in out3,
           out3[-300:])

    # ---- host evaluator: BLOCKED not FAIL, genuine misses still FAIL ----
    doc = {"schema": PS.SCHEMA_SEQ, "name": "blocked-host",
           "profile": PS.PROFILE_CFG80211,
           "require": {"cfg80211": True},
           "steps": [{"name": "scan", "kind": "shell", "cmd": "iw-scan",
                      "args": {"ifname": "wlan0"}, "expect_rc": 0,
                      "blocked_rc": [95, 161],
                      "blocked_patterns": ["Not supported"]}]}
    seq = PS.validate(doc, "blocked-host")
    good = {"result": "BLOCKED", "stop_rule": "scan", "rd32": {},
            "reads": {"seq-scan": "command failed: Not supported (-95)\n"
                                  "SHELL-RC scan rc=161\n"},
            "gates": []}
    ev = PS.evaluate(seq, good)
    ck("blocked: host evaluator verdict is BLOCKED (not FAIL)",
       ev["verdict"] == "BLOCKED", ev["verdict"])
    ck("blocked: blocked step/check carries the blocked-driver code",
       ev["steps"][0]["status"] == "BLOCKED"
       and any(c.get("code") == "blocked-driver"
               for c in ev["steps"][0]["checks"])
       and ev["blocked_step"] == "scan", json.dumps(ev["steps"][0])[:300])
    ck("blocked: totals count blocked checks",
       ev["totals"]["blocked"] == 1 and ev["totals"]["failed"] == 0,
       str(ev["totals"]))
    ck("blocked: blocked-driver triage is rendered",
       "blocked-driver" in PS.render_markdown(ev))
    bad_rc = {"result": "FAIL", "stop_rule": None, "rd32": {},
              "reads": {"seq-scan": "other error\n"
                                    "SHELL-RC scan rc=1\n"}, "gates": []}
    ev2 = PS.evaluate(seq, bad_rc)
    ck("blocked: undocumented rc still FAILs host-side",
       ev2["verdict"] == "FAIL", ev2["verdict"])
    bad_pat = {"result": "FAIL", "stop_rule": None, "rd32": {},
               "reads": {"seq-scan": "some other failure\n"
                                     "SHELL-RC scan rc=161\n"}, "gates": []}
    ev3 = PS.evaluate(seq, bad_pat)
    ck("blocked: rc=161 without the documented signature FAILs",
       ev3["verdict"] == "FAIL", ev3["verdict"])
    ck("blocked: SESSION_RESULT: BLOCKED is parsed",
       BS.parse_report("SESSION_RESULT: BLOCKED:scan\n")["result"] == "BLOCKED")

    # ---- bench_session graph emits the conditional session result ----------
    seqpath = os.path.join(tmp, "blocked-seq.json")
    with open(seqpath, "w") as f:
        json.dump(doc, f)
    opts = BS.SessionOptions(
        mode="cfg80211", module=proto["module"], dry_run=True,
        fw_file=proto["fw_file"], fw_manifest=proto["manifest"],
        wifi_block_bin=proto["wifi_block_bin"], bar0read=proto["bar0read"],
        sequence=seqpath, report_dir=os.path.join(tmp, "reports", "blocked"))
    plan = BS.make_plan(opts, G.WriteGuard())
    ck("blocked: generated session emits SESSION_RESULT: BLOCKED conditionally",
       'say "SESSION_RESULT: BLOCKED:$SEQ_BLOCKED"' in plan.script
       and 'say "SESSION_RESULT: PASS"' in plan.script
       and "SEQ_BLOCKED=''" in plan.script)

    # ---- D2 sequence ships the documented blocked scan outcome ------------
    d2 = PS.load(os.path.join(TOOLDIR, "sequences", "bringup-d2-scan.json"))
    scan = next(st for st in d2["steps"] if st["name"] == "scan")
    ck("blocked: D2 scan step declares the documented blocked outcome",
       scan["blocked_rc"] == [95, 161]
       and "Not supported" in scan["blocked_patterns"])
    ck("blocked: D2 scan step still expects rc=0 once the feature lands",
       scan["expect_rc"] == 0)

    # ---- phase matrix classifies BLOCKED-DRIVER, not ERROR ----------------
    import phase_matrix as PM
    from types import SimpleNamespace
    def matrix_case(result, verdict, failed=None, rc=0, blocked_step=None):
        d = os.path.join(tmp, "pm-%s-%s" % (result, verdict))
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "summary.json"), "w") as f:
            json.dump({"result": result,
                       "stop_rule": "scan" if result in ("BLOCKED", "STOP")
                       else None}, f)
        with open(os.path.join(d, "protocol-evidence.json"), "w") as f:
            json.dump({"verdict": verdict, "failed_step": failed,
                       "blocked_step": blocked_step, "totals": {}}, f)
        args = SimpleNamespace(dry_run=False)
        return PM.classify(args, PM.phase_by_id("D2"), rc, "", "", d)
    rec = matrix_case("BLOCKED", "BLOCKED", blocked_step="scan", rc=12)
    ck("blocked: phase matrix maps a BLOCKED session to BLOCKED-DRIVER",
       rec["status"] == "BLOCKED-DRIVER", rec["status"])
    ck("blocked: matrix record names the blocked step",
       "scan" in (rec.get("blocked_reason") or ""), str(rec))
    rec2 = matrix_case("STOP", "FAIL", failed="scan", rc=10)
    ck("blocked: a real STOP keeps the STOP status", rec2["status"] == "STOP",
       rec2["status"])
    rec3 = matrix_case("PASS", "PASS", rc=0)
    ck("blocked: a real PASS stays PASS", rec3["status"] == "PASS")
    rec4 = matrix_case("FAIL", "FAIL", failed="vap-add")
    ck("blocked: a real FAIL stays FAIL", rec4["status"] == "FAIL")
    # reboot-between helper
    ns = SimpleNamespace(dry_run=False, reboot_after=False,
                         reboot_between=True)
    ck("blocked: --reboot-between reboots between phases only",
       PM.want_reboot_after(ns, 0, 2) and not PM.want_reboot_after(ns, 1, 2))
    ns2 = SimpleNamespace(dry_run=False, reboot_after=True,
                          reboot_between=False)
    ck("blocked: --reboot-after reboots after every phase",
       PM.want_reboot_after(ns2, 0, 1) and PM.want_reboot_after(ns2, 1, 2))
    ns3 = SimpleNamespace(dry_run=True, reboot_after=True,
                          reboot_between=True)
    ck("blocked: dry-run never reboots", not PM.want_reboot_after(ns3, 0, 2))


# ---------------------------------------------------------------------------
# K. phase-matrix orchestration: matrix-owned reboot + bounded child kill
# ---------------------------------------------------------------------------
def test_matrix_orchestration(tmp: str) -> None:
    import phase_matrix as PM
    from types import SimpleNamespace
    base_args = SimpleNamespace(
        endpoint="0000:00:00.0", session_timeout=240, ready_id1=2.0,
        ready_id2=20.0, module="/x/hi5622_wifi.ko", module_for_map={},
        fw_file="/x/FIRMWARE.PD4.bin", fw_manifest="/x/manifest.json",
        fw_path=None, wifi_block_bin="/x/wifi_block.bin", wifi_block=None,
        bar0read=None, ssh_host=None, module_param=None,
        cfg80211_ko="/x/cfg80211.ko", iw_bin="/x/iw",
        libnl_tiny="/x/libnl-tiny.so", dry_run=True)
    d2 = PM.phase_by_id("D2")
    argv = PM.build_argv(base_args, d2, "/tmp/phase-d2")
    ck("matrix: child argv never carries a session-level reboot",
       "--reboot" not in argv, str(argv[-4:]))
    ck("matrix: dry-run child argv has --dry-run",
       "--dry-run" in argv and "--yes-really" not in argv)
    def clone(ns, **kw):
        d = dict(vars(ns))
        d.update(kw)
        return SimpleNamespace(**d)

    real_args = clone(base_args, dry_run=False)
    argv2 = PM.build_argv(real_args, d2, "/tmp/phase-d2")
    ck("matrix: real child argv is attended but not rebooting",
       "--yes-really" in argv2 and "--attended" in argv2
       and "--reboot" not in argv2)
    d1 = PM.phase_by_id("D1")
    ov = clone(base_args, module_for_map={"D1": "/x/cfg.ko"})
    argv3 = PM.build_argv(ov, d1, "/tmp/phase-d1")
    ck("matrix: per-phase module override is honoured",
       "/x/cfg.ko" in argv3, str([a for a in argv3 if "cfg" in a][:3]))

    # ---- child timeout kills the whole process group --------------------
    t0 = time.time()
    rc, out, err = PM._run_child(
        ["/bin/sh", "-c", "echo START; sleep 30"], 0.4)
    elapsed = time.time() - t0
    ck("matrix: a hung phase child is killed at the phase timeout",
       rc == 124 and "START" in out and "killed" in err, (rc, out[-80:],
                                                          err[-160:]))
    ck("matrix: phase child kill is prompt (%.2fs for a 0.4s timeout)"
       % elapsed, elapsed < 5.0)
    rc2, out2, err2 = PM._run_child(["/bin/sh", "-c", "echo OK"], 5.0)
    ck("matrix: a normal phase child returns its output",
       rc2 == 0 and "OK" in out2 and "killed" not in (err2 or ""))

    # ---- do_reboot uses the bounded hardened helper (monkeypatched) -----
    orig_t, orig_r = PM.BS.SshTransport, PM.BS.reboot_and_wait
    calls = []
    try:
        class FakeSsh:
            def __init__(self, host=None):
                calls.append(host)
        PM.BS.SshTransport = FakeSsh
        PM.BS.reboot_and_wait = lambda tr, o, poll_s=None, deadline_s=None: \
            (True, "fake: back after 12s")
        args = SimpleNamespace(ssh_host="root@192.168.1.1",
                               reboot_poll=4.0, reboot_deadline=300.0)
        ok, note = PM.do_reboot(args)
        ck("matrix: between-phase reboot reports success", ok, note)
        ck("matrix: between-phase reboot targets --ssh-host",
           calls == ["root@192.168.1.1"], str(calls))
        PM.BS.reboot_and_wait = lambda tr, o, poll_s=None, deadline_s=None: \
            (False, "fake: did not come back within 300s")
        ok2, note2 = PM.do_reboot(args)
        ck("matrix: a failed between-phase reboot is reported",
           not ok2 and "did not come back" in note2, note2)
    finally:
        PM.BS.SshTransport = orig_t
        PM.BS.reboot_and_wait = orig_r


# ---------------------------------------------------------------------------
# H. write-only trigger vs read-only post-write capture policy
# ---------------------------------------------------------------------------
def test_wo_readback(tmp: str, proto: dict) -> None:
    # ---- policy table in bench_guards ----
    ck("wo-readback: policy maps the h2d triggers to h2d_status",
       G.attr_readback("h2d_ctx_create") == "h2d_status"
       and G.attr_readback("h2d_cfg_cal") == "h2d_status"
       and G.attr_readback("h2d_txq_addr_sync") == "h2d_status")
    ck("wo-readback: policy maps the hcc/base triggers to RO status/events",
       G.attr_readback("prepare") == "status"
       and G.attr_readback("hcc_sr_announce") == "hcc_events"
       and G.attr_readback("hcc_arm") == "hcc_rings")
    ck("wo-readback: h2d_status is known and read-only",
       "h2d_status" in G.ALL_KNOWN_ATTRS and "h2d_status" in G.RO_ATTRS
       and not G.attr_is_write_only("h2d_status"))

    # ---- schema refusals: a plan may never read a WO trigger ----
    def refuse(label, mutate, code="seq-schema"):
        doc = {"schema": PS.SCHEMA_SEQ, "name": "wo-smoke",
               "steps": [{"name": "x", "kind": "attr_expect",
                          "attr": "h2d_status", "patterns": ["ok=1"]}]}
        mutate(doc)
        expect_guard("wo-readback refusals: %s" % label,
                     lambda: PS.validate(doc, label), code)

    refuse("attr_expect on a write-only trigger",
           lambda d: d["steps"].__setitem__(0, {
               "name": "x", "kind": "attr_expect", "attr": "h2d_ctx_create",
               "patterns": ["ctx_created=1"]}))
    refuse("readback attr is itself write-only",
           lambda d: d["steps"].__setitem__(0, {
               "name": "x", "kind": "attr_write", "attr": "h2d_ctx_create",
               "value": "1", "readback": "h2d_ctx_create"}))
    refuse("readback attr is unknown",
           lambda d: d["steps"].__setitem__(0, {
               "name": "x", "kind": "attr_write", "attr": "h2d_ctx_create",
               "value": "1", "readback": "not_an_attr"}))

    # ---- default readback, generated script, host evaluation ----
    doc = {"schema": PS.SCHEMA_SEQ, "name": "wo-smoke",
           "steps": [{"name": "create", "kind": "attr_write",
                      "attr": "h2d_ctx_create", "value": "1",
                      "patterns": ["ctx_created=1"]}]}
    seq = PS.validate(doc, "wo-smoke")
    ck("wo-readback: default readback is h2d_status",
       seq["steps"][0]["readback"] == "h2d_status", str(seq["steps"][0]))
    pdoc = PS.plan_doc(seq)
    ck("wo-readback: plan doc records the readback attribute",
       pdoc["steps"][0]["readback"] == "h2d_status")
    script = "\n".join(PS.render_script(seq))
    ck("wo-readback: generated capture reads the RO attr, not the trigger",
       "r_attr_as seq-create-h2d_status h2d_status" in script
       and "r_attr_as seq-create-h2d_ctx_create" not in script,
       script[-500:])
    ck("wo-readback: generated gate targets the RO attr",
       "gate_attr 'create.p0' 'h2d_status' 'ctx_created=1'" in script)
    summary = {"result": "PASS", "stop_rule": None, "rd32": {},
               "reads": {"seq-create-h2d_status":
                         "counters: ok=1 ctx_created=1"},
               "gates": [{"name": "create.write", "ok": True},
                         {"name": "create.p0", "ok": True}]}
    ev = PS.evaluate(seq, summary)
    ck("wo-readback: evaluate passes from the RO status capture",
       ev["verdict"] == "PASS", json.dumps(ev)[:300])
    bad = {"result": "PASS", "stop_rule": None, "rd32": {},
           "reads": {"seq-create-h2d_ctx_create": "ctx_created=1"}, "gates": []}
    ev2 = PS.evaluate(seq, bad)
    ck("wo-readback: a WO-attr capture cannot satisfy the post-write gate",
       ev2["verdict"] == "FAIL"
       and any(c.get("code") == "attr-capture-missing"
               for s in ev2["steps"] for c in s["checks"]), ev2["verdict"])

    # ---- every bundled sequence: no post-write capture may read a WO attr ----
    seqdir = os.path.join(TOOLDIR, "sequences")
    bad_hits = []
    for fn in sorted(os.listdir(seqdir)):
        if not fn.endswith(".json"):
            continue
        try:
            s = PS.load(os.path.join(seqdir, fn))
        except G.GuardError as e:
            bad_hits.append((fn, "REFUSED: %s" % e.code))
            continue
        txt = "\n".join(PS.render_script(s))
        for ln in txt.splitlines():
            m = re.match(r"\s*r_attr_as (seq-\S+) (\w+)$", ln)
            if m and m.group(2) in G.WO_ATTRS:
                bad_hits.append((fn, ln.strip()))
    ck("wo-readback: no bundled sequence captures a write-only attr on read",
       not bad_hits, str(bad_hits[:3]))
    c2 = PS.load(os.path.join(seqdir, "bringup-c2-calibration.json"))
    wsteps = [st for st in c2["steps"] if st["kind"] == "attr_write"]
    ck("wo-readback: C2's 8 triggers all read back via h2d_status",
       len(wsteps) == 8
       and all(st["readback"] == "h2d_status" for st in wsteps),
       str([(st["name"], st["readback"]) for st in wsteps]))
    c2txt = "\n".join(PS.render_script(c2))
    ck("wo-readback: C2 script has no read of a WO trigger attr",
       not re.search(r"r_attr_as \S+ (?:h2d_ctx_create|h2d_txq_sync72|"
                     r"h2d_cfg_cal|h2d_txq_addr_sync)\b", c2txt))
    e1 = PS.load(os.path.join(seqdir, "bringup-e1-dataplane.json"))
    e1w = {st["name"]: st["readback"] for st in e1["steps"]
           if st["kind"] == "attr_write"}
    ck("wo-readback: E1's 3 triggers read back via their RO attrs",
       e1w == {"ctx-create-t1": "h2d_status", "dp-setup": "dp_status",
               "dp-txq-sync": "h2d_status"}, str(e1w))
    ck("wo-readback: dp_setup/dp_txq_sync carry the DP policy",
       G.attr_readback("dp_setup") == "dp_status"
       and G.attr_readback("dp_txq_sync") == "h2d_status"
       and G.attr_is_write_only("dp_status") is False
       and G.attr_is_write_only("dp_probe") is False)
    # the flag is surfaced in the plan for the operator
    ck("wo-readback: plan text names the RO readback for the C2 plan",
       "h2d_status" in PS.plan_doc(c2)["steps"][3].get("readback", ""))


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main() -> int:
    tmp = tempfile.mkdtemp(prefix="wifi-bench-selftest-")
    try:
        env = synth_env(tmp)
        test_guards(tmp)
        test_fw_builder(tmp)
        test_session_plans(tmp, env)
        test_session_parser()
        test_session_execute(tmp, env)
        test_session_cli(tmp, env)
        test_transport(tmp)
        test_reboot_wait(tmp)
        proto = proto_env(tmp)
        test_protocol_engine(tmp)
        test_protocol_plan(tmp, proto)
        test_protocol_execute(tmp, proto)
        test_protocol_cli(tmp, proto)
        test_shell_engine(tmp)
        test_pattern_whitespace(tmp, proto)
        test_shell_output_matching(tmp, proto)
        test_blocked_outcome(tmp, proto)
        test_matrix_orchestration(tmp)
        test_wo_readback(tmp, proto)
        test_bringup_sequences(tmp)
        test_cfg80211_mode(tmp, proto)
        test_phase_matrix(tmp, proto)
        test_bringup_report_ready(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    passed = sum(1 for _, ok, _ in CHECKS if ok)
    for name, ok, detail in CHECKS:
        if not ok:
            print("FAIL: %s %s" % (name, ("- " + detail) if detail else ""))
    print("wifi-bench-tooling selftest: %d/%d checks PASS" % (passed, len(CHECKS)))
    return 0 if passed == len(CHECKS) else 1


if __name__ == "__main__":
    raise SystemExit(main())
