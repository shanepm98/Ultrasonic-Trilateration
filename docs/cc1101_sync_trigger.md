# CC1101 sync-trigger test

App: `src/apps/cc1101_sync_trigger_test/` (spec and usage in its `README.md`). The question is
whether a 433 MHz ASK radio burst can trigger every node within about 1 us of the others. Absolute
TX -> RX latency doesn't matter. Only the spread between receivers does.

## Code
| Where | What |
|---|---|
| `src/components/cc1101/` | Reusable SPI driver: `cc1101_init` (manual reset, datasheet 19.1.2), register read/write/burst, status reads (header `0xC0\|addr`, i.e. 0xF0-0xFD), strobes, PATABLE, `cc1101_wait_marcstate`. |
| `src/components/console_io/` | Menu input helpers, moved from `ir_accuracy_test/receiver/main/`. New: `console_read_line`, `console_read_hex` (byte string, optional `0x`), `console_read_hex_uint`. |
| `.../common/cc_link/cc_ask.c` | SPI2 bring-up (MOSI 23 / MISO 19 / SCLK 18, ICU CS GPIO5 held high), the async-ASK register table, data-rate math, register dump. |
| `.../common/cc_link/cc_link.c` | ESP-NOW: NVS + Wi-Fi STA on channel 1, a broadcast peer, and a receive callback that only queues frames. |
| `.../common/cc_link/cc_proto.c` | Format and parse `RR:VV`, `DUMP`, `REGS:<hex>`. |
| `.../transmitter/main/ask_tx.c` | RMT on GPIO27 at 10 MHz, bytes encoder (one symbol per bit, NRZ, MSB first). `ask_tx_carrier_on/off` (STX / SIDLE); `ask_tx_send` only clocks data on the running carrier. |

### SPI details
- CC1101 SPI is mode 0 at 1 MHz. Chip select is manual (GPIO25), so the driver can poll MISO for the
  chip-ready (SO low) signal after pulling CSn low.
- Each access holds the bus (`spi_device_acquire_bus`) for the whole CSn-low window. That way an
  ICU-20201 transaction can never run while the CC1101 is selected.
- `cc_ask_init()` calls `spi_bus_initialize()` itself, because this app has no SonicLib. An app that
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
| MDMCFG2 | 0x30 | ASK/OOK modulation format, no sync word |
| MCSM0 | 0x18 | autocalibrate on IDLE -> RX/TX |
| AGCCTRL2/1/0 | 0x03 / 0x00 / 0x91 | DN022 OOK recommendations, kept as a starting point; tune at runtime. Under OOK, AGCCTRL2 = 0xC3 (low gain only) was the most noise-immune (`AGC_config.txt`) |
| FREND0 + PATABLE | 0x11, {0x34, 0xC0} | ASK: data 0 = PATABLE[0] (about -10 dBm), data 1 = PATABLE[1] (about +10 dBm), so 20 dB modulation depth |
| FSCTRL1, FSCAL*, TEST* | SmartRF Studio values | |

The transmitter's bit rate (menu `2`) changes only the transmitter: its RMT bit time and its own
CC1101 data rate. The receivers' async-RX output doesn't depend on their data-rate setting.
The 325 kHz RX bandwidth and the AGC settings are the main levers for the receive-side skew. Tune
them across all receivers with menu `3`.

## ASK instead of OOK (2026-10-13)
With OOK the carrier is off between bursts (and for every 0 bit), so the receivers' AGC turns its
gain up to noise and GDO2 chatters on spurious noise. The CC1101 has no separate ASK format:
MOD_FORMAT = 3 switches the PA between PATABLE[0] and PATABLE[1], and OOK is just PATABLE[0] = 0x00.
ASK here means:

- PATABLE[0] is a non-zero "0" level (0x34, about -10 dBm), and PATABLE[1] = 0xC0 (about +10 dBm).
- The transmitter enters TX at boot and **stays in TX**, so the carrier is always present. GDO0
  idles low (the "0" level), and a send only toggles the amplitude.
- The modulation depth can be tuned at runtime with register pseudo-addresses 0x3E (PATABLE[0]) and
  0x3F (PATABLE[1]). They work in menu `4` (local) and in menu `3` / ESP-NOW `RR:VV`. 0x3E matches the
  chip's own PATABLE address; 0x3F is this app's own pseudo-address for the second entry (on the chip,
  0x3F is the FIFO). DN013 433 MHz levels: 0xC0 +10, 0x60 0, 0x34 -10, 0x1D -15, 0x0E -20,
  0x12 -30 dBm.
- The transmitter is a continuous 433 MHz emitter while it runs. Keep the power low and keep the test
  short, within the local ISM duty-cycle rules.
