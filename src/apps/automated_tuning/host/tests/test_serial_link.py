import protocol
from serial_link import Frame, build_frame, decode_telemetry, parse_frame


def test_call_frame_round_trip():
    raw = build_frame(protocol.MsgType.CALL, seq=5, opcode=protocol.Opcode.HELLO, body=b"")
    assert raw.endswith(b"\x00")
    frame = parse_frame(raw[:-1])
    assert frame == Frame(msg_type=protocol.MsgType.CALL, seq=5, opcode=protocol.Opcode.HELLO, body=b"")


def test_response_frame_with_body_round_trip():
    body = bytes([protocol.Status.OK]) + protocol.RESP_HELLO.pack(protocol.PROTOCOL_VERSION)
    raw = build_frame(protocol.MsgType.RESPONSE, seq=5, opcode=protocol.Opcode.HELLO, body=body)
    frame = parse_frame(raw[:-1])
    assert frame.body == body


def test_telemetry_frame_round_trip():
    tbody = protocol.TELEMETRY.pack(42, 1, 3000 * 32, 1234)
    raw = build_frame(protocol.MsgType.TELEMETRY, 0, protocol.OPCODE_TELEMETRY, tbody)
    frame = parse_frame(raw[:-1])
    telemetry = decode_telemetry(frame.body)
    assert telemetry.meas_seq == 42
    assert telemetry.have_target is True
    assert telemetry.range_mm == 3000.0
    assert telemetry.amplitude == 1234


def test_corrupted_frame_is_rejected():
    raw = bytearray(build_frame(protocol.MsgType.CALL, 1, protocol.Opcode.HELLO, b""))
    raw[-2] ^= 0xFF  # flip a byte inside the COBS-encoded body, before the trailing delimiter
    assert parse_frame(bytes(raw[:-1])) is None


def _demux(raw_stream: bytes):
    """Mirrors SerialLink._reader_loop's byte-at-a-time delimiter-splitting logic, without a
    real port, so the demux behavior is directly testable."""
    frames = []
    buf = bytearray()
    for b in raw_stream:
        if b == 0x00:
            if buf:
                frame = parse_frame(bytes(buf))
                buf.clear()
                if frame is not None:
                    frames.append(frame)
            continue
        buf.append(b)
    return frames


def test_telemetry_interleaved_between_call_and_response_is_correctly_demuxed():
    """The concurrency detail this task discovered: AT_MSG_TELEMETRY streams independently of
    RPC activity and can land on the wire between a CALL and its matching RESPONSE."""
    tbody = protocol.TELEMETRY.pack(1, 0, 0, 0)
    resp_body = bytes([protocol.Status.OK]) + protocol.RESP_HELLO.pack(protocol.PROTOCOL_VERSION)
    stream = build_frame(protocol.MsgType.TELEMETRY, 0, protocol.OPCODE_TELEMETRY, tbody) + build_frame(
        protocol.MsgType.RESPONSE, 7, protocol.Opcode.HELLO, resp_body
    )
    frames = _demux(stream)
    assert len(frames) == 2
    assert frames[0].msg_type == protocol.MsgType.TELEMETRY
    assert frames[1].msg_type == protocol.MsgType.RESPONSE
    assert frames[1].seq == 7


def test_garbage_byte_stream_does_not_prevent_the_next_frame_from_parsing():
    garbage = b"\x01\x02\x03garbage-log-line-that-should-never-be-here"
    good = build_frame(protocol.MsgType.CALL, 9, protocol.Opcode.HELLO, b"")
    frames = _demux(garbage + b"\x00" + good)
    assert len(frames) == 1
    assert frames[0].seq == 9
