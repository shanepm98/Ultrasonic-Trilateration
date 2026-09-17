/*! \file at_protocol.h
 *
 * \brief Wire protocol for the automated sensor tuner: binary RPC between the host Python
 * control script and the receiver board over USB serial, and the resulting sensor config
 * snapshot the receiver relays to the transmitter board over ESP-NOW.
 *
 * This header is the single source of truth for both links. It has no ESP-IDF or SonicLib
 * dependency (plain C99 + stdint.h) so it compiles standalone and can be reasoned about without
 * the toolchain; it is mirrored by hand in ../host/protocol.py for the Python side (see that
 * file's header comment). There is no codegen tying the two together - drift is caught at
 * runtime by the AT_OP_HELLO version handshake below, not by tooling.
 *
 * Values here deliberately don't reuse SonicLib's own enum types (ch_odr_t, ch_mode_t, ...):
 * the host has no C toolchain, so every field is a plain fixed-width int and the receiver
 * firmware is responsible for casting to/from the real SonicLib enums when it makes the actual
 * ch_ and icu_gpt_ calls.
 */

#ifndef AT_PROTOCOL_H_
#define AT_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#define AT_PACKED __attribute__((packed))

/* Bump whenever any layout in this file changes. Host sends AT_OP_HELLO first and hard-aborts
 * on a mismatch; the transmitter drops (does not attempt to interpret) any ESP-NOW snapshot
 * whose proto_version doesn't match its own. No negotiation - this is a single-developer
 * bring-up tool, not a public API. */
#define AT_PROTOCOL_VERSION 1u

/* Protocol-level cap on segments per measurement, NOT a SonicLib limit (hardware max is
 * CH_MEAS_MAX_SEGMENTS = 32). Real pitch-catch usage today is 2-3 segments; 8 leaves headroom
 * for tuning experiments while keeping at_config_snapshot_t well under ESP-NOW's 250-byte
 * payload cap (see static_assert below). Raise later if needed - the cap can go as high as 32
 * without the snapshot exceeding the ESP-NOW MTU. */
#define AT_MAX_SEGMENTS 8u

/* Matches SonicLib's CH_NUM_THRESHOLDS. */
#define AT_NUM_THRESHOLDS 8u

/* ===================== Serial message framing (host <-> receiver) =====================
 *
 * Each serial frame is COBS-encoded with a 0x00 delimiter, wrapping:
 *   at_msg_header_t header || body[header.len] || uint16_t crc16
 * where crc16 = at_crc16() over the unencoded header+body. COBS (not raw length-prefixing) is
 * used because the board's only wire back to the host is the same USB-UART the ESP-IDF console
 * uses - if a stray log line or boot banner ever lands on the wire, the 0x00 delimiter always
 * resynchronizes the next frame; a corrupted length field in a length-prefixed scheme would
 * silently misparse the following bytes instead. Firmware should also silence ESP_LOGx on this
 * app once the RPC task starts, as defense in depth.
 *
 * The host RPC model is strictly synchronous: send one AT_MSG_CALL, block for the matching
 * AT_MSG_RESPONSE (same seq + opcode), then send the next. No pipelining. Only AT_MSG_TELEMETRY
 * is asynchronous/continuous.
 */

typedef enum {
    AT_MSG_CALL      = 0x01,
    AT_MSG_RESPONSE  = 0x02,
    AT_MSG_TELEMETRY = 0x03,
} at_msg_type_t;

/* opcode value used in the header for AT_MSG_TELEMETRY frames, since telemetry isn't a
 * call/response pair and has no real opcode. */
#define AT_OPCODE_TELEMETRY 0xFFu

typedef struct AT_PACKED {
    uint8_t msg_type; /* at_msg_type_t */
    uint8_t seq;      /* host-assigned; echoed back in AT_MSG_RESPONSE; unused (0) for TELEMETRY */
    uint8_t opcode;   /* at_opcode_t for CALL/RESPONSE; AT_OPCODE_TELEMETRY for TELEMETRY */
    uint8_t len;      /* length of body immediately following this header (excludes the trailing crc16) */
} at_msg_header_t;

/* ===================== Opcodes (host -> receiver AT_MSG_CALL) ===================== */

