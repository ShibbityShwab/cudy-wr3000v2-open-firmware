# alg_cfg codec - host-side 17-twin oracle harness
# oracle_test: links the LOCAL hi1105 reference (oracle/ - not committed)
# port_test:   links the committed port (../codec/)
CC ?= gcc
CFLAGS = -Wall -Wno-unused-function -Wno-unused-parameter -Wno-pointer-sign \
         -Wno-sign-compare -Wno-format -fno-strict-aliasing -O1 -g

all: oracle_test port_test

oracle_test: test_alg_codec.c shims/shim_util.c oracle/wal_config_alg_cfg_ref.c
	$(CC) $(CFLAGS) -Ioracle/ref-shims test_alg_codec.c shims/shim_util.c \
	    oracle/wal_config_alg_cfg_ref.c -o oracle_test

port_test: test_alg_codec.c shims/shim_util.c ../codec/wal_config_alg_cfg.c
	$(CC) $(CFLAGS) -I../codec test_alg_codec.c shims/shim_util.c \
	    ../codec/wal_config_alg_cfg.c -o port_test

check: oracle_test port_test
	./oracle_test > /tmp/oracle.out 2>&1; ./port_test > /tmp/port.out 2>&1; \
	diff /tmp/oracle.out /tmp/port.out && echo "ORACLE DIFF: IDENTICAL (17/17)" || echo "ORACLE DIFF: DIVERGED"

clean:
	rm -f oracle_test port_test /tmp/oracle.out /tmp/port.out

.PHONY: all check clean
