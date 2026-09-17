"""Automatic tuning search - implements tuning_algorithm.md's staged, coordinate-wise search.

Builds on serial_link.SerialLink (transport) and trial.py (statistics/scoring). The receiver's
own local sensor and the transmitter's ESP-NOW-relayed sensor are independent queues (see the
target-field design note next to at_call_add_segment_tx_t in at_protocol.h): the receiver is
always RX-only (COUNT + RX_near + RX_far, per Stage 4's two-segment layout), the transmitter is
always TX + COUNT(settle) + RX (per AN-000175's requirement that CH_MODE_TRIGGERED_TX_RX still
listens for its own echo), matching pitch_catch_mode_hardware_test's reference structure.
"""

from __future__ import annotations

import json
import math
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import IntEnum
from pathlib import Path
from typing import List, Optional, Tuple

import protocol
import trial
from serial_link import SerialLink

# SonicLib's ch_odr_t values (soniclib.h) - not part of at_protocol.h's wire format (odr is a
# plain uint8_t on the wire), but the host needs the real enum values both to send them and to
# compute the sample-index formula's divisor (tuning_algorithm.md §4a).
class Odr(IntEnum):
    DIV_32 = 2
    DIV_16 = 3
    DIV_8 = 4  # default
    DIV_4 = 5
    DIV_2 = 6


CH_MEAS_MODE_ACTIVE = 0
CH_MODE_TRIGGERED_RX_ONLY = 0x20  # the receiver's own sensing mode - always this in this app

# ICU-x0201 max active sample count (Table 3, AN-000175 / ICU_MAX_NUM_SAMPLES in SonicLib) -
# hardcoded here since the host has no C toolchain to pull the real macro from.
ICU_MAX_NUM_SAMPLES = 340

SPEED_OF_SOUND_MPS_DEFAULT = 343.0


# ===================== Config model =====================


@dataclass
class SensorConfig:
    meas_num: int = 0
    odr: int = Odr.DIV_8
    meas_period: int = 0
    sensing_mode: int = CH_MODE_TRIGGERED_RX_ONLY

    # Receiver's own local queue: COUNT (matched to the transmitter's TX cycles) -> RX_near
    # (attenuated, first K samples) -> RX_far (normal gain, remaining samples). Each entry is a
    # dict: {"type": "count"|"rx"|"tx", ...fields..., "int_enable": 0/1}.
    receiver_segments: List[dict] = field(default_factory=list)
    # Transmitter's relayed queue: TX -> COUNT (post-TX settle) -> RX (single segment - the
    # transmitter's own detection results are never read, per sender_loop.c, so it doesn't need
    # the receiver's near/far split).
    transmitter_segments: List[dict] = field(default_factory=list)

    max_range_mm: int = 5000
    ringdown_cancel_samples: int = 20
    static_filter_samples: int = 0
    iq_output_format: int = 0
    num_ranges: int = 1
    filter_update_interval: int = 0
    thresholds: List[Tuple[int, int]] = field(
        default_factory=lambda: [(0, 0)] * protocol.NUM_THRESHOLDS
    )

    def tx_num_cycles(self) -> int:
        for seg in self.transmitter_segments:
            if seg["type"] == "tx":
                return seg["num_cycles"]
        raise ValueError("no tx segment in transmitter_segments")


def _us_to_cycles(microseconds: float, op_freq_hz: int) -> int:
    """Approximates SonicLib's ch_usec_to_cycles() - exact device-side rounding isn't available
    host-side, but this is a settle/burst duration, not something requiring bit-exact timing."""
    return round(microseconds * op_freq_hz / 1_000_000.0)


