# CC1101 sync-trigger test

App: `src/apps/cc1101_sync_trigger_test/` (spec and usage in its `README.md`). The question is
whether a 433 MHz ASK radio burst can trigger every node within about 1 us of the others. Absolute
TX -> RX latency doesn't matter. Only the spread between receivers does.

## Code
| Where | What |
|---|---|
| `src/components/cc1101/` | Reusable SPI driver: `cc1101_init` (manual reset, datasheet 19.1.2), register read/write/burst, status reads (header `0xC0\|addr`, i.e. 0xF0-0xFD), strobes, PATABLE, `cc1101_wait_marcstate`. |
| `src/components/console_io/` | Menu input helpers, moved from `ir_accuracy_test/receiver/main/`. New: `console_read_line`, `console_read_hex` (byte string, optional `0x`), `console_read_hex_uint`. |
| `.../common/cc_link/cc_radio.c` | SPI2 bring-up (MOSI 23 / MISO 19 / SCLK 18, ICU CS GPIO5 held high), `cc_radio_load()` (burst-write a full register set + PATABLE), data-rate math, register dump. Was `cc_ask.c` until 2026-10-26. |
| `.../common/cc_link/cc_cfg_*.c`, `include/cc_cfg_select.h` | The two trigger configs (ASK `cc_cfg_ask_10k`, FSK `cc_cfg_fsk_50k`), each with TX and RX register tables. `CC_CFG_SELECT` picks the one both apps load at boot. |
| `.../common/cc_link/cc_link.c` | ESP-NOW: NVS + Wi-Fi STA on channel 1, a broadcast peer, and a receive callback that only queues frames. |
| `.../common/cc_link/cc_proto.c` | Format and parse `RR:VV`, `DUMP`, `REGS:<hex>`. |
| `.../transmitter/main/radio_tx.c` | RMT on GPIO27 at 10 MHz, bytes encoder (one symbol per bit, NRZ, MSB first). `radio_tx_carrier_on/off` (STX / SIDLE); `radio_tx_send` only clocks data on the running carrier. Was `ask_tx.c`. |

### SPI details
- CC1101 SPI is mode 0 at 1 MHz. Chip select is manual (GPIO25), so the driver can poll MISO for the
  chip-ready (SO low) signal after pulling CSn low.
- Each access holds the bus (`spi_device_acquire_bus`) for the whole CSn-low window. That way an
  ICU-20201 transaction can never run while the CC1101 is selected.
- `cc_radio_init()` calls `spi_bus_initialize()` itself, because this app has no SonicLib. An app that
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

## Skew sweep (2026-10-25)
Short overview: [`cc1101_skew_sweep_summary.md`](cc1101_skew_sweep_summary.md).

Automated sweep of modulation × TX bit rate × receiver data rate × RX filter bandwidth × AGC. The
figure of merit is the skew between the **first falling edge** of two receivers' GDO2 outputs for one
`0xAAAAAA` burst. Scope: Siglent SDS1052DL+ (CH1 = receiver B0:B2:1C:A8:AD:80, CH2 = 8C:94:DF:58:7D:60),
20 ns/sample, ±164 µs window, triggered on CH1 falling.
- **Size:** 117 configs, 1403 shots, 1039 valid.
- **Data:** `readouts/skew_sweep_20261025.{csv,log}` and `_summary.txt`, local only (gitignored).

### Method
- **Tools:** `host/skew_sweep.py` drives the transmitter menu and the scope. `host/skew.py` does the
  edge extraction. Usage is in the app README.
- **Valid shot:** both receivers must start the capture in the same logic state. With a steady carrier
  each receiver holds its last level and drifts back to idle on its own after a burst; a receiver that
  starts in the wrong state would put its first falling edge on a later bit.
- **Failure statuses:** `ch2_missing` (no CH2 falling edge within ±164 µs of CH1's), `noise_trigger` (the
  scope triggered before the burst was sent, i.e. CH1 chatters at idle), `no_trigger`.
- **Ranking:** by 90th-percentile |skew|, because a trigger has to be good on its bad shots. Configs that
  fail on more than 30% of shots rank last. Each config is 10 valid shots, so treat differences under
  about 0.1 µs as noise.
- **Stage 1** (AGC 05/00/91): ASK/FSK/MSK × TX 1, 10, 50, 150 kBaud (MSK 50/150 only) × RX data rate 10,
  50, 160 kBaud × filter 203, 325, 541 kHz. Six configs were skipped because the filter was narrower
  than Carson's bandwidth (FSK/MSK 150 kBaud at 203 kHz).
- **Stage 2:** the three best stage-1 configs per modulation × the other AGC presets: 03/00/91 (DN022),
  C3/00/91 (low gain), 03/40/91 (chip default).
- **Register application:** the receivers' registers were verified via menu `5` before every config
  (both nodes, all 43 non-calibration config registers). Lost ESP-NOW replies happened 4 times and
  were re-broadcast automatically; no config failed.

