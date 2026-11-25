#!/usr/bin/env python3
"""verify_decode.py - re-derive the fw-h2d-stall decode from the reader's own local artifacts.

Host-only, read-only. This tool never writes to a device and never copies vendor bytes into the
repository: it only prints PASS/FAIL lines with offsets, mnemonics and symbol addresses.

Usage:
  python3 tools/verify_decode.py \
      --firmware  /path/to/FIRMWARE.bin \
      --pd4       /path/to/pd4-rebuilt.bin \
      --plat-ko   /path/to/hi5622v100_plat.ko \
      --wifi-ko   /path/to/hi5622v100_wifi.ko \
      --objdump   /path/to/arm-openwrt-linux-objdump \
      --nm        /path/to/arm-openwrt-linux-nm

All paths have defaults tuned to the Lead's workstation; override as needed.
Exit code 0 = every check passed; 1 = at least one failed check (details printed).
"""
import argparse
import hashlib
import struct
import subprocess
import sys

DEFAULTS = {
    "firmware": "/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin",
    "pd4": "/home/shibbityshwab/pd4-rebuilt.bin",
    "plat_ko": "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko",
    "wifi_ko": "/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko",
    "objdump": "/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-"
               "arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump",
    "nm": "/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-"
          "arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-nm",
}

SHA_STOCK = "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b"
SHA_PD4 = "cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1"

RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, ok, detail))
    print(("PASS  " if ok else "FAIL  ") + name + ((" : " + detail) if detail else ""))
    return ok


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_word(data, value):
    needle = struct.pack("<I", value)
    out = []
    i = data.find(needle)
    while i != -1:
        out.append(i)
        i = data.find(needle, i + 1)
    return out


def disasm(objdump, mode, path, start=None, stop=None):
    cmd = [objdump, "-d"]
    if mode == "raw-thumb":
        cmd += ["-D", "-b", "binary", "-m", "arm", "-M", "force-thumb"]
    if start is not None:
        cmd += ["--start-address=%#x" % start]
    if stop is not None:
        cmd += ["--stop-address=%#x" % stop]
    cmd.append(path)
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    return out


def has_ops(text, ops):
    missing = [o for o in ops if o not in text]
    return (not missing), missing


