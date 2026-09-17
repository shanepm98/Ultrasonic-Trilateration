/*! \file serial_link.h
 *
 * \brief Owns UART0 and at_protocol.h's COBS/CRC framing. No opcode knowledge beyond
 * at_msg_header_t - rpc_dispatch.c and trigger_loop.c are the only callers, and they own what
 * goes in a body.
 *
 * UART0 is also the ESP-IDF console's UART. Callers MUST call esp_log_level_set("*",
 * ESP_LOG_NONE) before serial_link_init(), and must not log anything (directly or via a
 * component that still logs) after that point - see at_protocol.h's own framing comment for why
 * (COBS resync is the defense-in-depth backstop, not a substitute for this).
 */

#ifndef SERIAL_LINK_H_
#define SERIAL_LINK_H_

#include <stdbool.h>
#include <stdint.h>

#include "at_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Install the UART0 driver for the framed RPC link. Call exactly once, after silencing
 * ESP_LOGx. */
void serial_link_init(void);

/*!
 * \brief Build header+body+crc16, COBS-encode, and write the whole frame (plus trailing 0x00
 * delimiter) to UART0 in one atomic operation (internally mutex-protected), so a telemetry frame
 * and a response frame from different tasks can never interleave mid-frame.
 *
 * \param type      AT_MSG_CALL / AT_MSG_RESPONSE / AT_MSG_TELEMETRY
 * \param seq       echoed seq for AT_MSG_RESPONSE; 0 for AT_MSG_TELEMETRY
 * \param opcode    at_opcode_t for CALL/RESPONSE; AT_OPCODE_TELEMETRY for TELEMETRY
 * \param body      body bytes, or NULL if body_len == 0
 * \param body_len  number of body bytes; caller-supplied buffers must never exceed
 *                  sizeof(at_config_snapshot_t) in practice (the largest struct in
 *                  at_protocol.h), well under a uint8_t's range
 */
void serial_link_send(at_msg_type_t type, uint8_t seq, uint8_t opcode, const void *body, uint8_t body_len);

/*!
 * \brief Block until one well-formed AT_MSG_CALL frame is received, decoded, and CRC-verified.
 *
 * Any framing/CRC error, any non-CALL message type, or a body too large for body_buf_cap is
 * silently dropped internally and the read resumes - COBS's 0x00 delimiter guarantees the next
 * frame is unaffected. This function only returns once it has a valid CALL frame to hand back.
 *
 * \param hdr_out       populated with the decoded header on return
 * \param body_buf      receives the decoded body bytes
 * \param body_buf_cap  capacity of body_buf
 */
void serial_link_recv_call(at_msg_header_t *hdr_out, uint8_t *body_buf, uint8_t body_buf_cap);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_LINK_H_ */
