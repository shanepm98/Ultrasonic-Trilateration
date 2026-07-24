# Project Journal
This doc is for briefly summarizing daily progress/thoughts/setbacks for future reference

## 7-22-2026
- Designed a circuit that can function as either master or slave beacon, designed the PCB, and ordered that + parts. Waiting on board and parts. Next step is to bring up the hardware and do a hardware test of the board support package

## 7-8-2026
- Had Claude assist in writing the SonicLib board support package for the ESP32 Devkit V1. This is the intermediate layer between the high-level SonicLib business logic and the low-level ESP32 hardware, basically the driver to control the pins and timers. It is NOT tested in hardware yet, although it is verified to build successfully. Also had claude write `docs/board_support_package.md` and update `TODO.md` with next steps for testing the BSP

## 7-5-2026
- Revised the test rig to use the newer ICU-20201 ultrasonic chip instead of the CH201. Redesigned the test rig so that each sensor has its own ESP32 for the sake of convenience and not running a ton of long wires

## 6-24-2026
- Finished and uploaded schematics for a CH201 test rig. Two CH201's operate in pitch-catch mode, controlled by an ESP32. Hardware has yet to be built and I still need to write the ESP32 firmware

## 6-2-2026
- Created this GitHub account

