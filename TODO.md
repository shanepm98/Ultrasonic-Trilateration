# To - Do

## General housekeeping + Documentation
- [ ] Update and clean up this to-do. Lots of now-irrelevant details.
- [ ] Add schematic for the IR triggering circuit.
- [ ] Add schematic for the buffered 32.768KHz clock oscillator
- [x] Remove the obsolete wireless sync testing and auto-calibration tests
- [ ] Revise the documentation to reflect new change in project scope, that being that we are now
	  only concerned with 2-dimensional positioning for this proof of concept. 
- [ ] Get website launched to document the process in more detailed writeups of each stage. 
- [ ] Write up final research paper detailing the entire project: motivation, theory of operation, hardware design,
	  software design, next steps, etc.

## Hardware Design & testing
- [ ] Bench-test to verify that clocking the ESP32 externally via GPIO33 works. 
- [ ] Change the terminology used on the solder bridges
- [ ] Add red/green LEDs for indicating operational status (power-on self-test result, runtime errors, etc)
- [ ] Design and test a new version of the IR trigger circuit that triggers on the tail end of the 
	  LED signal, hopefully this will have a more consistent latency since the automatic gain control 
	  is already adjusted (as the AGC adjustment seems to be the cause of the variable latency)
- [ ] Wire up and test the 32KHz buffered oscillator circuit. 
- [ ] Design the omni-directional transmitter (the object to be tracked). Current plan is to implement it as a hexagon
	  with an ultrasonic transmitter on each edge, and 360-degree-FoV IR emitter/demodulator pair on top.

## IR-triggered pitch-catch (`src/apps/ir_accuracy_test/`)
`sender/` + `receiver/` - wireless version of `receiver_readout`: the receiver's IR burst
(GPIO25, 38 kHz, 5 ms) triggers both sensors via each board's IR demodulator on INT1; GPIO2 is
high-impedance on both boards; GPIO26 held high powers the IR module. **Verified on hardware
(2026-09-28): end-to-end test passes.** See `src/apps/ir_accuracy_test/README.md`.
- [x] First on-hardware run (2026-09-28): receiver's IR burst triggers both sensors, and the
      receiver dumps the I/Q trace.
- [ ] Scope receiver GPIO25 (38 kHz, ~5 ms per Enter) and INT1 on both boards (clean low pulse,
      no contention from GPIO2).
- [x] Trigger edge (2026-09-28): LED idles low, demodulator and INT1 are both active-low, so the
      measurement starts on the falling edge at the start of the IR burst. The ~5 ms low pulse
      works; burst length doesn't affect trigger timing.
- [ ] Measure trigger skew between the two boards (INT1 falling edges on the scope) - this is the
      core accuracy number for this approach.
- [ ] Capture readouts at known distances and compare `range_mm` / waveforms against the hardwired
      `pitch_catch_mode_hardware_test` captures (`readouts/15ft`, `readouts/25ft`).
- [x] IR LED drive polarity (2026-09-28): active-high, idles low (off), as the firmware assumes.
- Receiver console menu (2026-10-02): `m` -> (1) IR timing tuner (optional pre-burst on/off +
  trigger-burst length, Space = test trigger), (2) raw I/Q readout, (3) distance (`RANGE` line).
  IR waveform moved from LEDC + busy-wait to RMT (exact to 1 us). **Builds clean, not yet run on
  hardware.**
  - [ ] Flash and walk through the menu: tuner prompts/echo, Backspace restart, Enter save, `m`
        from modes 2/3. Confirm the serial monitor's Enter doesn't fire twice.
  - [ ] Scope GPIO25 with a pre-burst (e.g. 600/600/5000 us): exact envelope, 38 kHz carrier,
        LED off between triggers.
  - [ ] Use the tuner to find the shortest reliable trigger burst and whether a pre-burst
        (demodulator AGC settling) tightens the trigger skew between the boards.

## General performance tweaks
- [ ] Raise SPI clock if needed. Running at 1MHz right now, can go up to 13MHz
- [ ] Tune the `RF_*` `#define`s at the top of `rangefinder_loop.c` against real targets:
      thresholds (false targets vs missed targets), `RF_TX_PULSE_US` (far-target echo strength),
      `RF_RX_GAIN_REDUCE` / `RF_RINGDOWN_CANCEL_SAMPLES` (near-field / ringdown false targets).
- [ ] Optional, improves close-range accuracy: `ch_set_init_firmware(&dev, icu_init_init)` before
      `ch_group_start()` + `ch_meas_optimize()` after building the queue (auto-dampens ringdown;
      ~250 ms, run once). `icu_init` is already compiled in.


## Self-mapping relative coordinate system
The stationary beacons should be able to coordinate with each other and use distance from one another to establish their own local,
relative coordinate system in which to locate the mobile client. More research needed into the feasibility of this.



