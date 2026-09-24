/*! \file ftsp_sync.h
 *
 * \brief Flooding Time Synchronization Protocol (FTSP) over ESP-NOW: wire packets, lowest-MAC
 * root election, and a linear-regression clock model mapping this node's local esp_timer clock
 * to the elected root's clock.
 *
 * Root election: every node starts in FTSP_ROLE_DISCOVERING, broadcasting HELLO and watching for
 * the lowest MAC address claimed by any node (including itself). After a short discovery window
 * it becomes root (its own MAC is lowest) or follower. A follower that ever sees a lower-MAC
 * claim than its current root adopts it and resets its regression table; a root that sees a
 * lower-MAC claim self-demotes. A follower that hears nothing from its root for
 * FTSP_ROOT_TIMEOUT_MS falls back to discovering (basic self-healing re-election).
 *
 * Rendezvous for the scheduled event (the GPIO strobe): the root is the sole decision-maker for
 * *which* grid instant (a multiple of FTSP_EVENT_PERIOD_US on its own clock) comes next, and
 * announces it explicitly in every SYNC packet (next_strobe_root_us) rather than leaving each
 * follower to independently round its own, noisier real-time clock estimate - decided during
 * hardware bring-up (2026-09-17) so the "which boundary is next" decision always happens on the
 * one clock that's definitionally correct about it (root's own), not a follower's regression
 * estimate evaluated at an arbitrary instant. Followers only ever *translate* the announced value
 * via the regression; they fall back to deriving their own boundary (the old behavior) only when
 * no still-future announcement is available yet (e.g. before the first SYNC, or after a dropped
 * packet) - see gpio_strobe.c's schedule_next_rise().
 *
 * Known limitations, left as-is for this capability-test scaffold and intended to be tuned once
 * on real hardware:
 *  - root_timestamp_us / recv_local_us are software timestamps taken at the esp_now_send() /
 *    recv-callback call sites, not true MAC-layer TX/RX timestamps - WiFi driver queuing and
 *    CSMA backoff jitter leak directly into the sync error budget.
 *  - No outlier/residual rejection before adding a regression sample (the original FTSP paper
 *    discards high-residual points); this scaffold fits a plain least-squares line over the last
 *    FTSP_TABLE_SIZE samples.
 *  - FTSP_TABLE_SIZE and the HELLO/SYNC intervals below are first-pass constants, not yet tuned
 *    against oscilloscope measurements.
 *  - Election/timeout handling has only been reasoned about for the 2-node case; multi-node
 *    tie-breaking and network-partition scenarios are out of scope for this test.
 */

#ifndef FTSP_SYNC_H_
#define FTSP_SYNC_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FTSP_PROTOCOL_VERSION 1u

#define FTSP_TABLE_SIZE 8u

/* A 2-point fit is a line through both points, so a single noisy sample becomes the entire
 * skew estimate - confirmed on hardware (2026-09-17): one ~13ms-outlier SYNC sample, landing as
 * one of only 2 points, produced a wildly wrong skew and multi-ms strobe misalignment. Require
 * more points before trusting a fit, and reject clear outliers against an existing fit. */
#define FTSP_MIN_SAMPLES_FOR_VALID 4u
#define FTSP_OUTLIER_THRESHOLD_US  3000 /* generous vs. the ~300-400us baseline jitter observed, well under the ~13ms outlier seen */

#define FTSP_DISCOVERY_WINDOW_MS 2000u
#define FTSP_HELLO_INTERVAL_MS   150u

/* Must not exceed FTSP_EVENT_PERIOD_US/1000 - a follower needs a fresh, still-future explicit
 * strobe target at least once per event cycle, or gpio_strobe.c has to fall back to deriving its
 * own boundary for that cycle (still correct, just loses the "root decides" property for it). */
#define FTSP_SYNC_INTERVAL_MS 100u
#define FTSP_ROOT_TIMEOUT_MS  1000u

/* Period of the scheduled event both boards rendezvous on (a GPIO strobe, in this app). Lives
 * here (not in gpio_strobe.h) because the root's rendezvous-boundary computation needs it too -
 * gpio_strobe.h's GPIO_STROBE_PERIOD_US is defined in terms of this constant; don't redefine it
 * there. */
#define FTSP_EVENT_PERIOD_US 100000

typedef enum {
    FTSP_MSG_HELLO = 0x01, /* discovery/liveness beacon carrying the sender's currently-claimed root */
    FTSP_MSG_SYNC  = 0x02, /* the root's periodic time-sync beacon */
} ftsp_msg_type_t;