typedef enum {
    AT_OP_RESERVED          = 0x00, /* never emitted; guards against a stray zero surviving a framing hiccup */
    AT_OP_HELLO             = 0x01, /* version handshake; empty call body, at_resp_hello_t response */
    AT_OP_MEAS_RESET        = 0x02, /* ch_meas_reset: clears the segment list; must precede rebuilding it */
    AT_OP_MEAS_INIT         = 0x03, /* ch_meas_init (+ internal icu_gpt_algo_init) */
    AT_OP_ADD_SEGMENT_TX    = 0x04, /* ch_meas_add_segment_tx */
    AT_OP_ADD_SEGMENT_RX    = 0x05, /* ch_meas_add_segment_rx */
    AT_OP_ADD_SEGMENT_COUNT = 0x06, /* ch_meas_add_segment_count */
    AT_OP_MEAS_WRITE_CONFIG = 0x07, /* ch_meas_write_config; empty call body */
    AT_OP_SET_ODR           = 0x08, /* ch_meas_set_odr */
    AT_OP_SET_NUM_SAMPLES   = 0x09, /* ch_meas_set_num_samples */
    AT_OP_SET_MAX_RANGE_MEAS = 0x0A, /* ch_meas_set_max_range */
    AT_OP_SET_MAX_RANGE     = 0x0B, /* ch_set_max_range */
    AT_OP_SET_THRESHOLDS    = 0x0C, /* ch_set_thresholds */
    AT_OP_GET_THRESHOLDS    = 0x0D, /* ch_get_thresholds; empty call body, at_thresholds_t response */
    AT_OP_GPT_ALGO_CONFIGURE = 0x0E, /* icu_gpt_algo_configure (+ internal ch_set_algo_config/ch_init_algo) */
    AT_OP_SET_MODE          = 0x0F, /* ch_set_mode; MUST be the last call in a config sequence - also the
                                      * point at which the receiver pushes the ESP-NOW config snapshot to
                                      * the transmitter (see at_config_snapshot_t below) */
    AT_OP_GET_STATUS        = 0x10, /* diagnostic; empty call body, at_resp_get_status_t response */
} at_opcode_t;

/* ===================== AT_MSG_RESPONSE status codes (body byte 0) ===================== */

typedef enum {
    /* 0x00/0x01 mirror SonicLib's own ch_retval (RET_OK/RET_ERR) and are relayed verbatim from
     * whatever the underlying ch_ or icu_gpt_ call returned - no re-encoding. */
    AT_STATUS_OK             = 0x00,
    AT_STATUS_ERR            = 0x01,
    AT_STATUS_MALFORMED_ARGS = 0xFD, /* receiver-generated: header.len didn't match the opcode's expected body size */
    AT_STATUS_UNKNOWN_OPCODE = 0xFE, /* receiver-generated */
} at_status_t;

/* ===================== Shared threshold structs =====================
 * Mirror SonicLib's ch_thresh_t / ch_thresholds_t field-for-field. */

typedef struct AT_PACKED {
    uint16_t start_sample;
    uint16_t level;
} at_thresh_t;

typedef struct AT_PACKED {
    at_thresh_t threshold[AT_NUM_THRESHOLDS];
} at_thresholds_t;

/* ===================== Per-opcode AT_MSG_CALL body layouts =====================
 * AT_OP_HELLO, AT_OP_MEAS_WRITE_CONFIG, AT_OP_GET_THRESHOLDS, and AT_OP_GET_STATUS have empty
 * (zero-length) call bodies and so have no struct here. */

typedef struct AT_PACKED {
    uint8_t meas_num;
} at_call_meas_reset_t;

typedef struct AT_PACKED {
    uint8_t  meas_num;
    uint8_t  odr;
    uint16_t meas_period;
    uint8_t  mode;
} at_call_meas_init_t;

typedef struct AT_PACKED {
    uint8_t  meas_num;
    uint16_t num_cycles;
    uint8_t  pulse_width;
    uint8_t  phase;
    uint8_t  int_enable;
} at_call_add_segment_tx_t;

typedef struct AT_PACKED {
    uint8_t  meas_num;
    uint16_t num_samples;
    uint8_t  gain_reduce;
    uint8_t  atten;
    uint8_t  int_enable;
} at_call_add_segment_rx_t;

typedef struct AT_PACKED {
    uint8_t  meas_num;
    uint16_t num_cycles;
    uint8_t  int_enable;
} at_call_add_segment_count_t;

typedef struct AT_PACKED {
    uint8_t meas_num;
    uint8_t odr;
} at_call_set_odr_t;

typedef struct AT_PACKED {
    uint8_t  meas_num;
    uint16_t num_samples;
} at_call_set_num_samples_t;

typedef struct AT_PACKED {
    uint8_t  meas_num;
    uint16_t max_range_mm;
} at_call_set_max_range_meas_t;

typedef struct AT_PACKED {
    uint16_t max_range_mm;
} at_call_set_max_range_t;

/* AT_OP_SET_THRESHOLDS call body is exactly an at_thresholds_t. */
typedef at_thresholds_t at_call_set_thresholds_t;

typedef struct AT_PACKED {
    uint8_t         meas_num;
    uint16_t        ringdown_cancel_samples;
    uint16_t        static_filter_samples;
    uint8_t         iq_output_format;
    uint8_t         num_ranges;
    uint8_t         filter_update_interval;
    at_thresholds_t thresholds;
} at_call_gpt_algo_configure_t;

typedef struct AT_PACKED {
    uint8_t mode;
} at_call_set_mode_t;

