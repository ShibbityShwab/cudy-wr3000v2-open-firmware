Evidence for the monitor2 lane (task-246), 2026-11-26, HOST-ONLY (no bench).
- BASE-TREE.sha256      : sha256 of the 97-file IMAGE #3 merged module source
                          tree (/tmp/wifi-image-3-module) the patch applies to.
- HOST-TESTS.txt        : tools/monitor_selftest.c output (host gcc; 34 checks).
- BUILD-FACTS.txt       : patched build vs unpatched control build facts.
- BUILD-patched.log     : full kbuild log, patched tree (0 warnings/errors).
- BUILD-control.log     : full kbuild log, unpatched tree (0 warnings/errors).
- PATCH-APPLY.txt       : patch(1) -p1 log on a fresh copy of the base tree.
- PATCH-ROUNDTRIP.txt   : post-apply tree is byte-identical to the built tree.
No binaries are committed (.ko stayed in /tmp/monitor2-work/out/artifacts/);
their size/sha256 are recorded in BUILD-FACTS.txt.
