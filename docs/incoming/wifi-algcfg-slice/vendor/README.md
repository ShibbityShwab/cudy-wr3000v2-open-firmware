# vendored GPL-2.0 hi3881 wal config-command layer

Origin: OpenHarmony `device_soc_hisilicon`, commit 7c1e2d5,
path `common/platform/wifi/hi3881v100/driver/wal/`, cloned with
`git clone --depth 1 https://github.com/openharmony/device_soc_hisilicon`
(the gitee mirror `gitee.com/openharmony/device_soc_hisilicon` carries the
same content). The driver directory is GPL-2.0-or-later (see the header of
every file; the repo LICENSE grants the same for this subtree).

Files: wal_hipriv.{c,h} (the iwpriv command entry - where the hi1105
wal_config.c plugs in), wal_ioctl.{c,h} (ioctl helpers), wal_customize.{c,h}
(cfg-id customization handlers), wal_maintenance.{c,h} (maintenance commands).

These are the hi3881 "wal config-command codec" - the layer the alg_cfg
codec (codec/) attaches to on the device. They are vendored as TEXT for the
wal slice; they only compile inside the full oal/hmac environment (the
6.18 cfg80211/netlink adaptation is that slice's job, see REPORT section 9).
No hongmengkernel/hi1105 text is vendored anywhere in this slice.
