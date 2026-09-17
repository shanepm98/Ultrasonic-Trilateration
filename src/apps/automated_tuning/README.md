# Automated Sensor Tuner
The ICU-20201 sensors are not optimally configured out-of-the-box; many parameters need to be tuned for the actual environment they will be used in.
The recording window of the sensor is divided into segments, and each segment can be configured differently to optimize reception/transmission at various
distances. 

The purpose of this application is to attempt to automate the tuning of 2 sensors operating in pitch-catch mode, using a control script on a host PC to try new config values experimentally and read the results. Like in the "pitch_catch_mode_hardware_test" app,
the program will run on the V3 PCB, and the receiving unit will own the measurement loop and will be connected to the host computer via USB. 
A python script running on the host computer will be responsible for processing the data readout reported by the receiving sensor and sending new configuration values.

## Operation
The program will work as follows:
### Python control script
The control script will run on the host PC and communicate over USB serial to the receiver board. The control script does the following:
- Take input from the user via STDIN to select between manual and automatic parameter tweaking mode. In automatic mode, let user enter the actual measured distance between sensors to use as feedback
- Read the distance measurements reported by the receiver board and compare them to the actual distance specified by the user
- Generate new config values for each sensor measurement segment and send them to the receiver board, via a low-level remote function call API exposed by the receiver.
- Save the best configuration values found so far for the current distance to a file under "configs/" in JSON format; e.g, "configs/3meter.config"

### Receiver/master board
The receiver board is responsible for communicating with the control script on the host, triggering the measurement cycles (via hardwired connection to transmitter's INT1 pin), and communicating new config values to the transmitter board.
The receiver will communicate with the host PC via USB serial, and will communicate wirelessly with the transmitter via ESP-NOW. The receiver does the following:
- Expose a low-level remote function call API over serial to let the control script remotely trigger local SonicLib API sensor config functions with arguments passed by the control script.
- Reconfigure the sensor in real time using values sent by the control script
- Trigger sensor measurements in a continuous loop and send the results back to the control script
- Use ESP-NOW to forward the configuration changes to the transmitter to keep their config profiles in sync

### Transmitter/slave board
- Expose a sensor configuration API over ESP-NOW so that it can keep its configuration in sync with the receiver unit. 
- Continuously reconfigure local sensor with new values from receiver


## Protocol
The USB RPC (host <-> receiver) and ESP-NOW config relay (receiver -> transmitter) are both defined in
`include/at_protocol.h`, mirrored by hand for the host script in `host/protocol.py`. Both are binary,
not clear-text, since only a program talks to either link:
- **USB link**: COBS-framed messages (`0x00` delimiter, trailing CRC-16) carrying either an RPC call/response
  pair (host calls a SonicLib config function by opcode, receiver replies with SonicLib's own status code) or
  an async telemetry stream of measured distances.
- **ESP-NOW link**: the receiver pushes one complete `at_config_snapshot_t` per tuning iteration (not a
  per-call relay), so a dropped packet can never leave the transmitter in a half-updated state.

See the header comments in `at_protocol.h` for the full opcode table, wire layouts, and the reasoning
behind each design choice (framing, snapshot-vs-incremental relay, versioning).

## To-Do 
- [x] Design the communication protocol for efficiently sharing configuration data from receiver to transmitter via ESP-NOW
- [x] Design the low-level remote function call API exposed by the receiver over USB for allowing the host to configure the sensor
- [ ] Design the algorithm the control script will use for experimentally adjusting control values
- [ ] Write the control python script 
- [x] Write the receiver firmware
- [x] Write the transmitter firmware 
- [ ] Test in hardware