/* ===================== AT_MSG_RESPONSE ret_data payloads =====================
 * Only opcodes that return data beyond the status byte have a struct here. */

/* AT_OP_GET_THRESHOLDS response ret_data is exactly an at_thresholds_t. */
typedef at_thresholds_t at_resp_get_thresholds_t;

typedef struct AT_PACKED {
    uint32_t op_freq_hz;
    uint16_t num_samples;
    uint16_t max_range_mm;
} at_resp_get_status_t;

typedef struct AT_PACKED {
    uint8_t version; /* AT_PROTOCOL_VERSION, as compiled into the receiver firmware */
} at_resp_hello_t;

/* ===================== AT_MSG_TELEMETRY body (receiver -> host, continuous) =====================
 * Modeled on the existing rf_sample_t/rx_sample_t pattern already used in
 * echo_mode_hardware_test and pitch_catch_mode_hardware_test. */

typedef struct AT_PACKED {
    uint32_t meas_seq;     /* receiver's free-running measurement counter (distinct from the RPC seq byte) */
    uint8_t  have_target;
    uint32_t range_q5;     /* ch_get_range() units, mm x 32; 0 if have_target == 0 */
    uint16_t amplitude;    /* 0 if have_target == 0 */
} at_telemetry_t;

/* ===================== ESP-NOW config snapshot (receiver -> transmitter) =====================
 *
 * Pushed as a single complete snapshot once per tuning iteration, not as a 1:1 relay of each
 * AT_MSG_CALL - ESP-NOW gives no delivery/ordering guarantee, and the segment list is built by
 * append calls, so a dropped mid-sequence relay would permanently desync the transmitter's
 * segment list with no way to detect it. A full snapshot is self-correcting: a dropped packet
 * just means the transmitter keeps running last iteration's config one cycle longer.
 *
 * The push fires when the receiver handles AT_OP_SET_MODE, not AT_OP_MEAS_WRITE_CONFIG: in the
 * real call order (see pitch_catch_mode_hardware_test/receiver/main/receiver_loop.c),
 * max_range and the algo config are applied *after* write_config, and ch_set_mode is SonicLib's
 * own documented last-call-arms-sensing point.
 *
 * The sensing mode itself (ch_set_mode()'s argument) is deliberately NOT part of this snapshot:
 * the transmitter and receiver always run structurally different modes (TRIGGERED_TX_RX vs
 * TRIGGERED_RX_ONLY), so each side keeps that as a local hardcoded constant instead of syncing
 * it, exactly as today in sender_loop.c / receiver_loop.c.
 */

typedef enum {
    AT_SEG_COUNT = 0, /* matches SonicLib's ch_meas_seg_type_t CH_MEAS_SEG_TYPE_COUNT */
    AT_SEG_TX    = 1, /* CH_MEAS_SEG_TYPE_TX */
    AT_SEG_RX    = 2, /* CH_MEAS_SEG_TYPE_RX */
} at_seg_type_t;

typedef struct AT_PACKED {
    uint8_t  seg_type;    /* at_seg_type_t */
    uint8_t  int_enable;
    uint16_t p0;          /* num_cycles (COUNT/TX) or num_samples (RX) */
    uint8_t  p1;          /* pulse_width (TX) / gain_reduce (RX) / unused (COUNT) */
    uint8_t  p2;          /* phase (TX) / atten (RX) / unused (COUNT) */
} at_segment_t;

typedef struct AT_PACKED {
    uint8_t         proto_version;
    uint8_t         generation; /* host-incremented per rebuild; log-only staleness hint, not required for correctness */
    uint8_t         meas_num;
    uint8_t         odr;
    uint16_t        meas_period;
    uint8_t         meas_mode; /* ch_meas_mode_t (active/standby) - not the sensing mode, see note above */
    uint8_t         num_segments;
    at_segment_t    segments[AT_MAX_SEGMENTS];
    uint16_t        max_range_mm;
    uint16_t        algo_ringdown_cancel_samples;
    uint16_t        algo_static_filter_samples;
    uint8_t         algo_iq_output_format;
    uint8_t         algo_num_ranges;
    uint8_t         algo_filter_update_interval;
    at_thresholds_t thresholds;
    uint16_t        crc16; /* over every preceding field */
} at_config_snapshot_t;

_Static_assert(sizeof(at_config_snapshot_t) <= 250,
               "at_config_snapshot_t must fit ESP-NOW's 250-byte payload cap");

/* ===================== CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) =====================
 * Computed over the unencoded header+body before COBS encoding (serial link), and over every
 * at_config_snapshot_t field preceding crc16 (ESP-NOW link). Chosen as a standard, easily
 * hand-implemented algorithm identically reproducible in C and Python with no new dependency.
 */

#define AT_CRC16_INIT 0xFFFFu

static inline uint16_t at_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = AT_CRC16_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

#endif /* AT_PROTOCOL_H_ */
