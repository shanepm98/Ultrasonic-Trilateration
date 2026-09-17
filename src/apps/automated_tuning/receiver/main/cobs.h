/*! \file cobs.h
 *
 * \brief Consistent Overhead Byte Stuffing - standalone codec, no ESP-IDF or protocol
 * dependency (independently testable with a plain host-side compiler).
 *
 * Used by serial_link.c to frame at_protocol.h messages: a 0x00 delimiter always
 * unambiguously marks a frame boundary on the wire, because COBS guarantees the encoded body
 * never contains a literal 0x00. This is what lets the RPC link resynchronize on the very next
 * frame if anything (a stray log line, a dropped byte) ever corrupts the stream, instead of
 * silently misparsing a run of following bytes the way a raw length-prefixed scheme would.
 */

#ifndef COBS_H_
#define COBS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Worst-case encoded size for an n-byte payload (excludes the trailing 0x00 frame delimiter,
 * which the caller appends separately after encoding). One extra overhead byte per 254 input
 * bytes, plus one leading code byte always present - even for n == 0. (n)/254 uses integer
 * division; e.g. n=254 needs 254/254+1=2 overhead bytes (256 total), not the 1 a ceiling-based
 * ((n)+253)/254 formula would give. */
#define COBS_MAX_ENCODED_SIZE(n) ((n) + (n) / 254u + 1u)

/*!
 * \brief COBS-encode src into dst.
 *
 * \param src      input bytes; may contain any byte value, including 0x00
 * \param src_len  number of input bytes
 * \param dst      output buffer; must have capacity >= COBS_MAX_ENCODED_SIZE(src_len)
 * \return number of bytes written to dst (never includes a trailing delimiter)
 */
size_t cobs_encode(const uint8_t *src, size_t src_len, uint8_t *dst);

/*!
 * \brief Decode a COBS-encoded buffer (with its trailing 0x00 delimiter already stripped by the
 * caller) back into dst.
 *
 * \param src      COBS-encoded input bytes (no trailing delimiter)
 * \param src_len  number of input bytes
 * \param dst      output buffer; must have capacity >= src_len
 * \param ok       set to false (and 0 is returned) on any malformed input, rather than
 *                 asserting - a bad frame must be dropped, never crash the caller
 * \return number of bytes written to dst
 */
size_t cobs_decode(const uint8_t *src, size_t src_len, uint8_t *dst, bool *ok);

#ifdef __cplusplus
}
#endif

#endif /* COBS_H_ */
