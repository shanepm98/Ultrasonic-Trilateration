# CC1101-Based Synchronous Trigger Test
This app will test the viability of using CC1101 radio modules for synchronously triggering the ICU-20201 sensors on each node.
It is not the absolute latency between tx and rx that is important, but rather the difference between rx times of each node- they must be as close
in time as possible, ideally all firing well within 1 microsecond of each other, in order to minimize error in distance measurement.

This test will be performed with one transmitter node and multiple receiver nodes, with the receiver nodes being bench-tested to measure difference in data output
times. 

The CC1101 modules will be configured for asynchronous ASK mode at 433 MHz (changed from OOK on 2026-10-13: OOK was too susceptible to spurious noise), and will be on the same SPI bus as the ICU-20201 sensors. Pins GDO0 and GDO2 on the CC1101 will be
configured as serial data in and serial data out, respectively. 

## Pinout information
ESP32 pin | CC1101 pin
______________________
GPIO25	  | CSN
GPIO27    | GDO0

During bench testing, the GDO2 pin will be disconnected from the ESP32 and read directly by an oscilloscope.

## CC1101 configuration
I will leave most of the radio configuration to Claude, as it is straight-forward. I will specify the following register values, per design note DN022:
AGCCTRL2 = 0x03 to 0x07
AGCCTRL1 = 0x00
AGCCTRL0 = 0x91 or 0x92

## Functional description
Both the transmitter and receiver programs should begin by programming their CC1101 module for asynch ASK as described previously, and
then dump CC1101 register values in hexadecimal for register addresses 0x00-0x2E, and 0xF0-0xFD.

For the receiver program, the device then enters loop that listens for ESP-NOW broadcasts from the transmitter node in "REGISTER:VALUE" format, which tell
each node to write the specified VALUE to the specified CC1101 REGISTER (to dynamically change configurations without recompiling and without hardwired connection).

For the transmitter program, the device should enter an interactive menu. Pressing "1" allows you to set the transmit data in hexadecimal (eg "0xAAAAAA"),
"2" allows you to change bit rate in KBaud, and "SPACE" sends configured transmit data. Configured transmit data and data rate handled by the RMT peripheral on the 
ESP32 and should persist until changed by user.
"3" allows you to broadcast ESP-NOW messages so that each node can dynamically reconfigure their local CC1101 unit. Messages should be simple broadcasts indicating
CC1101 register address and value to be written. 
"4" allows you to change local CC1101 register values by entering register address in hex and value in hex.
"5" broadcasts an ESP-NOW command that tells all receiver nodes to broadcast their current local CC1101 register values over ESP-NOW broadcast, and then listens
for and prints these broadcasts from receiver nodes. The registers should be the same addresses printed on startup.

## Implementation (2026-10-11)
Built, not yet tested on hardware. Design notes and register choices: `docs/cc1101_sync_trigger.md`.

- `transmitter/` - radio setup, register dump, then the console menu (`main/tx_menu.c`). The RMT
  peripheral clocks the data out on GPIO27/GDO0 (`main/ask_tx.c`): NRZ, MSB first, 0.1 us
  resolution. The radio goes into TX at boot and stays there, so the carrier is always on at the
  "0" level (PATABLE[0]). `SPACE` only clocks the data, raising it to the "1" level (PATABLE[1])
  for each 1 bit. Menu `2` and `4` drop to IDLE, write, and go back to TX. The defaults are
  `0xAAAAAA` at 10 kBaud. `d` dumps the local registers again.
- `receiver/` - radio setup, SRX, register dump, then it serves ESP-NOW commands. Each register
  write is done in IDLE, then the radio goes back to RX.
- `common/cc_link/` - code both apps share: SPI bus + CC1101 config (`cc_ask`), ESP-NOW link
  (`cc_link`), message format (`cc_proto`).
- `src/components/cc1101/` - the reusable CC1101 SPI driver. `src/components/console_io/` - the
  menu input helpers (moved here from `ir_accuracy_test`).

### ESP-NOW messages (ASCII broadcast, Wi-Fi channel 1)
| Message | Direction | Meaning |
|---|---|---|
| `RR:VV` (e.g. `1B:07`) | TX -> RX | write hex value VV to register RR (0x00-0x2E; 0x3E = PATABLE[0], the "0" amplitude; 0x3F = PATABLE[1], the "1" amplitude) |
| `DUMP` | TX -> RX | every receiver replies with its registers |
| `REGS:<122 hex digits>` | RX -> TX | 61 bytes: config 0x00-0x2E, then status 0x30-0x3D. Sent after a random 0-100 ms delay |

### Usage
```
cd transmitter && PORT=/dev/ttyUSB0 ./build.sh flash monitor
cd receiver    && PORT=/dev/ttyUSB1 ./build.sh flash monitor   # once per receiver node
```
On the scope, probe the transmitter's GPIO27 (data in) and each receiver's GDO2 (data out).
Send `0xAAAAAA` at a few bit rates and measure the skew between the receivers' GDO2 edges.
