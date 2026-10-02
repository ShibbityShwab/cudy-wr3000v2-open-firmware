set -x
cp /tmp/bothep.ko /lib/modules/5.10.201/bothep.ko
cp /tmp/omo-bothep /etc/init.d/omo-bothep
cp /tmp/recover-bothep.sh /root/recover-bothep.sh
chmod 755 /etc/init.d/omo-bothep /root/recover-bothep.sh
ln -sf ../init.d/omo-bothep /etc/rc.d/S99omo-bothep
cd /lib/modules/5.10.201
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
sync
set +x
echo "== staged =="
md5sum /lib/modules/5.10.201/bothep.ko
ls -l /etc/init.d/omo-bothep /etc/rc.d/S99omo-bothep /root/recover-bothep.sh
ls -l /lib/modules/5.10.201/*.omo-off
sh -n /etc/init.d/omo-bothep && echo "loader syntax: OK"
sh -n /root/recover-bothep.sh && echo "recovery syntax: OK"