def seed_config(op_freq_hz: int, true_distance_mm: float, speed_of_sound_mps: float = SPEED_OF_SOUND_MPS_DEFAULT) -> SensorConfig:
    """tuning_algorithm.md §3 seed values, from pitch_catch_mode_hardware_test's own
    never-bench-validated bring-up defaults. Single RX segment on both boards at this point -
    Stage 4 is what introduces the receiver's near/far split, not the seed."""
    tx_pulse_us = 80.0
    tx_cycles = _us_to_cycles(tx_pulse_us, op_freq_hz)
    settle_cycles = _us_to_cycles(100.0, op_freq_hz)

    return SensorConfig(
        odr=Odr.DIV_8,
        receiver_segments=[
            {"type": "count", "num_cycles": tx_cycles, "int_enable": 0},
            {"type": "rx", "num_samples": ICU_MAX_NUM_SAMPLES, "gain_reduce": 0, "atten": 0, "int_enable": 1},
        ],
        transmitter_segments=[
            {"type": "tx", "num_cycles": tx_cycles, "pulse_width": 3, "phase": 8, "int_enable": 0},
            {"type": "count", "num_cycles": settle_cycles, "int_enable": 0},
            {"type": "rx", "num_samples": ICU_MAX_NUM_SAMPLES, "gain_reduce": 0, "atten": 0, "int_enable": 1},
        ],
        max_range_mm=5000,
        ringdown_cancel_samples=20,
        static_filter_samples=0,
        thresholds=[(0, 2500), (30, 1200), (60, 700), (120, 450), (200, 350)]
        + [(0, 0)] * (protocol.NUM_THRESHOLDS - 5),
    )


# ===================== Applying a config over RPC =====================


def _check(result: Tuple["protocol.Status", bytes]) -> bytes:
    status, ret_data = result
    if status != protocol.Status.OK:
        raise RuntimeError(f"RPC call failed with status {status!r}")
    return ret_data


def _add_segment(link: SerialLink, meas_num: int, seg: dict, target: protocol.SegTarget) -> None:
    seg_type = seg["type"]
    int_enable = seg.get("int_enable", 0)
    if seg_type == "count":
        body = protocol.CALL_ADD_SEGMENT_COUNT.pack(meas_num, target, seg["num_cycles"], int_enable)
        _check(link.call(protocol.Opcode.ADD_SEGMENT_COUNT, body))
    elif seg_type == "rx":
        body = protocol.CALL_ADD_SEGMENT_RX.pack(
            meas_num, target, seg["num_samples"], seg["gain_reduce"], seg["atten"], int_enable
        )
        _check(link.call(protocol.Opcode.ADD_SEGMENT_RX, body))
    elif seg_type == "tx":
        body = protocol.CALL_ADD_SEGMENT_TX.pack(
            meas_num, target, seg["num_cycles"], seg["pulse_width"], seg["phase"], int_enable
        )
        _check(link.call(protocol.Opcode.ADD_SEGMENT_TX, body))
    else:
        raise ValueError(f"unknown segment type: {seg_type!r}")


def apply_config(link: SerialLink, config: SensorConfig) -> None:
    """Replay a SensorConfig as the exact opcode sequence the firmware expects: MEAS_RESET ->
    MEAS_INIT -> ADD_SEGMENT_* (receiver segments as LOCAL, transmitter segments as REMOTE) ->
    MEAS_WRITE_CONFIG -> GPT_ALGO_CONFIGURE (thresholds bundled in, so there's no ambiguity about
    whether a separate SET_THRESHOLDS call "won" - see at_sensor_config.c's staged_thresholds
    last-call-wins note) -> SET_MAX_RANGE -> SET_MODE (last - also triggers the ESP-NOW snapshot
    push). Order matches the real call order in pitch_catch_mode_hardware_test's receiver_loop.c.
    """
    _check(link.call(protocol.Opcode.MEAS_RESET, protocol.CALL_MEAS_RESET.pack(config.meas_num)))
    _check(
        link.call(
            protocol.Opcode.MEAS_INIT,
            protocol.CALL_MEAS_INIT.pack(config.meas_num, config.odr, config.meas_period, CH_MEAS_MODE_ACTIVE),
        )
    )

    for seg in config.receiver_segments:
        _add_segment(link, config.meas_num, seg, protocol.SegTarget.LOCAL)
    for seg in config.transmitter_segments:
        _add_segment(link, config.meas_num, seg, protocol.SegTarget.REMOTE)

    _check(link.call(protocol.Opcode.MEAS_WRITE_CONFIG))

    algo_body = protocol.CALL_GPT_ALGO_CONFIGURE_HEAD.pack(
        config.meas_num,
        config.ringdown_cancel_samples,
        config.static_filter_samples,
        config.iq_output_format,
        config.num_ranges,
        config.filter_update_interval,
    ) + protocol.pack_thresholds(config.thresholds)
    _check(link.call(protocol.Opcode.GPT_ALGO_CONFIGURE, algo_body))

    _check(link.call(protocol.Opcode.SET_MAX_RANGE, protocol.CALL_SET_MAX_RANGE.pack(config.max_range_mm)))
    _check(link.call(protocol.Opcode.SET_MODE, protocol.CALL_SET_MODE.pack(config.sensing_mode)))


