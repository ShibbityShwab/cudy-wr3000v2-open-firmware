# The core userland runs on the vendor kernel: a 24.10 IPC round trip (phase 26b, 2026-10-03)

Phase 26a measured that a **24.10.5 binary runs** on the vendor's 5.10.201 kernel (an arithmetic tool -
correct output, md5-verified transfer). That answers the ABI question but not the *planning* question: an
arithmetic tool proves the ELF loads; it does not prove that the parts of OpenWrt a **rootfs replacement**
depends on - the IPC daemon, its socket layer, the ubus/ubox stack - work on an older kernel.

This closes that gap, and the answer is **yes**.

## The stack, and how it was verified

Six packages resolved from the 24.10.5 `base` feed by walking the dependency chain of `ubus`:

| package | version | size |
| --- | --- | --- |
| `ubus` | 2025.10.17~60e04048-r1 | 6,437 B |
| `ubusd` | 2025.10.17~60e04048-r1 | 12,210 B |
| `libubus20250102` | 2025.10.17~60e04048-r1 | 10,529 B |
| `libblobmsg-json20240329` | 2025.07.23~49056d17-r1 | 4,226 B |
| `libubox20240329` | 2025.07.23~49056d17-r1 | 24,878 B |
| `libjson-c5` | 0.18-r1 | 25,791 B |

Pushed to the device and **verified per file, all six md5 identical**.

## The result

```
$ LD_LIBRARY_PATH=/tmp/u2410/root/lib ./root/sbin/ubusd --help
Usage: ./root/sbin/ubusd [<options>]              <- runs
$ LD_LIBRARY_PATH=/tmp/u2410/root/lib ./root/sbin/ubusd -s /tmp/u2410/priv.sock &
srw-rw-rw- 1 root root 0 /tmp/u2410/priv.sock     <- DAEMON STARTED, SOCKET CREATED
$ LD_LIBRARY_PATH=/tmp/u2410/root/lib ./root/bin/ubus -s /tmp/u2410/priv.sock list
list exit=0                                        <- CLIENT ROUND TRIP SUCCEEDED
```

**A 24.10 daemon and a 24.10 client completed an IPC round trip over a unix socket on the vendor's
5.10.201 kernel.** (`ubus call system board` fails with "Not found" - correctly, because `system` is an
rpcd object and no rpcd was registered in the private daemon; the transport, not the object, is what was
under test.)

## Why this is the decisive test rather than another sample

The userland-replacement plan (upgrade the OpenWrt userland on the vendor kernel) fails in one place if it
fails at all: **the IPC/daemon layer**. A statically-simple tool can run while `ubusd` - which uses
netlink, epoll, unix sockets, and the ubox event loop - does not. That layer is now demonstrated working,
**with the vendor's own `ubusd` left untouched throughout** (the test used a private socket path, and at
the end the cleanup was verified: test process gone, vendor `ubus call system board` still answering).

## What this adds to the deliverable's table

| question | answer |
| --- | --- |
| how current can the **userland** go? | 24.10.5 **binary** runs (26a) **and** its **IPC stack runs** (26b) on the vendor kernel |
| is the userland replacement plan credible? | **yes for the userland half** - the part that would break first is demonstrated working |
| kernel half | still blocked on the vendor's target patches |

## Method notes, both learned the hard way today

1. **`/usr/bin/scp` on this box is a symlink to dropbear, and it ACKNOWLEDGES a file and exits 0 without
   writing it.** Three "successful" transfers left nothing. The working path is
   `cat local | ssh 'cat > remote'` over the ssh command lane - note that `.sshwrap/rsh.sh` nulls stdin
   (`< /dev/null`), so the ssh invocation must be made directly - with a **per-file md5 comparison**.
2. **The device has no WAN route and this host is firewalled for inbound**, so hosting packages here and
   fetching from the device fails. Host→device push is the only direction that works.
3. **Suppressing stderr hides real failures.** Two operations in this session "succeeded" while doing
   nothing (`cat > file` into a directory that did not exist yet; `git commit -q` with nothing staged)
   because their output was discarded. Do not silence output on an operation whose success is being
   assumed; verify the artifact instead.

