/*! \file rangefinder_loop.h
 *
 * \brief Bring-up rangefinding loop: configure the ICU-20201 for free-running measurements at
 * 5 m full-scale range and stream the measured one-way distance to the serial console.
 *
 * Call after icu_post's post_run() has confirmed the sensor is up (ch_group_init / ch_init /
 * chbsp_esp32_init / ch_group_start all done). rangefinder_run() does not return.
 */

#ifndef RANGEFINDER_LOOP_H_
#define RANGEFINDER_LOOP_H_

#include <invn/soniclib/soniclib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Configure free-running rangefinding and stream distances to the console forever.
 *
 * \param grp  sensor group (already started via ch_group_start())
 * \param dev  the single sensor device
 */
void rangefinder_run(ch_group_t *grp, ch_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* RANGEFINDER_LOOP_H_ */
