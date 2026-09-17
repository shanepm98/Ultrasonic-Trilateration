"""USB serial transport for the automated-tuning RPC link - Python counterpart of
../receiver/main/serial_link.c.

One detail the algorithm design (tuning_algorithm.md) doesn't need to cover but this transport
must: AT_MSG_TELEMETRY streams continuously from the receiver's trigger loop, independently of
RPC activity. The firmware's serial_link.c only guarantees a telemetry frame and a response frame
never interleave *mid-frame* (via its TX mutex) - it does not guarantee ordering. A telemetry
frame can legitimately land on the wire between a host CALL and its matching RESPONSE. The
background reader below demuxes by msg_type into two separate queues so call() only ever waits
on responses, never mis-consumes a telemetry frame as if it were one.
"""

from __future__ import annotations

import queue
import threading
from dataclasses import dataclass
from typing import Optional, Tuple

import serial

import protocol
from cobs import cobs_decode, cobs_encode


@dataclass
class Frame:
    msg_type: int
    seq: int
    opcode: int
    body: bytes


@dataclass
class TelemetryFrame:
    meas_seq: int
    have_target: bool
    range_q5: int
    amplitude: int

    @property
    def range_mm(self) -> float:
        return self.range_q5 / 32.0


class ProtocolError(Exception):
    """Raised on a malformed/mismatched response - never on a routine timeout (see TimeoutError)."""


def build_frame(msg_type: int, seq: int, opcode: int, body: bytes = b"") -> bytes:
    """Build one on-wire frame (COBS-encoded, with the trailing 0x00 delimiter) ready to write."""
    header = protocol.MSG_HEADER.pack(msg_type, seq, opcode, len(body))
    plain = header + body
    crc = protocol.crc16(plain)
    plain += bytes((crc & 0xFF, (crc >> 8) & 0xFF))
    return cobs_encode(plain) + b"\x00"


def parse_frame(raw: bytes) -> Optional[Frame]:
    """Decode one COBS-encoded frame (trailing 0x00 delimiter already stripped by the caller).

    Returns None on any malformed input (bad COBS, length mismatch, bad CRC) rather than
    raising - a bad frame must be dropped and reading resumed, exactly like the firmware's
    serial_link_recv_call()'s own "drop and resync on the next 0x00" policy.
    """
    try:
        plain = cobs_decode(raw)
    except ValueError:
        return None

    if len(plain) < protocol.MSG_HEADER.size + 2:
        return None

    msg_type, seq, opcode, length = protocol.MSG_HEADER.unpack_from(plain, 0)
    if len(plain) != protocol.MSG_HEADER.size + length + 2:
        return None

    body = plain[protocol.MSG_HEADER.size : protocol.MSG_HEADER.size + length]
    expected_crc = protocol.crc16(plain[: protocol.MSG_HEADER.size + length])
    crc_offset = protocol.MSG_HEADER.size + length
    got_crc = plain[crc_offset] | (plain[crc_offset + 1] << 8)
    if expected_crc != got_crc:
        return None

    return Frame(msg_type=msg_type, seq=seq, opcode=opcode, body=body)


def decode_telemetry(body: bytes) -> TelemetryFrame:
    meas_seq, have_target, range_q5, amplitude = protocol.TELEMETRY.unpack(body)
    return TelemetryFrame(
        meas_seq=meas_seq, have_target=bool(have_target), range_q5=range_q5, amplitude=amplitude
    )


class SerialLink:
    """Owns one USB-serial port. A background thread reads and demuxes frames; call() is the
    only way to send - it's synchronous (one outstanding RPC at a time), matching the protocol's
    own strictly-synchronous design."""

    def __init__(self, port: str, baud: int = 115200, read_timeout: float = 1.0):
        self._ser = serial.Serial(port, baudrate=baud, timeout=read_timeout)
        self._telemetry_queue: "queue.Queue[TelemetryFrame]" = queue.Queue()
        self._response_queue: "queue.Queue[Frame]" = queue.Queue()
        self._seq = 0
        self._call_lock = threading.Lock()
        self._stop = threading.Event()
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()

    def close(self) -> None:
        self._stop.set()
        self._reader_thread.join(timeout=2.0)
        self._ser.close()

    def __enter__(self) -> "SerialLink":
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    def _reader_loop(self) -> None:
        buf = bytearray()
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(1)
            except serial.SerialException:
                break
            if not chunk:
                continue  # read timeout with no data; keep polling until stopped
            byte = chunk[0]
            if byte == 0x00:
                if buf:
                    frame = parse_frame(bytes(buf))
                    buf.clear()
                    if frame is not None:
                        self._dispatch(frame)
                continue
            buf.append(byte)

    def _dispatch(self, frame: Frame) -> None:
        if frame.msg_type == protocol.MsgType.TELEMETRY:
            self._telemetry_queue.put(decode_telemetry(frame.body))
        elif frame.msg_type == protocol.MsgType.RESPONSE:
            self._response_queue.put(frame)
        # Anything else (e.g. a stray CALL echoed back) is silently dropped - this link only
        # ever expects TELEMETRY/RESPONSE frames from the receiver.

    def call(
        self, opcode: "protocol.Opcode", body: bytes = b"", timeout: float = 1.0
    ) -> Tuple["protocol.Status", bytes]:
        """Send one CALL and block for its matching RESPONSE.

        Raises TimeoutError if no response arrives in time, or ProtocolError if a response
        arrives but its seq/opcode doesn't match what was sent (defends against the rare race
        where a late response from a previously-timed-out call arrives just as a new call's
        response is expected).
        """
        with self._call_lock:
            seq = self._seq
            self._seq = (self._seq + 1) % 256

            # Discard any stale response left over from a previous timed-out call.
            while True:
                try:
                    self._response_queue.get_nowait()
                except queue.Empty:
                    break

            self._ser.write(build_frame(protocol.MsgType.CALL, seq, int(opcode), body))

            try:
                resp = self._response_queue.get(timeout=timeout)
            except queue.Empty:
                raise TimeoutError(
                    f"no response to {opcode.name} (seq={seq}) within {timeout}s"
                ) from None

            if resp.seq != seq or resp.opcode != int(opcode):
                raise ProtocolError(
                    f"response mismatch: sent seq={seq} opcode={opcode}, "
                    f"got seq={resp.seq} opcode={resp.opcode}"
                )

            status = protocol.Status(resp.body[0])
            ret_data = resp.body[1:]
            return status, ret_data

    def hello(self) -> int:
        """AT_OP_HELLO handshake. Raises ProtocolError on any status/version mismatch - this
        protocol hard-aborts rather than negotiating, per at_protocol.h's documented policy."""
        status, ret_data = self.call(protocol.Opcode.HELLO)
        if status != protocol.Status.OK:
            raise ProtocolError(f"HELLO failed with status {status!r}")
        (version,) = protocol.RESP_HELLO.unpack(ret_data)
        if version != protocol.PROTOCOL_VERSION:
            raise ProtocolError(
                f"protocol version mismatch: host={protocol.PROTOCOL_VERSION}, receiver={version}"
            )
        return version

    def read_telemetry(self, timeout: float = 1.0) -> Optional[TelemetryFrame]:
        try:
            return self._telemetry_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def drain_telemetry(self) -> None:
        """Discard any buffered telemetry - used for the trial protocol's warm-up discard
        (tuning_algorithm.md §1) after a reconfiguration."""
        while True:
            try:
                self._telemetry_queue.get_nowait()
            except queue.Empty:
                break
