/*! \file sensor_offsets.h
 *
 * \brief Mounting offsets between each sensor and the point the "actual" distance is measured
 * from (the base of the unit), in millimetres.
 *
 * The sensor's range is measured between the two transducers. The tape-measured distance is
 * taken from the base of each unit, which sits a little further out, so a corrected distance is
 *
 *     corrected_mm = sensor_range_mm + TRANSMITTER_OFFSET + RECEIVER_OFFSET
 *
 * Usage: add sensor_calibration to the app's main/CMakeLists.txt REQUIRES, then
 * #include "sensor_offsets.h".
 */

#ifndef SENSOR_OFFSETS_H_
#define SENSOR_OFFSETS_H_

#define TRANSMITTER_OFFSET 23.25f /* mm, sender transducer -> base of the sender unit */
#define RECEIVER_OFFSET    23.25f /* mm, receiver transducer -> base of the receiver unit */

#endif /* SENSOR_OFFSETS_H_ */