### Results
**The receivers' data-rate setting is the dominant factor**, even though async RX has no bit
synchronizer. The demodulator's internal sample rate follows DRATE, so a higher RX data rate means
finer edge timing:

| RX data rate (MDMCFG4[3:0] / MDMCFG3) | Configs | Configs with p90 < 1 µs | Median p90 across configs |
|---|---|---|---|
| 10 kBaud (0x8 / 0x93) | 30 | 0 | 14.2 µs |
| 50 kBaud (0xA / 0xF8) | 36 | 0 | 2.7–10.5 µs |
| **160 kBaud (0xC / 0x93)** | 51 | **26** | **0.7–0.85 µs** |

Best configs (all at RX 160 kBaud). Skew is CH2 − CH1 in µs:

| Config | Median | p90 \|skew\| | Std | Max \|skew\| | Fails |
|---|---|---|---|---|---|
| **ASK, TX 10k, RX 160k, 541 kHz, AGC 05/00/91** | +0.18 | **0.35** | 0.31 | 0.65 | 0% |
| FSK, TX 50k, RX 160k, 541 kHz, AGC C3/00/91 | +0.02 | 0.38 | 0.28 | 0.68 | 9% |
| FSK, TX 50k, RX 160k, 541 kHz, AGC 03/40/91 | −0.04 | 0.39 | 0.26 | 0.49 | 0% |
| ASK, TX 10k, RX 160k, 203 kHz, AGC 05/00/91 | +0.00 | 0.43 | 0.30 | 0.44 | 0% |
| ASK, TX 10k, RX 160k, 325 kHz, AGC 05/00/91 | −0.03 | 0.45 | 0.39 | 0.94 | 0% |
| ASK, TX 1k, RX 160k, 541 kHz, AGC 05/00/91 | +0.14 | 0.46 | 0.30 | 0.50 | 0% |
| ASK, TX 1k, RX 160k, 325 kHz, AGC 05/00/91 (previous best) | −0.14 | 0.53 | 0.30 | 0.53 | 0% |

AGC (stage 2, p90 |skew| in µs on the same base configs):

| Base config | 05/00/91 | 03/00/91 | C3/00/91 | 03/40/91 |
|---|---|---|---|---|
| ASK, TX 10k, RX 160k, 203 kHz | **0.43** | 1.52 | 1.88 | 108.6 |
| ASK, TX 10k, RX 160k, 325 kHz | **0.45** | 0.68 | 0.66 | 1.13 |
| ASK, TX 10k, RX 160k, 541 kHz | **0.35** | 1.45 | 1.70 | 1.13 |
| FSK, TX 150k, RX 160k, 325 kHz | 0.50 | 0.56 | 0.56 | 0.48 |
| FSK, TX 1k, RX 160k, 203 kHz | 0.66 | 0.64 | 0.91 | 0.76 |
| FSK, TX 50k, RX 160k, 541 kHz | 0.46 | 0.58 | 0.38 | 0.39 |
| MSK, TX 50k, RX 160k, 325 kHz | 4.03 | none | none | none |

**Conclusions**
- **ASK with AGCCTRL2 = 0x05 is the most consistent.** It is the only setup whose max |skew| stays under
  1 µs across all three filter bandwidths at RX 160k, and it beats every other AGC preset for ASK.
- **FSK is about as good at its best and insensitive to AGC**, but some FSK configs show occasional
  whole-bit outliers: 15–39 µs single shots at TX 50k with 203/325 kHz.
- **MSK in async mode is not viable.** Its best p90 is 4 µs, and with any AGC except 05/00/91 the
  receivers stop producing edges.
- **TX bit rate matters little** at RX 160k: 1–10 kBaud is best, and 150 kBaud is worse.
- **Filter bandwidth is secondary** at RX 160k. At lower RX rates, 541 kHz mostly misses edges or
  chatters.
- **Recommended:** ASK, TX 10 kBaud, receivers MDMCFG4 = 0x2C (541 kHz) or 0x8C (203 kHz),
  MDMCFG3 = 0x93, AGC 05/00/91. That gives p90 ≈ 0.35–0.43 µs and max ≈ 0.44–0.65 µs over 10 shots.

### Setup problems found during the sweep
- **Firmware bug (fixed):** `ask_tx_carrier_off()` (now `radio_tx_carrier_off()`) strobed SIDLE without waiting for IDLE, so the STX
  that follows was ignored. Every second local register change (menu `2`/`4`) left the transmitter's
  carrier **off**, so earlier manual ASK tests may have run without a carrier part of the time. Both
  the transmitter and the receiver write path now wait for MARCSTATE = IDLE after SIDLE. The
  transmitter is reflashed; the receivers' change builds but is not flashed yet.
- **Serial link:** the transmitter's CP2102 link corrupted data intermittently (two of three flash
  attempts failed). On one boot its CC1101 read back all zeros and the firmware aborted. Both point
  to a marginal supply or connection (see the TODO about a separate 3.3 V LDO).
- **Board reset:** opening that port with DTR/RTS forced low resets the DevKit; a default open does
  not.