def get_status(link: SerialLink) -> Tuple[int, int, int]:
    """Returns (op_freq_hz, num_samples, max_range_mm) via AT_OP_GET_STATUS."""
    ret_data = _check(link.call(protocol.Opcode.GET_STATUS))
    return protocol.RESP_GET_STATUS.unpack(ret_data)


# ===================== Trial collection =====================


def run_trial(
    link: SerialLink,
    n_frames: int,
    true_distance_mm: float,
    warmup: int = 3,
    per_frame_timeout: float = 1.0,
) -> "trial.TrialResult":
    """tuning_algorithm.md §1: discard `warmup` frames after a reconfiguration, then collect
    `n_frames` telemetry frames and reduce them to a TrialResult. Counts successfully-received
    telemetry frames, not raw trigger attempts - a receiver-side full response timeout (no
    data-ready within AT_RESPONSE_TIMEOUT_MS) means that cycle's frame is never sent at all, so
    it's implicitly excluded rather than explicitly modeled as a "miss"."""
    link.drain_telemetry()
    for _ in range(warmup):
        link.read_telemetry(timeout=per_frame_timeout)

    frames = []
    max_attempts = n_frames * 3 + warmup  # slack for occasional skipped trigger cycles
    attempts = 0
    while len(frames) < n_frames and attempts < max_attempts:
        attempts += 1
        frame = link.read_telemetry(timeout=per_frame_timeout)
        if frame is not None:
            frames.append(frame)

    if not frames:
        raise RuntimeError("no telemetry received - check the receiver is configured and running")

    return trial.compute_trial_result(frames, true_distance_mm)


# ===================== Threshold-zone / sample-index math (§4a) =====================


def estimate_target_sample_index(
    true_distance_mm: float, op_freq_hz: int, odr: int, speed_of_sound_mps: float = SPEED_OF_SOUND_MPS_DEFAULT
) -> int:
    """tuning_algorithm.md §4a. Deliberately NOT SonicLib's own ch_mm_to_samples() formula: that
    bakes in a round-trip /2 that only applies for CH_RANGE_ECHO_ONE_WAY (confirmed in
    ch_common.c's ch_common_range_lsb_to_mm(), gated explicitly on that enum value) - this
    pitch-catch setup uses CH_RANGE_DIRECT (one-way), so no halving belongs here."""
    effective_sample_rate_hz = op_freq_hz / (2 ** (7 - odr))
    return round(true_distance_mm * effective_sample_rate_hz / (speed_of_sound_mps * 1000.0))


def correct_target_sample_index(estimated_index: int, true_distance_mm: float, observed_range_mm: float) -> int:
    """tuning_algorithm.md §4a's empirical self-correction: trust the firmware's own calibrated
    range_mm over the reverse formula when they disagree substantially."""
    if observed_range_mm <= 0:
        return estimated_index
    return round(estimated_index * true_distance_mm / observed_range_mm)


# ===================== Output (§6) =====================


