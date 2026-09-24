/*! \file gpio_strobe.h
 *
 * \brief Drives GPIO25 high for 10ms, 10 times per second, on boundaries of the FTSP-synchronized
 * clock (root time, not local time) so that two boards running this app strobe together. Uses
 * esp_timer exclusively for the actual pulse timing (not vTaskDelay) for precision; the schedule
 * is re-derived every cycle (from the root's explicitly-announced rendezvous instant when one is
 * available, otherwise derived locally - see ftsp_sync.h's rendezvous note and
 * schedule_next_rise() in gpio_strobe.c) so skew corrections and fresher announcements keep
 * applying instead of a single schedule drifting over time.
 */

#ifndef GPIO_STROBE_H_
#define GPIO_STROBE_H_

#include "driver/gpio.h"
#include "esp_err.h"

#include "ftsp_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_STROBE_PIN GPIO_NUM_25
/* 10 Hz - kept in lockstep with ftsp_sync's rendezvous period by definition, not just by
 * convention: the root announces "next strobe" instants on its own FTSP_EVENT_PERIOD_US grid, so
 * this app's strobe period cannot be anything else. See ftsp_sync.h. */
#define GPIO_STROBE_PERIOD_US FTSP_EVENT_PERIOD_US
#define GPIO_STROBE_HIGH_US   10000 /* 10 ms high per pulse */

/*! \brief Configure GPIO_STROBE_PIN as an output (initially low) and create the rise/fall
 * one-shot timers. Call once, before gpio_strobe_run(). */
esp_err_t gpio_strobe_init(void);

/*! \brief Wait until ctx has a usable clock (root, or follower with a valid regression fit),
 * then strobe forever on FTSP-synchronized 100ms boundaries. Does not return. */
void gpio_strobe_run(ftsp_sync_ctx_t *ctx) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* GPIO_STROBE_H_ */
