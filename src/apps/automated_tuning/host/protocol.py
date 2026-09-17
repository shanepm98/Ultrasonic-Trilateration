"""Wire protocol for the automated sensor tuner - Python mirror of ../include/at_protocol.h.

This is a hand-maintained mirror, not generated code: the host has no C toolchain to consume
the header directly. Keep every struct format string and opcode value in lockstep with
at_protocol.h when either changes. Drift between the two is caught at runtime by the
AT_OP_HELLO version handshake (PROTOCOL_VERSION below), not by any codegen/schema tooling.

All multi-byte fields are little-endian (`<` in every struct format string below), matching
the ESP32's and the host's native byte order.
"""

import struct
from enum import IntEnum

PROTOCOL_VERSION = 1

MAX_SEGMENTS = 8
NUM_THRESHOLDS = 8

CRC16_INIT = 0xFFFF


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), matching at_crc16() in at_protocol.h."""
    crc = CRC16_INIT
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# ===================== Serial message framing (host <-> receiver) =====================
# Frame on the wire = COBS_encode(header || body || crc16(header || body)) + b"\x00"


class MsgType(IntEnum):
    CALL = 0x01
    RESPONSE = 0x02
    TELEMETRY = 0x03


OPCODE_TELEMETRY = 0xFF  # opcode value used in the header for TELEMETRY frames

# at_msg_header_t: msg_type, seq, opcode, len
MSG_HEADER = struct.Struct("<BBBB")


# ===================== Opcodes =====================


class Opcode(IntEnum):
    RESERVED = 0x00
    HELLO = 0x01
    MEAS_RESET = 0x02
    MEAS_INIT = 0x03
    ADD_SEGMENT_TX = 0x04
    ADD_SEGMENT_RX = 0x05
    ADD_SEGMENT_COUNT = 0x06
    MEAS_WRITE_CONFIG = 0x07
    SET_ODR = 0x08
    SET_NUM_SAMPLES = 0x09
    SET_MAX_RANGE_MEAS = 0x0A
    SET_MAX_RANGE = 0x0B
    SET_THRESHOLDS = 0x0C
    GET_THRESHOLDS = 0x0D
    GPT_ALGO_CONFIGURE = 0x0E
    SET_MODE = 0x0F  # must be sent last in a config sequence; triggers the ESP-NOW relay
    GET_STATUS = 0x10


# ===================== AT_MSG_RESPONSE status codes (body byte 0) =====================


class Status(IntEnum):
    OK = 0x00  # mirrors SonicLib's RET_OK, relayed verbatim
    ERR = 0x01  # mirrors SonicLib's RET_ERR, relayed verbatim
    MALFORMED_ARGS = 0xFD
    UNKNOWN_OPCODE = 0xFE


# ===================== Shared threshold structs =====================

# at_thresh_t: start_sample, level
THRESH = struct.Struct("<HH")
# at_thresholds_t: NUM_THRESHOLDS x at_thresh_t
THRESHOLDS = struct.Struct("<" + "HH" * NUM_THRESHOLDS)


def pack_thresholds(pairs) -> bytes:
    """pairs: iterable of exactly NUM_THRESHOLDS (start_sample, level) tuples."""
    pairs = list(pairs)
    assert len(pairs) == NUM_THRESHOLDS
    flat = [v for pair in pairs for v in pair]
    return THRESHOLDS.pack(*flat)


def unpack_thresholds(data: bytes):
    flat = THRESHOLDS.unpack(data)
    return list(zip(flat[0::2], flat[1::2]))


# ===================== Per-opcode AT_MSG_CALL body layouts =====================
# HELLO, MEAS_WRITE_CONFIG, GET_THRESHOLDS, and GET_STATUS have empty call bodies.

# at_call_meas_reset_t: meas_num
CALL_MEAS_RESET = struct.Struct("<B")
# at_call_meas_init_t: meas_num, odr, meas_period, mode
CALL_MEAS_INIT = struct.Struct("<BBHB")
# at_call_add_segment_tx_t: meas_num, num_cycles, pulse_width, phase, int_enable
CALL_ADD_SEGMENT_TX = struct.Struct("<BHBBB")
# at_call_add_segment_rx_t: meas_num, num_samples, gain_reduce, atten, int_enable
CALL_ADD_SEGMENT_RX = struct.Struct("<BHBBB")
# at_call_add_segment_count_t: meas_num, num_cycles, int_enable
CALL_ADD_SEGMENT_COUNT = struct.Struct("<BHB")
# at_call_set_odr_t: meas_num, odr
CALL_SET_ODR = struct.Struct("<BB")
# at_call_set_num_samples_t: meas_num, num_samples
CALL_SET_NUM_SAMPLES = struct.Struct("<BH")
# at_call_set_max_range_meas_t: meas_num, max_range_mm
CALL_SET_MAX_RANGE_MEAS = struct.Struct("<BH")
# at_call_set_max_range_t: max_range_mm
CALL_SET_MAX_RANGE = struct.Struct("<H")
# at_call_set_thresholds_t == at_thresholds_t (see THRESHOLDS above)
# at_call_gpt_algo_configure_t: meas_num, ringdown_cancel_samples, static_filter_samples,
#   iq_output_format, num_ranges, filter_update_interval, thresholds[32B]
CALL_GPT_ALGO_CONFIGURE_HEAD = struct.Struct("<BHHBBB")  # thresholds appended separately
# at_call_set_mode_t: mode
CALL_SET_MODE = struct.Struct("<B")


# ===================== AT_MSG_RESPONSE ret_data payloads =====================

# at_resp_get_thresholds_t == at_thresholds_t (see THRESHOLDS above)
# at_resp_get_status_t: op_freq_hz, num_samples, max_range_mm
RESP_GET_STATUS = struct.Struct("<IHH")
# at_resp_hello_t: version
RESP_HELLO = struct.Struct("<B")


# ===================== AT_MSG_TELEMETRY body =====================

# at_telemetry_t: meas_seq, have_target, range_q5, amplitude
TELEMETRY = struct.Struct("<IBIH")


# ===================== ESP-NOW config snapshot (receiver -> transmitter) =====================
# Python doesn't need to send/parse this - it's a receiver<->transmitter-only struct - but it's
# mirrored here for completeness and so configs/*.json saved by the host can be cross-checked
# against the on-wire size the firmware will produce.


class SegType(IntEnum):
    COUNT = 0
    TX = 1
    RX = 2


# at_segment_t: seg_type, int_enable, p0, p1, p2
SEGMENT = struct.Struct("<BBHBB")

# at_config_snapshot_t fixed portion, before the variable-length segments array:
# proto_version, generation, meas_num, odr, meas_period, meas_mode, num_segments
CONFIG_SNAPSHOT_HEAD = struct.Struct("<BBBBHBB")
# ... segments[MAX_SEGMENTS] (SEGMENT.size * MAX_SEGMENTS bytes) ...
# tail: max_range_mm, algo_ringdown_cancel_samples, algo_static_filter_samples,
#   algo_iq_output_format, algo_num_ranges, algo_filter_update_interval
CONFIG_SNAPSHOT_TAIL = struct.Struct("<HHHBBB")
# ... thresholds (THRESHOLDS.size bytes) ...
# trailing crc16 (2 bytes)

CONFIG_SNAPSHOT_SIZE = (
    CONFIG_SNAPSHOT_HEAD.size
    + SEGMENT.size * MAX_SEGMENTS
    + CONFIG_SNAPSHOT_TAIL.size
    + THRESHOLDS.size
    + 2
)
assert CONFIG_SNAPSHOT_SIZE <= 250, "must fit ESP-NOW's 250-byte payload cap"
