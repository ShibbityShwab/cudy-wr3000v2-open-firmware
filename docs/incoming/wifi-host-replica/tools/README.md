# tools - host-only regeneration helpers

No vendor binaries, bytes or source text are needed by (or emitted from) these tools;
they consume a local vendor .ko plus its objdump listing (both local evidence, never
committed) and print derived addresses/strings/tables.

| tool | purpose |
| --- | --- |
| annotate.py | ELF reloc resolver + objdump annotator: appends, to each relocation line, the resolved ARM MOVW/MOVT/ABS32/CALL target (string or section:offset) |
| census.py | prints every call site (function + 10 preceding instructions) of a set of named functions in an annotated listing |
| tables.py | dumps a .data/.rodata table with function/string pointers resolved and raw words shown |
| regen.sh | end-to-end regeneration: disassemble + annotate each given .ko, then dump the report's key tables for the 2.5.24 plat module |

Requires: host python3 (stdlib only) and the ARM cross objdump/nm used by the project
(the TC variable in regen.sh points at the OpenWrt staging toolchain).

    ./regen.sh /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko \
                /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko

Output goes to /tmp/hr-regen (override with OUT=...).  See REPORT.md section 12.