def save_config_json(
    path: Path,
    config: SensorConfig,
    result: "trial.TrialResult",
    passed: bool,
    warnings: List[str],
    true_distance_mm: float,
    speed_of_sound_mps: float,
    expected_target_sample_index: int,
    expected_target_sample_index_source: str,
    total_trials_run: int,
    total_search_wall_clock_s: float,
) -> None:
    """tuning_algorithm.md §6 JSON schema."""
    doc = {
        "schema_version": 1,
        "protocol_version": protocol.PROTOCOL_VERSION,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "true_distance_mm": true_distance_mm,
        "speed_of_sound_mps": speed_of_sound_mps,
        "passed_acceptance_gate": passed,
        "warnings": warnings,
        "measurement": {
            "meas_num": config.meas_num,
            "odr": int(config.odr),
            "meas_period": config.meas_period,
            "sensing_mode": config.sensing_mode,
        },
        "receiver_segments": config.receiver_segments,
        "transmitter_segments": config.transmitter_segments,
        "max_range_mm": config.max_range_mm,
        "algo": {
            "ringdown_cancel_samples": config.ringdown_cancel_samples,
            "static_filter_samples": config.static_filter_samples,
            "iq_output_format": config.iq_output_format,
            "num_ranges": config.num_ranges,
            "filter_update_interval": config.filter_update_interval,
        },
        "thresholds": [{"start_sample": s, "level": l} for s, l in config.thresholds],
        "expected_target_sample_index": expected_target_sample_index,
        "expected_target_sample_index_source": expected_target_sample_index_source,
        "performance": {
            "detection_rate": result.detection_rate,
            "outlier_rate": result.outlier_rate,
            "error_median_mm": result.error_median_mm,
            "error_mad_mm": result.error_mad_mm,
            "error_p95_abs_mm": result.error_p95_abs_mm,
            "amplitude_median": result.amplitude_median,
            "n_frames_confirmation_trial": result.n_frames,
        },
        "search_metadata": {
            "total_trials_run": total_trials_run,
            "total_search_wall_clock_s": total_search_wall_clock_s,
        },
    }
    # tx_num_cycles() asserts the coupling this schema is designed to keep explicit and
    # machine-checkable (see the module docstring / §6): the receiver's COUNT segment must match
    # the transmitter's TX segment's cycle count.
    assert config.receiver_segments[0]["num_cycles"] == config.tx_num_cycles()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=2))


def load_config_json(path: Path) -> dict:
    return json.loads(path.read_text())


# ===================== Staged search (§3) =====================


ODR_STAGE1_CANDIDATES = [Odr.DIV_8, Odr.DIV_4, Odr.DIV_16]
TX_PHASE_COARSE = [0, 4, 8, 12]
GAIN_REDUCE_SWEEP = [0, 8, 16, 24, 28]
THRESHOLD_LEVEL_SWEEP = [200, 350, 500, 700, 1000, 1500, 2500]

SCREENING_N = 10
CONFIRMATION_N = 30
CATASTROPHIC_DETECTION_RATE = 0.10


@dataclass
class SearchState:
    """Mutable state threaded through the staged search - the SensorConfig under test, plus the
    bookkeeping (trial count, wall-clock) that ends up in search_metadata."""

    config: SensorConfig
    op_freq_hz: int
    true_distance_mm: float
    speed_of_sound_mps: float
    expected_target_sample_index: int = 0
    expected_target_sample_index_source: str = "formula"
    total_trials_run: int = 0
    start_time: float = field(default_factory=time.monotonic)

    def screen(self, link: SerialLink) -> "trial.TrialResult":
        apply_config(link, self.config)
        self.total_trials_run += 1
        return run_trial(link, SCREENING_N, self.true_distance_mm)

    def confirm(self, link: SerialLink) -> "trial.TrialResult":
        apply_config(link, self.config)
        self.total_trials_run += 1
        return run_trial(link, CONFIRMATION_N, self.true_distance_mm)

    def elapsed_s(self) -> float:
        return time.monotonic() - self.start_time


def _stage1_odr(link: SerialLink, state: SearchState) -> "trial.TrialResult":
    result = None
    for odr in ODR_STAGE1_CANDIDATES:
        state.config.odr = odr
        result = state.screen(link)
        passed, _ = trial.passes_gate(result, state.true_distance_mm)
        if passed:
            break
    return result


