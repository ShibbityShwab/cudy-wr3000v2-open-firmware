set -x
cp /tmp/srt.ko /lib/modules/5.10.201/srt.ko
cp /tmp/omo-srt /etc/init.d/omo-srt
cp /tmp/recover-srt.sh /root/recover-srt.sh
chmod 755 /etc/init.d/omo-srt /root/recover-srt.sh
ln -sf ../init.d/omo-srt /etc/rc.d/S99omo-srt
cd /lib/modules/5.10.201
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
sync
set +x
echo "== staged =="
md5sum /lib/modules/5.10.201/srt.ko
ls -l /etc/init.d/omo-srt /etc/rc.d/S99omo-srt /root/recover-srt.sh
ls -l /lib/modules/5.10.201/*.omo-off
sh -n /etc/init.d/omo-srt && echo "loader syntax: OK"
sh -n /root/recover-srt.sh && echo "recovery syntax: OK"
