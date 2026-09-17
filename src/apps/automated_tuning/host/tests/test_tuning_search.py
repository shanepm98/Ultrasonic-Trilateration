import protocol
import trial
import tuning_search as ts
from serial_link import TelemetryFrame


def test_seed_config_keeps_count_and_tx_cycles_coupled():
    cfg = ts.seed_config(op_freq_hz=175000, true_distance_mm=3000)
    assert cfg.receiver_segments[0]["num_cycles"] == cfg.tx_num_cycles()


def test_sample_index_scales_linearly_with_distance():
    idx_1m = ts.estimate_target_sample_index(1000, 175000, ts.Odr.DIV_8)
    idx_3m = ts.estimate_target_sample_index(3000, 175000, ts.Odr.DIV_8)
    assert abs(idx_3m - 3 * idx_1m) <= 1  # exact up to independent rounding at each call


def test_sample_index_has_no_round_trip_halving():
    # Doubling the ODR divisor (DIV_16 vs DIV_8) should roughly halve the index - if a round-trip
    # /2 were incorrectly baked in here (as SonicLib's own ch_mm_to_samples() has for
    # CH_RANGE_ECHO_ONE_WAY), this relationship would still hold, so this test alone can't catch
    # that mistake; it only guards the divisor arithmetic. See ch_common.c's
    # ch_common_range_lsb_to_mm(), verified during design: the halving is gated exclusively on
    # CH_RANGE_ECHO_ONE_WAY and never applied for CH_RANGE_DIRECT (this pitch-catch setup).
    idx_div8 = ts.estimate_target_sample_index(3000, 175000, ts.Odr.DIV_8)
    idx_div16 = ts.estimate_target_sample_index(3000, 175000, ts.Odr.DIV_16)
    assert abs(idx_div16 - idx_div8 / 2) <= 1


def test_correct_target_sample_index_uses_observed_ratio():
    corrected = ts.correct_target_sample_index(200, true_distance_mm=3000, observed_range_mm=3300)
    assert corrected == round(200 * 3000 / 3300)


def test_correct_target_sample_index_handles_zero_observed_range():
    assert ts.correct_target_sample_index(200, true_distance_mm=3000, observed_range_mm=0) == 200


class _FakeLink:
    """Records every call() without touching real hardware - used to verify apply_config's
    opcode sequence and LOCAL/REMOTE targeting, which is exactly the bug fixed in
    at_sensor_config.c (see the module docstring in tuning_search.py)."""

    def __init__(self):
        self.calls = []

    def call(self, opcode, body=b""):
        self.calls.append((opcode, body))
        return protocol.Status.OK, b""


_SEGMENT_OPCODES = {
    protocol.Opcode.ADD_SEGMENT_COUNT,
    protocol.Opcode.ADD_SEGMENT_RX,
    protocol.Opcode.ADD_SEGMENT_TX,
}

_SEGMENT_STRUCTS = {
    protocol.Opcode.ADD_SEGMENT_TX: protocol.CALL_ADD_SEGMENT_TX,
    protocol.Opcode.ADD_SEGMENT_RX: protocol.CALL_ADD_SEGMENT_RX,
    protocol.Opcode.ADD_SEGMENT_COUNT: protocol.CALL_ADD_SEGMENT_COUNT,
}


def test_apply_config_sends_expected_opcode_sequence():
    link = _FakeLink()
    ts.apply_config(link, ts.seed_config(op_freq_hz=175000, true_distance_mm=3000))

    opcodes = [c[0] for c in link.calls]
    assert opcodes == [
        protocol.Opcode.MEAS_RESET,
        protocol.Opcode.MEAS_INIT,
        protocol.Opcode.ADD_SEGMENT_COUNT,
        protocol.Opcode.ADD_SEGMENT_RX,
        protocol.Opcode.ADD_SEGMENT_TX,
        protocol.Opcode.ADD_SEGMENT_COUNT,
        protocol.Opcode.ADD_SEGMENT_RX,
        protocol.Opcode.MEAS_WRITE_CONFIG,
        protocol.Opcode.GPT_ALGO_CONFIGURE,
        protocol.Opcode.SET_MAX_RANGE,
        protocol.Opcode.SET_MODE,
    ]


def test_apply_config_targets_receiver_segments_local_and_transmitter_segments_remote():
    link = _FakeLink()
    ts.apply_config(link, ts.seed_config(op_freq_hz=175000, true_distance_mm=3000))

    targets = [
        _SEGMENT_STRUCTS[opcode].unpack(body)[1]
        for opcode, body in link.calls
        if opcode in _SEGMENT_OPCODES
    ]
    assert targets == [
        protocol.SegTarget.LOCAL,  # receiver's COUNT
        protocol.SegTarget.LOCAL,  # receiver's RX
        protocol.SegTarget.REMOTE,  # transmitter's TX
        protocol.SegTarget.REMOTE,  # transmitter's settle COUNT
        protocol.SegTarget.REMOTE,  # transmitter's RX
    ]


def test_config_json_round_trip_preserves_cycle_coupling_and_version(tmp_path):
    cfg = ts.seed_config(op_freq_hz=175000, true_distance_mm=3000)
    frames = [TelemetryFrame(meas_seq=i, have_target=True, range_q5=3000 * 32, amplitude=3000) for i in range(30)]
    result = trial.compute_trial_result(frames, 3000)
    passed, warnings = trial.passes_gate(result, 3000)

    path = tmp_path / "3meter.config"
    ts.save_config_json(
        path,
        cfg,
        result,
        passed,
        warnings,
        true_distance_mm=3000,
        speed_of_sound_mps=343.0,
        expected_target_sample_index=191,
        expected_target_sample_index_source="formula",
        total_trials_run=1,
        total_search_wall_clock_s=1.0,
    )
    doc = ts.load_config_json(path)
    assert doc["receiver_segments"][0]["num_cycles"] == doc["transmitter_segments"][0]["num_cycles"]
    assert doc["protocol_version"] == protocol.PROTOCOL_VERSION
