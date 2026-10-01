set -x
cp /tmp/srpump.ko /lib/modules/5.10.201/srpump.ko
cp /tmp/omo-srpump /etc/init.d/omo-srpump
cp /tmp/recover-srpump.sh /root/recover-srpump.sh
chmod 755 /etc/init.d/omo-srpump /root/recover-srpump.sh
ln -sf ../init.d/omo-srpump /etc/rc.d/S99omo-srpump
cd /lib/modules/5.10.201
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
sync
set +x
echo "== staged =="
md5sum /lib/modules/5.10.201/srpump.ko
ls -l /etc/init.d/omo-srpump /etc/rc.d/S99omo-srpump /root/recover-srpump.sh
ls -l /lib/modules/5.10.201/*.omo-off
sh -n /etc/init.d/omo-srpump && echo "loader syntax: OK"
sh -n /root/recover-srpump.sh && echo "recovery syntax: OK"
