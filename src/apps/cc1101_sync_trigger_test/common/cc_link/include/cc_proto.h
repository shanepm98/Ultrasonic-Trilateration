/*! \file cc_proto.h
 *
 * \brief ESP-NOW side-channel message format. All messages are short ASCII strings (no NUL on the
 * air) broadcast to FF:FF:FF:FF:FF:FF:
 *
 *   "RR:VV"         transmitter -> receivers: write hex VALUE VV to CC1101 hex REGISTER RR
 *                   (0x00-0x2E, 0x3E = PATABLE[0] "0" level, 0x3F = PATABLE[1] "1" level). A "0x" prefix on either
 *                   field is accepted.
 *   "DUMP"          transmitter -> receivers: reply with your register dump.
 *   "REGS:<hex>"    receiver -> transmitter: the CC_ASK_DUMP_LEN-byte register dump (config
 *                   0x00-0x2E, then status 0x30-0x3D) as 2 * CC_ASK_DUMP_LEN hex digits.
 */

#ifndef CC_PROTO_H_
#define CC_PROTO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cc_ask.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC_PROTO_DUMP_CMD    "DUMP"
#define CC_PROTO_REGS_PREFIX "REGS:"
#define CC_PROTO_REGS_LEN    (sizeof(CC_PROTO_REGS_PREFIX) - 1 + 2 * CC_ASK_DUMP_LEN) /* chars, no NUL */

/*! \brief Format a register write as "RR:VV" into \a out (at least 6 bytes). Returns the length. */
size_t cc_proto_format_write(char *out, uint8_t reg, uint8_t value);

/*! \brief Parse "RR:VV" (NUL-terminated). True on success, with the register in range. */
bool cc_proto_parse_write(const char *msg, uint8_t *reg, uint8_t *value);

/*! \brief True if \a msg (NUL-terminated) is the dump request. */
bool cc_proto_is_dump(const char *msg);

/*! \brief Format a "REGS:" reply into \a out (at least CC_PROTO_REGS_LEN + 1 bytes). Returns the length. */
size_t cc_proto_format_regs(char *out, const uint8_t dump[CC_ASK_DUMP_LEN]);

/*! \brief Parse a "REGS:" reply (NUL-terminated) into \a dump. True on success. */
bool cc_proto_parse_regs(const char *msg, uint8_t dump[CC_ASK_DUMP_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* CC_PROTO_H_ */
