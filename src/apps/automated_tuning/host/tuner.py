#!/usr/bin/env python3
"""Control script for the automated ultrasonic sensor tuner.

Usage: python tuner.py --port /dev/ttyUSB0 [--baud 115200] [--speed-of-sound-mps 343]

Opens the USB serial link to the receiver board, does the AT_OP_HELLO version handshake, then
offers manual mode (direct RPC calls + live telemetry, see tuning_algorithm.md §7) or automatic
mode (runs the staged search in tuning_search.py and saves the result to configs/<N>meter.config).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List, Optional, Tuple

import protocol
import trial
import tuning_search
from serial_link import ProtocolError, SerialLink, TelemetryFrame


# ===================== Manual mode: per-opcode call body builders =====================


def _prompt_int(label: str, default: Optional[int] = None) -> int:
    suffix = f" [{default}]" if default is not None else ""
    while True:
        raw = input(f"  {label}{suffix}: ").strip()
        if raw == "" and default is not None:
            return default
        try:
            return int(raw, 0)  # base 0 -> accepts "0x1A" etc.
        except ValueError:
            print("  not a valid integer, try again")


def _prompt_target() -> protocol.SegTarget:
    while True:
        raw = input("  target [l=local (receiver's own sensor) / r=remote (relay to transmitter)]: ").strip().lower()
        if raw in ("l", "local"):
            return protocol.SegTarget.LOCAL
        if raw in ("r", "remote"):
            return protocol.SegTarget.REMOTE
        print("  enter 'l' or 'r'")


def _prompt_thresholds() -> List[Tuple[int, int]]:
    print(f"  Enter up to {protocol.NUM_THRESHOLDS} threshold zones; blank start_sample to stop early.")
    zones: List[Tuple[int, int]] = []
    for i in range(protocol.NUM_THRESHOLDS):
        raw = input(f"  zone {i} start_sample (blank to stop): ").strip()
        if raw == "":
            break
        start = int(raw, 0)
        level = _prompt_int("  level")
        zones.append((start, level))
    zones += [(0, 0)] * (protocol.NUM_THRESHOLDS - len(zones))
    return zones


def _build_meas_reset() -> bytes:
    return protocol.CALL_MEAS_RESET.pack(_prompt_int("meas_num", default=0))


def _build_meas_init() -> bytes:
    meas_num = _prompt_int("meas_num", default=0)
    odr = _prompt_int("odr (2=DIV_32 3=DIV_16 4=DIV_8[default] 5=DIV_4 6=DIV_2)", default=4)
    meas_period = _prompt_int("meas_period", default=0)
    mode = _prompt_int("meas_mode (0=active 1=standby)", default=0)
    return protocol.CALL_MEAS_INIT.pack(meas_num, odr, meas_period, mode)


def _build_add_segment_tx() -> bytes:
    meas_num = _prompt_int("meas_num", default=0)
    target = _prompt_target()
    num_cycles = _prompt_int("num_cycles")
    pulse_width = _prompt_int("pulse_width (1-4)", default=3)
    phase = _prompt_int("phase (0-15)", default=8)
    int_enable = _prompt_int("int_enable (0/1)", default=0)
    return protocol.CALL_ADD_SEGMENT_TX.pack(meas_num, target, num_cycles, pulse_width, phase, int_enable)


def _build_add_segment_rx() -> bytes:
    meas_num = _prompt_int("meas_num", default=0)
    target = _prompt_target()
    num_samples = _prompt_int("num_samples", default=tuning_search.ICU_MAX_NUM_SAMPLES)
    gain_reduce = _prompt_int("gain_reduce (0-31)", default=0)
    atten = _prompt_int("atten (0-3)", default=0)
    int_enable = _prompt_int("int_enable (0/1)", default=1)
    return protocol.CALL_ADD_SEGMENT_RX.pack(meas_num, target, num_samples, gain_reduce, atten, int_enable)


def _build_add_segment_count() -> bytes:
    meas_num = _prompt_int("meas_num", default=0)
    target = _prompt_target()
    num_cycles = _prompt_int("num_cycles")
    int_enable = _prompt_int("int_enable (0/1)", default=0)
    return protocol.CALL_ADD_SEGMENT_COUNT.pack(meas_num, target, num_cycles, int_enable)


def _build_set_odr() -> bytes:
    return protocol.CALL_SET_ODR.pack(_prompt_int("meas_num", default=0), _prompt_int("odr", default=4))


def _build_set_num_samples() -> bytes:
    return protocol.CALL_SET_NUM_SAMPLES.pack(_prompt_int("meas_num", default=0), _prompt_int("num_samples"))


def _build_set_max_range_meas() -> bytes:
    return protocol.CALL_SET_MAX_RANGE_MEAS.pack(_prompt_int("meas_num", default=0), _prompt_int("max_range_mm"))


def _build_set_max_range() -> bytes:
    return protocol.CALL_SET_MAX_RANGE.pack(_prompt_int("max_range_mm", default=5000))


def _build_set_thresholds() -> bytes:
    return protocol.pack_thresholds(_prompt_thresholds())


def _build_gpt_algo_configure() -> bytes:
    meas_num = _prompt_int("meas_num", default=0)
    ringdown = _prompt_int("ringdown_cancel_samples", default=20)
    static_filter = _prompt_int("static_filter_samples", default=0)
    iq_fmt = _prompt_int("iq_output_format (0=IQ 1=mag/thresh 2=mag/phase)", default=0)
    num_ranges = _prompt_int("num_ranges", default=1)
    filter_interval = _prompt_int("filter_update_interval", default=0)
    thresholds = _prompt_thresholds()
    head = protocol.CALL_GPT_ALGO_CONFIGURE_HEAD.pack(
        meas_num, ringdown, static_filter, iq_fmt, num_ranges, filter_interval
    )
    return head + protocol.pack_thresholds(thresholds)


def _build_set_mode() -> bytes:
    return protocol.CALL_SET_MODE.pack(
        _prompt_int("mode (0x00=IDLE 0x10=TRIGGERED_TX_RX 0x20=TRIGGERED_RX_ONLY)", default=0x20)
    )


CALL_BUILDERS = {
    protocol.Opcode.MEAS_RESET: _build_meas_reset,
    protocol.Opcode.MEAS_INIT: _build_meas_init,
    protocol.Opcode.ADD_SEGMENT_TX: _build_add_segment_tx,
    protocol.Opcode.ADD_SEGMENT_RX: _build_add_segment_rx,
    protocol.Opcode.ADD_SEGMENT_COUNT: _build_add_segment_count,
    protocol.Opcode.SET_ODR: _build_set_odr,
    protocol.Opcode.SET_NUM_SAMPLES: _build_set_num_samples,
    protocol.Opcode.SET_MAX_RANGE_MEAS: _build_set_max_range_meas,
    protocol.Opcode.SET_MAX_RANGE: _build_set_max_range,
    protocol.Opcode.SET_THRESHOLDS: _build_set_thresholds,
    protocol.Opcode.GPT_ALGO_CONFIGURE: _build_gpt_algo_configure,
    protocol.Opcode.SET_MODE: _build_set_mode,
    # HELLO, MEAS_WRITE_CONFIG, GET_THRESHOLDS, GET_STATUS have empty call bodies - no builder.
}


def _decode_response(opcode: protocol.Opcode, status: protocol.Status, ret_data: bytes) -> str:
    if status != protocol.Status.OK:
        return f"status={status.name}"
    if opcode == protocol.Opcode.HELLO:
        (version,) = protocol.RESP_HELLO.unpack(ret_data)
        return f"status=OK version={version}"
    if opcode == protocol.Opcode.GET_THRESHOLDS:
        return f"status=OK thresholds={protocol.unpack_thresholds(ret_data)}"
    if opcode == protocol.Opcode.GET_STATUS:
        op_freq, num_samples, max_range = protocol.RESP_GET_STATUS.unpack(ret_data)
        return f"status=OK op_freq_hz={op_freq} num_samples={num_samples} max_range_mm={max_range}"
    return "status=OK"


def manual_mode(link: SerialLink) -> None:
    opcodes = [op for op in protocol.Opcode if op != protocol.Opcode.RESERVED]
    while True:
        print("\nManual mode - choose an opcode, 't' for live telemetry, or 'b' to go back:")
        for i, op in enumerate(opcodes, 1):
            print(f"  {i:2d}) {op.name}")
        choice = input("> ").strip().lower()

        if choice == "b":
            return
        if choice == "t":
            live_telemetry(link)
            continue
        try:
            opcode = opcodes[int(choice) - 1]
        except (ValueError, IndexError):
            print("invalid choice")
            continue

        builder = CALL_BUILDERS.get(opcode)
        body = builder() if builder else b""
        try:
            status, ret_data = link.call(opcode, body)
        except (TimeoutError, ProtocolError) as e:
            print(f"error: {e}")
            continue
        print(_decode_response(opcode, status, ret_data))


def live_telemetry(link: SerialLink, window: int = 20) -> None:
    print("Showing live telemetry (Ctrl+C to stop)...")
    frames: List[TelemetryFrame] = []
    try:
        while True:
            frame = link.read_telemetry(timeout=2.0)
            if frame is None:
                print("  (no telemetry in 2s - is the sensor configured and armed?)")
                continue
            frames.append(frame)
            del frames[:-window]
            tag = "TARGET" if frame.have_target else "no target"
            print(f"  seq={frame.meas_seq:6d}  {tag:10s}  range={frame.range_mm:8.1f}mm  amp={frame.amplitude}")
    except KeyboardInterrupt:
        print()
        if not frames:
            return
        # Reuse trial.py's stats for the live display, per tuning_algorithm.md §7 - no separate
        # ad hoc calculation just because this is manual mode.
        raw = input("Enter true distance to compute rolling error stats (blank to skip): ").strip()
        if raw:
            result = trial.compute_trial_result(frames, _parse_distance_mm(raw))
            print(
                f"  last {len(frames)} frames: detection_rate={result.detection_rate:.2f} "
                f"error_median_mm={result.error_median_mm:.1f} outlier_rate={result.outlier_rate:.2f}"
            )


# ===================== Automatic mode =====================


def _parse_distance_mm(raw: str) -> float:
    raw = raw.strip().lower()
    if raw.endswith("mm"):
        return float(raw[:-2])
    if raw.endswith("m"):
        return float(raw[:-1]) * 1000.0
    return float(raw)  # bare number assumed mm


def automatic_mode(link: SerialLink, speed_of_sound_mps: float) -> None:
    raw = input("Enter true distance between sensors (e.g. '3m', '1500mm', or a bare number in mm): ")
    true_distance_mm = _parse_distance_mm(raw)

    print(f"Running automatic tuning search for {true_distance_mm:.0f} mm ...")
    config, result, passed, warnings, state = tuning_search.run_search(link, true_distance_mm, speed_of_sound_mps)

    print("\n=== Search complete ===")
    print(f"passed_acceptance_gate: {passed}")
    for w in warnings:
        print(f"  warning: {w}")
    print(
        f"detection_rate={result.detection_rate:.2f}  outlier_rate={result.outlier_rate:.2f}  "
        f"error_median_mm={result.error_median_mm:.1f}  error_mad_mm={result.error_mad_mm:.1f}"
    )
    print(f"trials run: {state.total_trials_run}  wall clock: {state.elapsed_s():.1f}s")

    meters = true_distance_mm / 1000.0
    configs_dir = Path(__file__).resolve().parent.parent / "configs"
    path = configs_dir / f"{meters:g}meter.config"
    tuning_search.save_config_json(
        path,
        config,
        result,
        passed,
        warnings,
        true_distance_mm=true_distance_mm,
        speed_of_sound_mps=speed_of_sound_mps,
        expected_target_sample_index=state.expected_target_sample_index,
        expected_target_sample_index_source=state.expected_target_sample_index_source,
        total_trials_run=state.total_trials_run,
        total_search_wall_clock_s=state.elapsed_s(),
    )
    print(f"Saved to {path}")


# ===================== Entry point =====================


def main() -> None:
    parser = argparse.ArgumentParser(description="Automated ultrasonic sensor tuner control script")
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--speed-of-sound-mps",
        type=float,
        default=tuning_search.SPEED_OF_SOUND_MPS_DEFAULT,
        help="Override for local temperature/altitude (default 343.0, 20degC air) - tuning_algorithm.md §4a",
    )
    args = parser.parse_args()

    print(f"Opening {args.port} @ {args.baud} baud...")
    with SerialLink(args.port, baud=args.baud) as link:
        try:
            version = link.hello()
        except (TimeoutError, ProtocolError) as e:
            print(f"HELLO handshake failed: {e}")
            sys.exit(1)
        print(f"Connected - receiver protocol version {version}")

        while True:
            choice = input("\n[M]anual, [A]utomatic, or [Q]uit? ").strip().lower()
            if choice.startswith("m"):
                manual_mode(link)
            elif choice.startswith("a"):
                automatic_mode(link, args.speed_of_sound_mps)
            elif choice.startswith("q"):
                break
            else:
                print("please enter m, a, or q")


if __name__ == "__main__":
    main()
