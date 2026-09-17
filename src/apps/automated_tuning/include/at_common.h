/*! \file at_common.h
 *
 * \brief Physical/timing constants shared between the automated_tuning receiver and
 * transmitter firmware - distinct from at_protocol.h, which defines the wire format. Unlike
 * pitch_catch_mode_hardware_test, this app has no compile-time TX pulse/ODR/max-range constants
 * at all (those are entirely host/RPC-driven, see at_protocol.h), so only what's genuinely fixed
 * by the PCB and the tuning workflow lives here.
 */

#ifndef AT_COMMON_H_
#define AT_COMMON_H_

#include "driver/gpio.h"

/* Transmitter's sensor INT2 (data-ready) line, physically routed to this GPIO on the receiver
 * board - same role as pitch_catch_common.h's PC_SENDER_READY_GPIO. ASSUMPTION: automated_tuning's
 * V3 PCB wiring is identical to pitch_catch_mode_hardware_test's; confirm against the schematic
 * before flashing. If it differs, only this line needs to change. */
#define AT_SENDER_READY_GPIO GPIO_NUM_33

/* Receiver's trigger cadence, and how long it waits for its own data-ready after triggering. */
#define AT_TRIGGER_INTERVAL_MS 100u /* 10 Hz */
#define AT_RESPONSE_TIMEOUT_MS 60u  /* > worst-case measurement time at the configured max range, < interval */

/* Fixed WiFi channel both sides set explicitly after esp_wifi_start() - no AP association ever
 * occurs, so nothing else picks this channel for us, and stale NVS state must not be allowed to
 * either (see espnow_link_init() in both apps: WIFI_STORAGE_RAM + an explicit
 * esp_wifi_set_channel() call). */
#define AT_ESPNOW_CHANNEL 1u

#endif /* AT_COMMON_H_ */