def _stage2_tx_energy(link: SerialLink, state: SearchState) -> "trial.TrialResult":
    result = state.screen(link)
    prev_amplitude = result.amplitude_median
    for _ in range(3):
        tx_seg = next(s for s in state.config.transmitter_segments if s["type"] == "tx")
        if tx_seg["pulse_width"] < 4:
            tx_seg["pulse_width"] += 1
        else:
            new_cycles = min(int(tx_seg["num_cycles"] * 1.5), 65535)
            tx_seg["num_cycles"] = new_cycles
            state.config.receiver_segments[0]["num_cycles"] = new_cycles  # keep COUNT matched
            # (the transmitter's settle COUNT segment is independent of TX cycles - untouched)

        result = state.screen(link)
        if math.isnan(prev_amplitude) or result.amplitude_median <= prev_amplitude * 1.05:
            break  # plateaued - no meaningful headroom gained by the last increase
        prev_amplitude = result.amplitude_median
    return result


def _stage3_tx_phase(link: SerialLink, state: SearchState) -> "trial.TrialResult":
    tx_seg = next(s for s in state.config.transmitter_segments if s["type"] == "tx")
    best_phase, best_amp, best_result = tx_seg["phase"], -1.0, None

    for phase in TX_PHASE_COARSE:
        tx_seg["phase"] = phase
        result = state.screen(link)
        if result.amplitude_median > best_amp:
            best_phase, best_amp, best_result = phase, result.amplitude_median, result

    for phase in [best_phase - 2, best_phase - 1, best_phase + 1, best_phase + 2]:
        if not (0 <= phase <= 15):
            continue
        tx_seg["phase"] = phase
        result = state.screen(link)
        if result.amplitude_median > best_amp:
            best_phase, best_amp, best_result = phase, result.amplitude_median, result

    tx_seg["phase"] = best_phase
    return best_result


def _stage4_rx_gain(link: SerialLink, state: SearchState) -> "trial.TrialResult":
    # Restructure into two RX segments (near attenuated, far normal gain) - tuning_algorithm.md
    # §3 Stage 4. Near segment width is a placeholder until Stage 5 computes a real ringdown
    # estimate; refined again after Stage 5 runs (both stages repeat in run_search's 2nd pass).
    near_samples = min(30, max(1, state.expected_target_sample_index - 10)) if state.expected_target_sample_index else 20
    far_samples = ICU_MAX_NUM_SAMPLES - near_samples

    state.config.receiver_segments = [
        state.config.receiver_segments[0],  # COUNT, unchanged
        {"type": "rx", "num_samples": near_samples, "gain_reduce": 0, "atten": 0, "int_enable": 0},
        {"type": "rx", "num_samples": far_samples, "gain_reduce": 0, "atten": 0, "int_enable": 1},
    ]

    result = None
    for atten in range(4):
        state.config.receiver_segments[1]["atten"] = atten
        result = state.screen(link)
        if result.outlier_rate <= trial.OUTLIER_RATE_MAX:
            break

    best_gain_reduce, best_amp, best_result = 0, -1.0, result
    for gain_reduce in GAIN_REDUCE_SWEEP:
        state.config.receiver_segments[2]["gain_reduce"] = gain_reduce
        result = state.screen(link)
        passed, _ = trial.passes_gate(result, state.true_distance_mm)
        if passed and result.amplitude_median > best_amp:
            best_gain_reduce, best_amp, best_result = gain_reduce, result.amplitude_median, result

    state.config.receiver_segments[2]["gain_reduce"] = best_gain_reduce
    return best_result if best_result is not None else result


