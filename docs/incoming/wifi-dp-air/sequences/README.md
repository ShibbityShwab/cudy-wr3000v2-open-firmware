# sequences/

The canonical F-AIR ladder is bundled with the bench tooling so the
one-command runner and the phase-matrix/decision-table tooling see it without a
special path:

    ../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json   (30 steps / 83 checks / 7 device writes / 4 shell actions)

It is validated by ../wifi-bench-tooling/tools/selftest.py and by this lane's
tools/build.sh. Earlier data-plane sequences remain where their lanes put them
(../wifi-dp-txrx/sequences/ladder-f-txrx.json = E1-post-only ladder F;
../wifi-bench-tooling/tools/sequences/bringup-e1-dataplane.json = E1 phase).
