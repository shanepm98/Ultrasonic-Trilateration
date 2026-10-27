# CC1101 configs to test further

This is the shortlist from the 2026-10-25 automated skew sweep. Each config needs **100+ shots** to
confirm it; the sweep took only 10 per config, which resolves differences of about 0.1 µs. Ideally
also test at real node spacing.

The shortlist is:
- the top 4 configs by sweep rank (90th-percentile |skew|, with configs failing >30% of shots ranked
  last),
- FSK with a 203 kHz filter and receivers at 160 kBaud, with the transmitter at 1 kBaud and at
  10 kBaud.

Skew is CH2 − CH1 at the first falling edge, in µs. Background: `docs/cc1101_skew_sweep_summary.md`.

**Status (2026-10-26):** all six were run at 100 shots each (`host/skew_sweep.py --shortlist`).
Results are in `docs/cc1101_skew_sweep_summary.md` ("Confirmation run"). Only #2 (FSK, TX 50k,
541 kHz, AGC C3/00/91) kept every shot under 1 µs (max 0.87 µs). #1 was next (max 1.07 µs). #3 had a
19.9 µs whole-bit outlier.

## Shortlist

| # | Config ID (`skew_sweep.py`) | Mod | TX kBaud | RX kBaud | Filter | AGC 2/1/0 | Sweep p90 | Sweep max | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `ASK_tx10_rx160_bw541_agc050091` | ASK | 10 | 160 | 541 kHz | 05/00/91 | 0.35 | 0.65 | Best overall |
| 2 | `FSK_tx50_rx160_bw541_agcC30091` | FSK | 50 | 160 | 541 kHz | C3/00/91 | 0.38 | 0.68 | 1 `ch2_missing` in 11 shots |
| 3 | `FSK_tx50_rx160_bw541_agc034091` | FSK | 50 | 160 | 541 kHz | 03/40/91 | 0.39 | 0.49 | |
| 4 | `ASK_tx10_rx160_bw203_agc050091` | ASK | 10 | 160 | 203 kHz | 05/00/91 | 0.43 | 0.44 | |
| 5 | `FSK_tx1_rx160_bw203_agc050091` | FSK | 1 | 160 | 203 kHz | 05/00/91 | 0.66 | 1.07 | Also tested: AGC 03/00/91 p90 0.64, C3/00/91 0.91 (one 30 µs shot), 03/40/91 0.76 |
| 6 | `FSK_tx10_rx160_bw203_agc050091` | FSK | 10 | 160 | 203 kHz | 05/00/91 | 0.91 | 1.44 | Only AGC tested at this TX rate |

## Register values

These are the values `skew_sweep.py` writes, at a 26 MHz crystal. The receivers' DRATE for 160 kBaud
is E = 0xC (MDMCFG4 low nibble), M = 0x93 (MDMCFG3).

**Receivers** (menu `3` broadcast):

| # | MDMCFG4 (0x10) | MDMCFG3 (0x11) | MDMCFG2 (0x12) | DEVIATN (0x15) | AGCCTRL2 (0x1B) | AGCCTRL1 (0x1C) | AGCCTRL0 (0x1D) |
|---|---|---|---|---|---|---|---|
| 1 | 0x2C | 0x93 | 0x30 | 0x47 | 0x05 | 0x00 | 0x91 |
| 2 | 0x2C | 0x93 | 0x00 | 0x40 | 0xC3 | 0x00 | 0x91 |
| 3 | 0x2C | 0x93 | 0x00 | 0x40 | 0x03 | 0x40 | 0x91 |
| 4 | 0x8C | 0x93 | 0x30 | 0x47 | 0x05 | 0x00 | 0x91 |
| 5 | 0x8C | 0x93 | 0x00 | 0x15 | 0x05 | 0x00 | 0x91 |
| 6 | 0x8C | 0x93 | 0x00 | 0x15 | 0x05 | 0x00 | 0x91 |

**Transmitter:** menu `2` sets the bit rate (RMT + DRATE). Menu `4` then writes these:

| # | Menu `2` (kBaud) | → DRATE E / M | MDMCFG4 (0x10) | MDMCFG2 (0x12) | DEVIATN (0x15) | FSK deviation |
|---|---|---|---|---|---|---|
| 1 | 10 | 0x8 / 0x93 | 0x28 | 0x30 | 0x47 | n/a |
| 2 | 50 | 0xA / 0xF8 | 0x2A | 0x00 | 0x40 | 25.4 kHz |
| 3 | 50 | 0xA / 0xF8 | 0x2A | 0x00 | 0x40 | 25.4 kHz |
| 4 | 10 | 0x8 / 0x93 | 0x88 | 0x30 | 0x47 | n/a |
| 5 | 1 | 0x5 / 0x43 | 0x85 | 0x00 | 0x15 | 5.2 kHz |
| 6 | 10 | 0x8 / 0x93 | 0x88 | 0x00 | 0x15 | 5.2 kHz |

All other registers stay at the `cc_ask.c` defaults (433 MHz, PATABLE 34/C0, async serial, GDO2 =
RX data).

## What to look at
- **Statistics:** p90 and max |skew| over 100+ valid shots, plus the rate of `invalid_prestate`,
  `ch2_missing` and `noise_trigger` shots.
- **Whole-bit outliers:** shots that come in one bit late (15–163 µs). These showed up in several
  FSK configs (e.g. #5 with AGC C3) and would ruin a trigger even when the p90 looks good.
- **Real node spacing:** repeat at the actual spacing between nodes, not just on the bench.
