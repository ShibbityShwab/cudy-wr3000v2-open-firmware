set -x
cp /tmp/fwaccept.ko /lib/modules/5.10.201/fwaccept.ko
cp /tmp/omo-fwaccept /etc/init.d/omo-fwaccept
cp /tmp/recover-fwaccept.sh /root/recover-fwaccept.sh
chmod 755 /etc/init.d/omo-fwaccept /root/recover-fwaccept.sh
ln -sf ../init.d/omo-fwaccept /etc/rc.d/S99omo-fwaccept
cd /lib/modules/5.10.201
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
sync
set +x
echo "== staged =="
md5sum /lib/modules/5.10.201/fwaccept.ko
ls -l /etc/init.d/omo-fwaccept /etc/rc.d/S99omo-fwaccept /root/recover-fwaccept.sh
ls -l /lib/modules/5.10.201/*.omo-off
sh -n /etc/init.d/omo-fwaccept && echo "loader syntax: OK"
sh -n /root/recover-fwaccept.sh && echo "recovery syntax: OK"
