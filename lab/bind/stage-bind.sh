set -x
cp /tmp/bind.ko /lib/modules/5.10.201/bind.ko
cp /tmp/omo-bind /etc/init.d/omo-bind
cp /tmp/recover-bind.sh /root/recover-bind.sh
chmod 755 /etc/init.d/omo-bind /root/recover-bind.sh
ln -sf ../init.d/omo-bind /etc/rc.d/S99omo-bind
cd /lib/modules/5.10.201
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
sync
set +x
echo "== staged =="
md5sum /lib/modules/5.10.201/bind.ko
ls -l /etc/init.d/omo-bind /etc/rc.d/S99omo-bind /root/recover-bind.sh
ls -l /lib/modules/5.10.201/*.omo-off
sh -n /etc/init.d/omo-bind && echo "loader syntax: OK"
sh -n /root/recover-bind.sh && echo "recovery syntax: OK"