typedef struct __attribute__((packed)) {
    uint8_t msg_type; /* FTSP_MSG_HELLO */
    uint8_t proto_version;
    uint8_t claimed_root_mac[6];
} ftsp_msg_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t  msg_type; /* FTSP_MSG_SYNC */
    uint8_t  proto_version;
    uint8_t  root_mac[6];
    uint32_t seq;
    int64_t  root_timestamp_us;   /* root's esp_timer_get_time(), read immediately before send - feeds the regression */
    int64_t  next_strobe_root_us; /* root's chosen root-clock instant for the next scheduled event - authoritative; see the rendezvous note at the top of this file */
} ftsp_msg_sync_t;

typedef enum {
    FTSP_ROLE_DISCOVERING = 0,
    FTSP_ROLE_ROOT,
    FTSP_ROLE_FOLLOWER,
} ftsp_role_t;

/*! \brief One (local_us, root_us - local_us) sample used to fit the clock regression. */
typedef struct {
    int64_t local_us;
    int64_t offset_us;
} ftsp_point_t;

/*! \brief Least-squares fit of clock offset (root - local) vs. local time, over a small ring
 * buffer of recent sync samples. Safe to copy by value (ftsp_sync_get_regression() does so under
 * a mutex) so gpio_strobe.c can read a consistent snapshot without blocking the sync task. */
typedef struct {
    ftsp_point_t points[FTSP_TABLE_SIZE];
    uint8_t       count;
    uint8_t       next_idx;
    int64_t       x0; /* first sample's local_us; recentres regression inputs to keep them small */
    double        skew; /* d(offset_us) / d(local_us) */
    double        intercept_us; /* fitted offset at local_us == x0 */
    bool          valid; /* true once count >= 2 and a real (non-degenerate) fit exists */
} ftsp_regression_t;

void ftsp_regression_init(ftsp_regression_t *reg);

/*! \brief Add a (local_us, root_us) sample. Once the fit is valid (>= FTSP_MIN_SAMPLES_FOR_VALID
 * points), a new sample whose residual against the *current* fit exceeds FTSP_OUTLIER_THRESHOLD_US
 * is rejected outright (not added, no refit) rather than allowed to corrupt the skew estimate.
 * Returns false if the sample was rejected as an outlier, true if it was added. */
bool ftsp_regression_add_sample(ftsp_regression_t *reg, int64_t local_us, int64_t root_us);

bool ftsp_regression_is_valid(const ftsp_regression_t *reg);
int64_t ftsp_regression_root_from_local(const ftsp_regression_t *reg, int64_t local_us);
int64_t ftsp_regression_local_from_root(const ftsp_regression_t *reg, int64_t root_us);

typedef struct ftsp_sync_ctx ftsp_sync_ctx_t;

/*! \brief Allocate sync state (rx queue + mutex). No radio calls - call espnow_link_init() with
 * the queue returned by ftsp_sync_get_rx_queue() before ftsp_sync_start(). */
esp_err_t ftsp_sync_init(ftsp_sync_ctx_t **out_ctx);

/*! \brief Spawn the FTSP election + sync task. Returns immediately. */
esp_err_t ftsp_sync_start(ftsp_sync_ctx_t *ctx);

/*! \brief Queue that espnow_link_init() should be given - the sync task drains it. */
QueueHandle_t ftsp_sync_get_rx_queue(ftsp_sync_ctx_t *ctx);

ftsp_role_t ftsp_sync_get_role(const ftsp_sync_ctx_t *ctx);

/*! \brief Mutex-guarded snapshot copy of the current regression fit. Safe to call from any task
 * (including esp_timer callback context). Returns false if ctx is NULL. */
bool ftsp_sync_get_regression(const ftsp_sync_ctx_t *ctx, ftsp_regression_t *out);

/*! \brief The most recently agreed root-clock instant for the next scheduled event: the root
 * self-announces it (computed fresh from its own clock on every SYNC send); a follower learns it
 * by copying the value out of each accepted SYNC packet. Returns false if none is known yet (no
 * SYNC received/sent since the last election), in which case the caller should derive one itself
 * (round up to the next multiple of FTSP_EVENT_PERIOD_US) as a fallback. */
bool ftsp_sync_get_next_strobe_root_us(const ftsp_sync_ctx_t *ctx, int64_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FTSP_SYNC_H_ */
