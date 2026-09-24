# ESP-NOW FTSP Test
This app is a a capability test of the Flooding Time Synchronization Protocol over ESP-NOW on a pair of ESP32 boards.

The goal is to achieve time synchronization as closely as possible between 2 ESP32 modules and have them toggle GPIO in sync, with the timing measured on the bench with an oscilloscope

- [ ] Use ESP-NOW with FTSP to synchronize times as closely as possible
- [ ] have the 2 boards agree on a time at which to strobe GPIO25 high for 10ms and then return low, 10 times per second
- [ ] repeat

## Architecture

Single symmetric app: identical firmware flashes to both boards. Unlike
`pitch_catch_mode_hardware_test`'s sender/receiver split, there's no build-time role - FTSP is
inherently symmetric, so role is decided at runtime.

- `espnow_link.c/.h` - thin transport. WiFi STA + ESP-NOW bring-up, broadcast send, and a receive
  callback that timestamps each inbound packet (`esp_timer_get_time()`, captured first thing in
  the callback) before handing it to `ftsp_sync` via a queue. Knows nothing about the FTSP wire
  protocol.
- `ftsp_sync.c/.h` - the protocol. Every node starts `DISCOVERING`: broadcasts HELLO for
  `FTSP_DISCOVERY_WINDOW_MS` while watching for the lowest MAC address claimed by any node
  (including itself); the lowest MAC becomes root, everyone else becomes follower. The root
  broadcasts a `SYNC` beacon every `FTSP_SYNC_INTERVAL_MS` carrying two things: its own current
  timestamp (`root_timestamp_us`, feeds the regression below) and its chosen instant for the
  *next* GPIO strobe (`next_strobe_root_us`) - an explicit rendezvous timestamp, not something
  each board derives independently. Followers feed each SYNC's timestamp into a small ring-buffer
  linear regression of clock offset (root - local) vs. local time, giving a skew-compensated
  mapping between local and root time, and copy `next_strobe_root_us` verbatim as the agreed
  target rather than recomputing their own. Root conflicts self-resolve (a root or follower that
  ever sees a lower MAC than its current root adopts it, resetting the regression); a follower
  that hears nothing from its root for `FTSP_ROOT_TIMEOUT_MS` falls back to discovery.
- `gpio_strobe.c/.h` - consumes the agreed rendezvous timestamp. Uses `esp_timer` exclusively (not
  `vTaskDelay`) to fire GPIO25 high for `GPIO_STROBE_HIGH_US` at the root-announced
  `next_strobe_root_us` (translated to local time via the regression), falling back to deriving
  its own next `GPIO_STROBE_PERIOD_US` boundary only when no still-future announcement is
  available yet (before the first SYNC, or after a dropped packet). Re-derives the deadline at the
  end of every pulse so fresher announcements and skew corrections keep applying instead of a
  fixed schedule drifting.
- `ftsp_main.c` - `app_main()`: wires the three modules together and hands off to
  `gpio_strobe_run()`, which never returns.

### Known limitations / first-pass constants (bench tuning still needed)
- Timestamps (`root_timestamp_us` on send, `recv_local_us` on receive) are software timestamps
  taken at the `esp_now_send()` / receive-callback call sites, not true MAC-layer TX/RX
  timestamps - WiFi driver queuing and CSMA backoff jitter leak directly into the sync error.
- No outlier/residual rejection before adding a regression sample (the original FTSP paper
  discards high-residual points); this is a plain least-squares fit over the last
  `FTSP_TABLE_SIZE` (8) samples.
- `FTSP_TABLE_SIZE` and the HELLO/SYNC intervals (`ftsp_sync.h`) are first-pass values, not yet
  tuned against oscilloscope measurements.
- Election/timeout handling has only been reasoned about for the 2-node case; multi-node
  tie-breaking and network-partition scenarios are unimplemented.
