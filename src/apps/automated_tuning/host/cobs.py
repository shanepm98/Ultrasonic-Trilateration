"""Consistent Overhead Byte Stuffing - Python port of ../receiver/main/cobs.c.

Used by serial_link.py to frame protocol.py messages exactly like the firmware's serial_link.c:
a 0x00 delimiter always unambiguously marks a frame boundary, because COBS guarantees the
encoded body never contains a literal 0x00. Keep this in lockstep with cobs.c - there's no
codegen tying the two together, only this comment and the one in cobs.c pointing back.
"""


def cobs_encode(data: bytes) -> bytes:
    """Encode data (may contain any byte value, including 0x00). Does not append the trailing
    frame delimiter - the caller does that separately, matching cobs_encode() in cobs.c."""
    out = bytearray()
    out.append(0)  # placeholder for the first code byte, patched at the end of each block
    code_idx = 0
    code = 1

    for byte in data:
        if byte == 0:
            out[code_idx] = code
            code = 1
            code_idx = len(out)
            out.append(0)  # placeholder for the next block's code byte
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:
                out[code_idx] = code
                code = 1
                code_idx = len(out)
                out.append(0)

    out[code_idx] = code
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    """Decode a COBS-encoded buffer (trailing 0x00 delimiter already stripped by the caller).

    Raises ValueError on malformed input (a zero code byte, or a code claiming more data than
    remains) - callers (serial_link.py) must catch this and drop the frame, exactly like the
    firmware's cobs_decode() returning ok=false rather than crashing.
    """
    out = bytearray()
    read_idx = 0
    n = len(data)

    while read_idx < n:
        code = data[read_idx]
        if code == 0 or read_idx + code > n:
            raise ValueError("malformed COBS data: invalid or truncated code byte")
        read_idx += 1

        out.extend(data[read_idx : read_idx + code - 1])
        read_idx += code - 1

        if code != 0xFF and read_idx != n:
            out.append(0)

    return bytes(out)