def _stage5_ringdown(link: SerialLink, state: SearchState) -> "trial.TrialResult":
    safety_margin = max(10, round(0.20 * state.expected_target_sample_index))
    max_safe_filter_len = max(0, state.expected_target_sample_index - safety_margin)

    result = state.screen(link)
    while state.config.ringdown_cancel_samples < max_safe_filter_len:
        if result.outlier_rate <= trial.OUTLIER_RATE_MAX:
            break
        state.config.ringdown_cancel_samples = min(state.config.ringdown_cancel_samples * 2, max_safe_filter_len)
        result = state.screen(link)

    if result.outlier_rate > trial.OUTLIER_RATE_MAX and state.config.static_filter_samples == 0:
        state.config.static_filter_samples = min(state.config.ringdown_cancel_samples // 2, max_safe_filter_len)
        result = state.screen(link)

    return result


def _stage6_thresholds(link: SerialLink, state: SearchState) -> "trial.TrialResult":
    # Step 0: empirical calibration check (§4a) - one maximally-permissive zone, compare the
    # firmware's own reported range against the true distance, correct the sample-index prior.
    state.config.thresholds = [(0, 200)] + [(0, 0)] * (protocol.NUM_THRESHOLDS - 1)
    calib_result = state.screen(link)
    if calib_result.n_detected > 0 and not _isnan(calib_result.error_median_mm):
        observed_range_mm = state.true_distance_mm + calib_result.error_median_mm
        state.expected_target_sample_index = correct_target_sample_index(
            state.expected_target_sample_index, state.true_distance_mm, observed_range_mm
        )
        state.expected_target_sample_index_source = "corrected_from_calibration_trial"

    idx = state.expected_target_sample_index
    half_width = max(1, round(0.15 * idx))
    zone2_start = max(0, idx - half_width)
    zone3_start = idx + half_width

    result = None
    for level in THRESHOLD_LEVEL_SWEEP:
        state.config.thresholds = [
            (0, level * 3),
            (max(0, zone2_start - 5), round(level * 1.5)),
            (zone2_start, level),
            (zone3_start, round(level * 1.7)),
        ] + [(0, 0)] * (protocol.NUM_THRESHOLDS - 4)
        result = state.screen(link)
        passed, _ = trial.passes_gate(result, state.true_distance_mm)
        if passed:
            break

    return result


def _isnan(x: float) -> bool:
    return math.isnan(x)


def run_search(
    link: SerialLink,
    true_distance_mm: float,
    speed_of_sound_mps: float = SPEED_OF_SOUND_MPS_DEFAULT,
) -> Tuple[SensorConfig, "trial.TrialResult", bool, List[str], SearchState]:
    """tuning_algorithm.md §3-§5 orchestrator. Returns (config, result, passed, warnings, state)
    - state carries expected_target_sample_index/_source and search_metadata for save_config_json.
    """
    op_freq_hz, _num_samples, _max_range = get_status(link)
    config = seed_config(op_freq_hz, true_distance_mm, speed_of_sound_mps)
    state = SearchState(
        config=config, op_freq_hz=op_freq_hz, true_distance_mm=true_distance_mm, speed_of_sound_mps=speed_of_sound_mps
    )
    state.expected_target_sample_index = estimate_target_sample_index(
        true_distance_mm, op_freq_hz, config.odr, speed_of_sound_mps
    )

    result = _stage1_odr(link, state)
    if result.detection_rate < CATASTROPHIC_DETECTION_RATE:
        passed, warnings = False, [
            "Catastrophically low detection rate even at Stage 1 - check wiring/pairing/distance, "
            "not a tuning-parameter problem."
        ]
        return state.config, result, passed, warnings, state

    result = _stage2_tx_energy(link, state)
    result = _stage3_tx_phase(link, state)
    result = _stage4_rx_gain(link, state)
    result = _stage5_ringdown(link, state)
    result = _stage6_thresholds(link, state)

    prev_score = trial.score(result)
    result2 = _stage4_rx_gain(link, state)
    result2 = _stage5_ringdown(link, state)
    result2 = _stage6_thresholds(link, state)
    if trial.score(result2) < prev_score - 0.02 * abs(prev_score if prev_score else 1.0):
        result = result2  # repeat pass improved things meaningfully; keep it

    final_result = state.confirm(link)
    passed, warnings = trial.passes_gate(final_result, true_distance_mm)
    return state.config, final_result, passed, warnings, state
