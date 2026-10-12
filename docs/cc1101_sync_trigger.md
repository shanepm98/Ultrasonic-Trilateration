# CC1101 sync-trigger test

App: `src/apps/cc1101_sync_trigger_test/` (spec and usage in its `README.md`). The question is
whether a 433 MHz OOK radio burst can trigger every node within about 1 us of the others. Absolute
TX -> RX latency doesn't matter. Only the spread between receivers does.

## Code
| Where | What |
|---|---|
| `src/components/cc1101/` | Reusable SPI driver: `cc1101_init` (manual reset, datasheet 19.1.2), register read/write/burst, status reads (header `0xC0\|addr`, i.e. 0xF0-0xFD), strobes, PATABLE, `cc1101_wait_marcstate`. |
| `src/components/console_io/` | Menu input helpers, moved from `ir_accuracy_test/receiver/main/`. New: `console_read_line`, `console_read_hex` (byte string, optional `0x`), `console_read_hex_uint`. |
| `.../common/cc_link/cc_ook.c` | SPI2 bring-up (MOSI 23 / MISO 19 / SCLK 18, ICU CS GPIO5 held high), the async-OOK register table, data-rate math, register dump. |
| `.../common/cc_link/cc_link.c` | ESP-NOW: NVS + Wi-Fi STA on channel 1, a broadcast peer, and a receive callback that only queues frames. |
| `.../common/cc_link/cc_proto.c` | Format and parse `RR:VV`, `DUMP`, `REGS:<hex>`. |
| `.../transmitter/main/ook_tx.c` | RMT on GPIO27 at 10 MHz, bytes encoder (one symbol per bit, NRZ, MSB first), STX -> send -> SIDLE. |

### SPI details
- CC1101 SPI is mode 0 at 1 MHz. Chip select is manual (GPIO25), so the driver can poll MISO for the
  chip-ready (SO low) signal after pulling CSn low.
- Each access holds the bus (`spi_device_acquire_bus`) for the whole CSn-low window. That way an
  ICU-20201 transaction can never run while the CC1101 is selected.
- `cc_ook_init()` calls `spi_bus_initialize()` itself, because this app has no SonicLib. An app that
  also runs the SonicLib BSP must instead add the CC1101 to the bus that `chbsp_esp32_init()` brings up.

## Register choices (26 MHz crystal)
| Register | Value | Why |
|---|---|---|
| IOCFG2 | 0x0D | GDO2 = async serial data out, for the scope |
| IOCFG1 / IOCFG0 | 0x2E / 0x2E | MISO high-Z when deselected (shared bus); GDO0 is the async TX data input |
| PKTCTRL0 | 0x32 | async serial mode, infinite length, no CRC |
| FREQ2..0 | 0x10 B0 71 | 433.92 MHz |
| MDMCFG4 (high nibble) | 0x5_ | 325 kHz RX filter bandwidth |
| MDMCFG4 (low nibble) / MDMCFG3 | from bit rate | DRATE_E / DRATE_M, closest to the requested rate |
| MDMCFG2 | 0x30 | ASK/OOK, no sync word |
| MCSM0 | 0x18 | autocalibrate on IDLE -> RX/TX |
| AGCCTRL2/1/0 | 0x03 / 0x00 / 0x91 | DN022 OOK recommendations (you can change them at runtime) |
| FREND0 + PATABLE | 0x11, {0x00, 0xC0} | OOK switches between PATABLE[0] (off) and [1] (about +10 dBm) |
| FSCTRL1, FSCAL*, TEST* | SmartRF Studio values | |

The transmitter's bit rate (menu `2`) changes only the transmitter: its RMT bit time and its own
CC1101 data rate. The receivers' async-RX output doesn't depend on their data-rate setting.
The 325 kHz RX bandwidth and the AGC settings are the main levers for the receive-side skew. Tune
them across all receivers with menu `3`.