def main():
    ap = argparse.ArgumentParser()
    for k, v in DEFAULTS.items():
        ap.add_argument("--" + k.replace("_", "-"), default=v)
    a = ap.parse_args()

    # ---- 1. artifact identity ------------------------------------------------
    try:
        fw = open(a.firmware, "rb").read()
        check("stock firmware sha256", sha256(a.firmware) == SHA_STOCK,
              "expected 7fc87e20...311b")
    except OSError as e:
        check("stock firmware readable", False, str(e))
        return 1
    try:
        pd4 = open(a.pd4, "rb").read()
        check("PD4 image sha256", sha256(a.pd4) == SHA_PD4, "expected cfe24bfb...037b1")
        check("PD4 same length as stock", len(pd4) == len(fw), str(len(fw)))
    except OSError as e:
        check("PD4 image readable", False, str(e))
        pd4 = None

    # ---- 2. the out[0]/out[1] CA literals in the firmware -------------------
    out0 = find_word(fw, 0x40039010)
    check("CA 0x40039010 (out[0]) appears exactly once", len(out0) == 1,
          "at file 0x%x" % out0[0] if out0 else "not found")
    check("out[0] literal is at file 0x97e0 (pcie_msg_init ctx store)", out0 == [0x97E0],
          " ".join(hex(x) for x in out0))
    out1 = find_word(fw, 0x40039014)
    check("CA 0x40039014 (out[1]) at file 0x97dc + 0x86fbc", out1 == [0x97DC, 0x86FBC],
          " ".join(hex(x) for x in out1))
    ctx_static = find_word(fw, 0x0010C190)
    check("ctx CA 0x0010C190 static literal at file 0x29c", ctx_static == [0x29C],
          " ".join(hex(x) for x in ctx_static))
    table_sentinel = find_word(fw, 0x00118D68)
    check("handler-table sentinel 0x00118D68 static at file 0xcc1b0",
          table_sentinel == [0xCC1B0], " ".join(hex(x) for x in table_sentinel))

    # ---- 3. the dispatcher: clear out[0] BEFORE dispatch --------------------
    try:
        d = disasm(a.objdump, "raw-thumb", a.firmware, 0x818AC, 0x818F4)
        ok, missing = has_ops(d, [
            "push", "mov\tr6, r0", "cbz\tr0", "ldr\tr2, [r0, #12]", "str\tr7, [r2, #0]",
            "ldr\tr2, [r0, #4]", "ldr\tr5, [r2, #0]", "str\tr1, [r2, #0]", "movs\tr1, #8",
            "ldr\tr2, [r0, #16]", "str\tr1, [r2, #0]",
        ])
        check("dispatcher file 0x818ac: ack, read out[0], clear out[0], doorbell=8, then dispatch",
              ok, "missing: " + "; ".join(missing))
        clear = d.find("str\tr1, [r2, #0]")
        call = d.find("blx\tr3")
        check("the clear precedes the handler call (clear-before-dispatch rule)",
              clear != -1 and call != -1 and clear < call,
              "clear_at=%d call_at=%d" % (clear, call))
    except (OSError, subprocess.CalledProcessError) as e:
        check("objdump available for firmware", False, str(e))

    # ---- 4. the only entry: thunk file 0x294 -> 0xC18AC ---------------------
    try:
        t = disasm(a.objdump, "raw-thumb", a.firmware, 0x294, 0x29C)
        ok, missing = has_ops(t, ["ldr\tr0, [pc, #4]", "b.w\t0x818ac"])
        check("thunk file 0x294 loads ctx CA 0x10C190 and branches to the dispatcher", ok,
              "missing: " + "; ".join(missing))
        whole = disasm(a.objdump, "raw-thumb", a.firmware)
        n = sum(1 for ln in whole.splitlines() if "b.w\t0x818ac" in ln)
        check("dispatcher has exactly one caller in the image", n == 1, "callers=%d" % n)
    except (OSError, subprocess.CalledProcessError) as e:
        check("objdump available for firmware (whole image)", False, str(e))

    # ---- 5. PD4 poller: entry patch + stub + literal pool -------------------
    if pd4 is not None:
        try:
            e = disasm(a.objdump, "raw-thumb", a.pd4, 0x916A, 0x916E)
            check("PD4 entry patch at file 0x916a branches to the stub (CPU 0x108240)", "b.w\t0xc8240" in e,
                  e.strip().splitlines()[-1] if e.strip() else "")
            s = disasm(a.objdump, "raw-thumb", a.pd4, 0xC8240, 0xC8270)
            ok, missing = has_ops(s, [
                "ldr\tr7", "ldr\tr3, [r7, #0]", "cmp\tr3, #0", "ldr\tr2",
                "ldr\tr3, [r2, #0]", "ldr\tr3, [r7, #20]", "cmp\tr3, r4",
                "bl\t0x294", "str\tr4, [r7, #20]", "str\tr3, [r7, #20]",
            ])
            check("PD4 poll stub shape (instance wait, out[0] poll, table guard, call thunk)",
                  ok, "missing: " + "; ".join(missing))
            lits = struct.unpack_from("<4I", pd4, 0xC8270)
            check("PD4 stub literal pool {0x10C19C, 0x40039010, 0x118D68, 0x2000}",
                  lits == (0x0010C19C, 0x40039010, 0x00118D68, 0x00002000),
                  ", ".join(hex(x) for x in lits))
        except (OSError, subprocess.CalledProcessError) as e:
            check("objdump available for PD4 image", False, str(e))

    # ---- 6. vendor module symbols ------------------------------------------
    def nm(path):
        try:
            out = subprocess.run([a.nm, path], capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError) as e:
            check("nm available for " + path, False, str(e))
            return {}
        syms = {}
        for ln in out.splitlines():
            parts = ln.split()
            if len(parts) >= 3 and parts[1] in ("t", "T"):
                syms.setdefault(parts[2], parts[0])
        return syms

    plat = nm(a.plat_ko)
    for name, addr in {
        "pcie_msg_send": "000160f4", "pcie_msg_register": "00015fbc",
        "pcie_msg_init": "0000b6e4", "pcie_msg_wait_for_clr": "000173dc",
        "pcie_msg_send_irq": "000174a8", "shuangta_ete_sr_dscr_fill": "00017858",
    }.items():
        check("plat.ko symbol %s @ %s" % (name, addr), plat.get(name) == addr,
              "got " + str(plat.get(name)))
    wifi = nm(a.wifi_ko)
    for name, addr in {
        "wal_check_and_release_msg_resp": "00109370",
        "wal_send_cfg_event": "00126c98",
        "wal_alloc_cfg_event": "00126ab0",
    }.items():
        check("wifi.ko symbol %s @ %s" % (name, addr), wifi.get(name) == addr,
              "got " + str(wifi.get(name)))

    # ---- 7. the vendor's only wait-for-clear: one call, inside send_irq ----
    if plat:
        try:
            pd = disasm(a.objdump, "elf", a.plat_ko)
            # call sites end with ">", the function definition line ends with ":"
            callers = [ln for ln in pd.splitlines()
                       if "bl\t173dc" in ln and not ln.rstrip().endswith(":")]
            check("pcie_msg_wait_for_clr has exactly one call site", len(callers) == 1,
                  "callers=%d" % len(callers))
            check("...and it is inside pcie_msg_send_irq (call at 0x17508)",
                  len(callers) == 1 and "17508" in callers[0], callers[0].strip() if callers else "")
            body = []
            try:
                lines = pd.splitlines()
                i = next(k for k, ln in enumerate(lines) if "<pcie_msg_send>:" in ln)
                for ln in lines[i + 1:]:
                    if ln.strip() == "":
                        break
                    body.append(ln)
            except StopIteration:
                pass
            check("pcie_msg_send refuses id > 9 (cmp r4,#9 in its body)",
                  any("cmp\tr4, #9" in ln for ln in body), "body_lines=%d" % len(body))
        except (OSError, subprocess.CalledProcessError) as e:
            check("objdump available for plat.ko", False, str(e))

    # ---- 8. summary ---------------------------------------------------------
    bad = [r for r in RESULTS if not r[1]]
    print("\n%d/%d checks passed" % (len(RESULTS) - len(bad), len(RESULTS)))
    if bad:
        print("FAILED:")
        for name, _, detail in bad:
            print("  - " + name + ((" : " + detail) if detail else ""))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
