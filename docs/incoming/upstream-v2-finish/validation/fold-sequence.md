# Sequential fold composition probe (host-only)

Starting state: frozen v1 26-patch review form (2cf024204667).
Fold source patches applied in L0 order; nothing forced.

| fold | method | composed | error |
| --- | --- | --- | --- |
| 027 | git apply | yes |  |
| 031 | git apply | yes |  |
| 032 | git apply | yes |  |
| 036 | git apply | yes |  |
| 038 | git apply | yes |  |
| 041 | - | NO | error: patch failed: drivers/net/ethernet/hisilicon/luofu-gmac.c:117 |
| 043 | git apply | yes |  |
| 044 | git apply | yes |  |
| 046 | - | NO | error: patch failed: drivers/net/ethernet/hisilicon/luofu-switch.c:242 |
| 048 | - | NO | error: patch failed: drivers/net/ethernet/hisilicon/luofu-pie.c:417 |
| 051 | git apply -C1 (reduced context) | yes |  |
| 053 | - | NO | error: patch failed: drivers/net/ethernet/hisilicon/luofu-pie.c:519 |
| 059 | - | NO | error: patch failed: drivers/net/ethernet/hisilicon/luofu-switch.c:242 |
| 060 | - | NO | error: patch failed: drivers/net/ethernet/hisilicon/luofu-pie.c:520 |
| 066 | git apply | yes |  |
| 067 | git apply | yes |  |

composed 10/16

Cumulative diffstat v1end -> composed tree:

~~~
.../bindings/thermal/hisilicon,luofu-tvsensor.yaml |   87 +-
 drivers/clk/hisilicon/crg-luofu.c                  |   12 +-
 drivers/net/ethernet/hisilicon/luofu-gmac.c        |  215 ++-
 drivers/net/ethernet/hisilicon/luofu-pie.c         |    9 +
 drivers/net/ethernet/hisilicon/luofu-switch-regs.h |  278 +++-
 drivers/net/ethernet/hisilicon/luofu-switch.c      | 1434 +++++++++++++++++++-
 drivers/thermal/Kconfig                            |    5 +-
 drivers/thermal/luofu_tvsensor.c                   |  459 ++++++-
 8 files changed, 2368 insertions(+), 131 deletions(-)
~~~
