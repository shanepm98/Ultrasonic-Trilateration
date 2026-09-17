import pytest

from cobs import cobs_decode, cobs_encode

CASES = [
    b"",
    b"\x00",
    bytes([1, 2, 3, 4, 5]),
    bytes([0] * 10),
    bytes([1, 0, 2, 3, 0, 0, 4, 0]),
    bytes(range(1, 255)),  # exactly 254 non-zero bytes - exercises the 0xFF code-byte case
    bytes([(i % 255) + 1 for i in range(255)]),  # spans the 0xFF boundary into a 2nd block
    bytes([0 if i % 7 == 0 else i % 256 for i in range(300)]),
]


@pytest.mark.parametrize("data", CASES)
def test_round_trip(data):
    encoded = cobs_encode(data)
    assert 0 not in encoded
    assert cobs_decode(encoded) == data


def test_encoded_length_matches_reference_c_implementation():
    # Cross-checked against receiver/main/cobs.c's own ASan-verified test output.
    assert len(cobs_encode(bytes(range(1, 255)))) == 256
    assert len(cobs_encode(bytes([(i % 255) + 1 for i in range(255)]))) == 257


def test_decode_rejects_truncated_code():
    with pytest.raises(ValueError):
        cobs_decode(bytes([5, 1, 2]))


def test_decode_rejects_embedded_zero_code():
    with pytest.raises(ValueError):
        cobs_decode(bytes([2, 0xAA, 0]))
