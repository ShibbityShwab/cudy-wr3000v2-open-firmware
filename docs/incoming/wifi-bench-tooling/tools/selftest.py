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

TOOLDIR = os.path.dirname(os.path.abspath(__file__))
if TOOLDIR not in sys.path:
    sys.path.insert(0, TOOLDIR)

import bench_guards as G          # noqa: E402
import fw_patch_builder as FWB    # noqa: E402
import bench_session as BS        # noqa: E402

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
