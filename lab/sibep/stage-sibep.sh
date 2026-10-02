set -x
cp /tmp/sibep.ko /lib/modules/5.10.201/sibep.ko
cp /tmp/omo-sibep /etc/init.d/omo-sibep
cp /tmp/recover-sibep.sh /root/recover-sibep.sh
chmod 755 /etc/init.d/omo-sibep /root/recover-sibep.sh
ln -sf ../init.d/omo-sibep /etc/rc.d/S99omo-sibep
cd /lib/modules/5.10.201
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
sync
set +x
echo "== staged =="
md5sum /lib/modules/5.10.201/sibep.ko
ls -l /etc/init.d/omo-sibep /etc/rc.d/S99omo-sibep /root/recover-sibep.sh
ls -l /lib/modules/5.10.201/*.omo-off
sh -n /etc/init.d/omo-sibep && echo "loader syntax: OK"
sh -n /root/recover-sibep.sh && echo "recovery syntax: OK"
