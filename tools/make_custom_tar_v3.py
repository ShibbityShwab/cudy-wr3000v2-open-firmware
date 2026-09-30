import io, os, tarfile

BASE = r'C:/Users/ShibbityShwab/router-openwrt'
MT = 1761796156
KEY_FILES = ['id_ed25519.pub', 'id_rsa.pub']
SRC = os.path.join(BASE, 'build/custom/rootfs-2.5.24-base.tar')
OUT = os.path.join(BASE, 'build/custom/rootfs-0.3.tar')
REPLACE = {
    'etc/shadow',
    'usr/lib/lua/luci/model/cbi/system/systime.lua',
}


def read(path):
    with open(os.path.join(BASE, path), 'rb') as f:
        return f.read()


def ti(name, mode, size=0, linkname=''):
    t = tarfile.TarInfo(name)
    t.mode = mode
    t.uid = 0
    t.gid = 0
    t.uname = 'root'
    t.gname = 'root'
    t.size = size
    t.mtime = MT
    if linkname:
        t.linkname = linkname
        t.type = tarfile.SYMTYPE
    return t


omosshd = (
    '#!/bin/sh /etc/rc.common\nSTART=95\nUSE_PROCD=1\nstart_service() {\n'
    '\tmkdir -p /etc/dropbear\n\tprocd_open_instance\n'
    '\tprocd_set_param command /usr/sbin/dropbear -p 22 -R\n'
    '\tprocd_set_param respawn\n\tprocd_close_instance\n}\n'
)
shadow = read('build/ref/shadow')
keys = b'\n'.join(read(p).strip() for p in KEY_FILES) + b'\n'
wrapper = read('build/custom/systime.lua')
stock_systime = read('rootfs-2.5.24/system/systime.lua')
opkg_bin = read('build/opkg/opkg')
opkg_key = read('build/opkg/opkg-key')
opkg_conf = read('build/custom/opkg.conf')
distfeeds = read('build/custom/distfeeds.conf')
customfeeds = read('build/custom/customfeeds.conf')
readme = read('build/custom/README-CUSTOM-03.md')
version = b'omo-minimal-0.3 stock-2.5.24-20260727-122111\n'

src = tarfile.open(SRC, 'r')
out = tarfile.open(OUT, 'w', format=tarfile.GNU_FORMAT)
kept = 0
for m in src:
    n = m.name[2:] if m.name.startswith('./') else m.name
    if n in REPLACE:
        continue
    m.name = n
    out.addfile(m, src.extractfile(m) if m.isfile() else None)
    kept += 1
src.close()
print('kept entries:', kept)

entries = [
    ('etc/shadow', shadow, 0o600),
    ('etc/init.d/omosshd', omosshd.encode(), 0o755),
    ('root/.ssh/authorized_keys', keys, 0o600),
    ('etc/custom-firmware-version', version, 0o644),
    ('root/README-CUSTOM.md', readme, 0o644),
    ('usr/lib/lua/luci/model/cbi/system/systime.lua', wrapper, 0o644),
    ('usr/lib/lua/luci/model/cbi/system/systime-stock.lua', stock_systime, 0o644),
    ('usr/bin/opkg', opkg_bin, 0o755),
    ('usr/sbin/opkg-key', opkg_key, 0o755),
    ('etc/opkg.conf', opkg_conf, 0o644),
    ('etc/opkg/distfeeds.conf', distfeeds, 0o644),
    ('etc/opkg/customfeeds.conf', customfeeds, 0o644),
]
for name, data, mode in entries:
    out.addfile(ti(name, mode, len(data)), io.BytesIO(data))

d = ti('root/.ssh', 0o700)
d.type = tarfile.DIRTYPE
out.addfile(d)
out.addfile(ti('etc/rc.d/S95omosshd', 0o777, linkname='../init.d/omosshd'))
out.close()
print('v3 tar written:', OUT)
