set -x
cp /tmp/svc.ko /lib/modules/5.10.201/svc.ko
cp /tmp/omo-svc /etc/init.d/omo-svc
cp /tmp/recover-svc.sh /root/recover-svc.sh
chmod 755 /etc/init.d/omo-svc /root/recover-svc.sh
ln -sf ../init.d/omo-svc /etc/rc.d/S99omo-svc
cd /lib/modules/5.10.201
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
sync
set +x
echo "== staged =="
md5sum /lib/modules/5.10.201/svc.ko
ls -l /etc/init.d/omo-svc /etc/rc.d/S99omo-svc /root/recover-svc.sh
ls -l /lib/modules/5.10.201/*.omo-off
sh -n /etc/init.d/omo-svc && echo "loader syntax: OK"
sh -n /root/recover-svc.sh && echo "recovery syntax: OK"
