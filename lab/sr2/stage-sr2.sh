set -x
cp /tmp/sr2.ko /lib/modules/5.10.201/sr2.ko
cp /tmp/omo-sr2 /etc/init.d/omo-sr2
cp /tmp/recover-sr2.sh /root/recover-sr2.sh
chmod 755 /etc/init.d/omo-sr2 /root/recover-sr2.sh
ln -sf ../init.d/omo-sr2 /etc/rc.d/S99omo-sr2
cd /lib/modules/5.10.201
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
sync
set +x
echo "== staged =="
md5sum /lib/modules/5.10.201/sr2.ko
ls -l /etc/init.d/omo-sr2 /etc/rc.d/S99omo-sr2 /root/recover-sr2.sh
ls -l /lib/modules/5.10.201/*.omo-off
sh -n /etc/init.d/omo-sr2 && echo "loader syntax: OK"
sh -n /root/recover-sr2.sh && echo "recovery syntax: OK"
