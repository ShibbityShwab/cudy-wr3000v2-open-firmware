# SPDX-License-Identifier: GPL-2.0
# Convenience wrapper for the scan D2H decoder lane (host-only checks).
#
#   make test        host unit tests (C99, -Wall -Wextra -Werror -pedantic)
#   make vectors     regenerate + cross-check testvectors/scan-d2h-vectors.json
#   make re          static evidence self-check (pinned local vendor artifacts)
#   make kernel      optional 6.18.60 module-context compile check
#   make all         test + vectors + re
#   make clean
#
# No target touches the device or the network.

CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic
PYTHON ?= /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
BIN ?= /tmp/scan-d2h

all: test vectors re

test: $(BIN)/scan_d2h_selftest
	$(BIN)/scan_d2h_selftest

$(BIN)/scan_d2h_selftest: src/hi5622_scan_d2h.c src/hi5622_scan_d2h.h tools/scan_d2h_selftest.c
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) -Isrc tools/scan_d2h_selftest.c src/hi5622_scan_d2h.c -o $@

vectors:
	$(PYTHON) tools/gen_vectors.py --write
	$(PYTHON) tools/gen_vectors.py --check

re:
	$(PYTHON) tools/fw_scan_d2h_re.py --selftest

kernel:
	./tools/kernel-check.sh

clean:
	rm -rf $(BIN)

.PHONY: all test vectors re kernel clean
