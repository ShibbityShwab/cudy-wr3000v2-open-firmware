# The chip layer's message tables, resolved by name (phase 25n, 2026-10-03)

G002's first pass: `hdpp_main_init` registers two tables into `plat.ko`
(`hcc_msg_register_tab_chip(3, tab, 5)` and `hcc_msg_register_tab_core(0, tab, 7)`). Reading `.data`
from index 27 and resolving every entry's relocation gives the handlers **by name**:

| id | handler (chip / core side) |
| --- | --- |
| 1 | `hmac_tx_complete_event_handle` |
| 2 | `hmac_tx_event_process` |
| 3 | `hmac_tx_complete_notify_other_core_event_handle` |
| 4 | `hmac_rx_process_data_event`, `hmac_voice_aggr_event` |
| 5 | `hmac_device_wow_data_report` |
| 7 | `hmac_rx_schedule_req`, `hmac_rx_process_data_event` |
| 8 | `hdpp_stat_save_tx_ppdu_record_process`, `hdpp_stat_save_rx_ppdu_record_process` |

## Why this matters: the message ids now have meanings

This is the semantic layer the port has been missing. The HCC message ids are not opaque tokens - they
are **TX/RX event notifications**:

- **id 1 / 2 / 3** are the **transmit** side - a completion, a tx event, and a notify-the-other-core
  completion;
- **id 4** is `hmac_rx_process_data_event` - **receive data**, alongside a voice-aggregation event;
- **id 5** is a **wow / wake-on-wireless data report**;
- **id 7** is `hmac_rx_schedule_req` - the host **requesting rx scheduling**, which is the natural
  counterpart of the firmware's id 6 ("wake the host's HCC receive thread");
- **id 8** is a **statistics record** save.

The firmware's own two words fit this vocabulary exactly: it emits **id 6** (wake the receive thread -
"go look at your queues") and **id 2** (`hmac_tx_event_process` on the host side). And the port's frame
carries proto `0x0100` with a field `+0x02 = 0x0004` - which, in this vocabulary, is the **rx-data /
voice-aggregation slot**.

## What this does and does not settle

**Does:** it names the message semantics, which is what the payload question needed in order to stop
being a guess. It also independently corroborates the already-recorded handler table (id 2 =
`device_plat_ready_msg_process` on the host side is the *host's* naming for the same slot the core table
calls `hmac_tx_event_process`), so the two records agree rather than conflict.

**Does not yet:** say which message the firmware wants *at the point the port is at*. That now reads as a
**sequencing** question rather than a payload-syntax one: the firmware has said "wake your receive thread"
(id 6), and the host's reply in the vendor's own vocabulary would be an `hmac_rx_schedule_req`-shaped
message (id 7) - not the rx-data/voice frame the port currently posts.

That is a testable statement, and it is the first one in this investigation that is grounded in **named
handlers** rather than inferred from a churning ring.
