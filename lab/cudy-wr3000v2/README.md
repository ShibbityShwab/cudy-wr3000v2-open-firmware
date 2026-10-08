# cudy-wr3000v2 - TR6560 BSP board material + local build

Board material for porting the Cudy WR3000 v2.0 onto the Triductor TR6560 BSP
(the Banana Pi BPI-Wifi6 fork). The port plan is `CUDY.md`; the CI workflow is
`.github/workflows/bsp-tr6560-build.yml` and it is **workflow_dispatch only** (it
never fires on push or pull request).

The commands below are the exact local reproduction of that workflow, run from the
`opensource/` submodule root (where `lab/` lives), on an Ubuntu 22.04 host.

```bash
# 1. Build dependencies (SDK manual). The i386 arch must be added before the
#    lib32 multilib pair can install.
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y \
  build-essential libncurses-dev unzip bzip2 gawk file python3 rsync \
  subversion wget gettext git automake libc6-dev-i386 lib32stdc++6

# 2. Clone the BSP (Triductor TR6560 / OpenWrt 22.03).
git clone --depth 1 https://github.com/BPI-SINOVOIP/THG6500-TAX2-OPENWRT-BSP.git bsp

# 3. Overlay board material (device tree etc. under lab/cudy-wr3000v2/). The two
#    docs are documentation, not board material, and are excluded so they never
#    clobber the BSP's own README.md.
rsync -a --exclude=README.md --exclude=CUDY.md lab/cudy-wr3000v2/ bsp/

# 4. Feeds.
cd bsp
./scripts/feeds update -a
./scripts/feeds install -a

# 5. Build with the committed .config at the BSP root (selects
#    tr6560/generic/DEVICE_THG6500-TAX2, arm_cortex-a9 + musl).
make -j"$(nproc)"
```

Built images land in `bsp/bin/targets/tr6560/generic/` (`sysupgrade.bin`,
`fullimage.bin`). In CI the workflow uploads `bsp/bin/targets` as
`bsp-bin-targets` and the image directory as `bsp-tr6560-images`.
