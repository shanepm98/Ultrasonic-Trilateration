#!/usr/bin/env python3
"""Derive the receiver's 8-segment detection threshold table from labelled I/Q recordings.

Standard library only. Reads a directory written by extract_measurements.py (manifest.csv + one
idx,i,q CSV per reading, each batch recorded at a known sender-receiver distance), fits a
worst-case direct-path amplitude envelope over distance, and writes a ch_thresholds_t table
(all CH_NUM_THRESHOLDS = 8 {start_sample, level} slots) as a C source file for the
src/components/sensor_calibration component. See docs/threshold_calibration.md.

Steps:
  1. Arrival of each batch: the peak of the batch's median |I,Q| envelope within
     +-ARRIVAL_SEARCH samples of the nominal index (distance / mm-per-sample + a fitted fixed
     offset); the shift from nominal is reported (a large one suggests a mis-measured distance).
     Direct-path peak of each reading: the largest |I,Q| within +-PEAK_WINDOW samples of that
     arrival. The received pulse ramps up for about the TX burst length (--tx-us) before its
     peak, so the noise floor is every sample from the end of the ringdown window up to the start
     of that ramp - the region where a noise crossing would be reported as a (false) first target.
  2. Batches whose title contains "45deg" are tested for SNR (1st-percentile peak / noise max) and
     excluded from the fit if it is below --min-snr (with the 2026-10-05 data: only 4 m), unless
     --include-45 is given.
  3. Threshold units: the sensor compares its own magnitude (Cordic_mag(I,Q)), not hypot(I,Q),
     against the threshold. The scale k (hypot / threshold-units) is fitted from the
     firmware table the recordings were captured with (--old-threshold) and the fraction of
     target=1 readings in each batch that has both hits and misses: the firmware's
     effective threshold is the (1 - hit fraction) quantile of that batch's peaks. The largest
     per-batch k is used (conservative: lower levels).
  4. Worst-case envelope: per distance, the 1st-percentile peak of the weakest included batch,
     interpolated log-linearly in distance and extrapolated at the ends.
  5. Segment 0 covers the ringdown window. Segments 1-7 split [ringdown end, X] so the envelope
     drops by the same factor across each, where X is where the level would reach the noise floor
     (or max range, if sooner); the last segment holds the floor out to max range. Each level is margin * envelope at the
     segment's far end / k, floored at noise_mult * noise max / k, and never rises with distance.
  6. Both tables are replayed on every reading (first index >= ringdown with |I,Q| / k >= level)
     and the detection / false-detection rates are printed and recorded in the C file. A detection
     counts as correct anywhere from the start of the ramp to PEAK_WINDOW samples past the peak.

Usage:
    ./calibrate_thresholds.py                                  # ../../readouts/parsed
    ./calibrate_thresholds.py path/to/parsed --margin 0.5 --max-range-mm 5000
    ./calibrate_thresholds.py --dry-run                        # report only, write nothing
"""

import argparse
import csv
import math
import sys
from collections import defaultdict
from datetime import date
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_PARSED_DIR = HERE.parent.parent / "readouts" / "parsed"
DEFAULT_OUT_C = HERE.parents[3] / "components" / "sensor_calibration" / "rx_thresholds.c"

CH_SPEEDOFSOUND_MPS = 343
NUM_THRESHOLDS = 8  # CH_NUM_THRESHOLDS (LEN_THRESH) for Shasta / ICU sensors
LEVEL_MAX = 0xFFFF  # ch_thresh_t.level is uint16_t
PEAK_WINDOW = 4     # samples either side of the arrival
ARRIVAL_SEARCH = 12  # samples either side of the nominal index searched for a batch's arrival
# Table the parsed recordings were captured with (receiver_readout/main/readout_loop.c rx_thresholds).
DEFAULT_OLD_THRESHOLDS = [(0, 2500), (30, 1200), (60, 700), (120, 450), (200, 350)]
EXCLUDE_TAG = "45deg"


def parse_threshold_arg(spec):
    try:
        start, level = spec.split(":")
        return int(start), int(level)
    except ValueError:
        raise argparse.ArgumentTypeError(f"threshold must be START:LEVEL, got {spec!r}")


def percentile(values, p):
    v = sorted(values)
    return v[min(len(v) - 1, int(p * len(v)))]


def level_at(table, idx):
    level = table[0][1]
    for start, lvl in table:
        if idx >= start:
            level = lvl
    return level


def first_detection(mag, table, k, ringdown):
    for j in range(ringdown, len(mag)):
        if mag[j] / k >= level_at(table, j):
            return j
    return None


def load(parsed_dir):
    rows = list(csv.DictReader(open(parsed_dir / "manifest.csv", newline="")))
    readings = []
    for r in rows:
        if r["error"] or not r["file"]:
            continue
        with open(parsed_dir / r["file"], newline="") as f:
            data = list(csv.reader(f))[1:]
        readings.append({
            "title": r["title"],
            "actual_mm": float(r["actual_mm"]),
            "target": r["target"] == "1",
            "odr": int(r["odr"]),
            "op_freq": int(r["op_freq_hz"]),
            "mag": [math.hypot(int(a[1]), int(a[2])) for a in data],
        })
    if not readings:
        sys.exit(f"no readings in {parsed_dir}")
    return readings


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("parsed_dir", nargs="?", type=Path, default=DEFAULT_PARSED_DIR,
                        help="extract_measurements.py output directory (default: %(default)s)")
    parser.add_argument("--out-c", type=Path, default=DEFAULT_OUT_C, help="C file to write (default: %(default)s)")
    parser.add_argument("--dry-run", action="store_true", help="print the report and table, write nothing")
    parser.add_argument("--max-range-mm", type=float, default=5000, help="far end of the table (default: %(default)s)")
    parser.add_argument("--margin", type=float, default=0.5,
                        help="level = margin * worst-case envelope (default: %(default)s, i.e. -6 dB)")
    parser.add_argument("--noise-mult", type=float, default=2,
                        help="levels never go below this multiple of the noise max (default: %(default)s)")
    parser.add_argument("--ringdown-samples", type=int, default=20,
                        help="ringdown_cancel_samples the recordings and table assume (default: %(default)s)")
    parser.add_argument("--tx-us", type=float, default=450,
                        help="sender TX burst length the recordings used, PC_TX_PULSE_US (default: %(default)s)")
    parser.add_argument("--min-snr", type=float, default=2,
                        help=f"'{EXCLUDE_TAG}' batches below this SNR are excluded (default: %(default)s)")
    parser.add_argument("--include-45", action="store_true", help=f"fit every '{EXCLUDE_TAG}' batch, whatever its SNR")
    parser.add_argument("--old-threshold", type=parse_threshold_arg, action="append", metavar="START:LEVEL",
                        help="firmware table the recordings were captured with, repeatable (default: "
                             + ", ".join(f"{s}:{l}" for s, l in DEFAULT_OLD_THRESHOLDS) + ")")
    args = parser.parse_args()
    old_table = args.old_threshold or DEFAULT_OLD_THRESHOLDS
    rd = args.ringdown_samples

    readings = load(args.parsed_dir)
    odrs = {r["odr"] for r in readings}
    freqs = {r["op_freq"] for r in readings}
    if len(odrs) != 1:
        sys.exit(f"recordings mix ODRs {sorted(odrs)}; calibrate one ODR at a time")
    odr = odrs.pop()
    op_freq = sum(freqs) / len(freqs)
    mm_per_sample = CH_SPEEDOFSOUND_MPS * 1000 * 2 ** (7 - odr) / op_freq  # one-way, see plot_readout.py

    # Fixed arrival offset (trigger / count-segment latency): median of the global post-ringdown peak
    # position against the nominal index.
    offsets = sorted(max(range(rd, len(r["mag"])), key=r["mag"].__getitem__) - r["actual_mm"] / mm_per_sample
                     for r in readings)
    offset = offsets[len(offsets) // 2]

    def index_of(mm):
        return mm / mm_per_sample + offset

    def mm_of(idx):
        return (idx - offset) * mm_per_sample

    # Pulse ramp-up: the TX burst length, in samples
    rise = math.ceil(args.tx_us * op_freq / 2 ** (7 - odr) / 1e6)
    lead = rise + PEAK_WINDOW

    groups = defaultdict(list)
    for r in readings:
        groups[r["title"]].append(r)
    shifts = {}
    for title, rs in groups.items():
        nominal = round(index_of(rs[0]["actual_mm"]))
        n = min(len(r["mag"]) for r in rs)
        lo, hi = max(rd, nominal - ARRIVAL_SEARCH), min(n - 1, nominal + ARRIVAL_SEARCH)
        median_env = {j: sorted(r["mag"][j] for r in rs)[len(rs) // 2] for j in range(lo, hi + 1)}
        exp = max(median_env, key=median_env.get)
        shifts[title] = exp - nominal
        for r in rs:
            r["exp"] = exp
            r["peak"] = max(r["mag"][max(rd, exp - PEAK_WINDOW):exp + PEAK_WINDOW + 1])

    noise = [m for r in readings for m in r["mag"][rd:max(rd, r["exp"] - lead)]]
    noise_max = max(noise)
    ringdown_max = max(m for r in readings for m in r["mag"][:rd])

    print(f"{len(readings)} readings, {len(groups)} batches; op freq {op_freq:.0f} Hz, ODR {odr} -> "
          f"{mm_per_sample:.2f} mm/sample, arrival offset {offset:+.2f} samples, "
          f"{args.tx_us:g} us TX burst = {rise}-sample ramp")
    print(f"pre-arrival noise: p50 {percentile(noise, .5):.0f}, p99.9 {percentile(noise, .999):.0f}, "
          f"max {noise_max:.0f} (|I,Q|); ringdown window max {ringdown_max:.0f}\n")

    # 1-2: per-batch peaks + 45deg gate
    print(f"{'batch':24s} {'mm':>5s} {'idx':>4s} {'shift':>5s} {'p1':>6s} {'p50':>6s} {'SNR':>5s} {'fw hits':>8s}  use")
    included = {}
    for title, rs in sorted(groups.items(), key=lambda kv: (kv[1][0]["actual_mm"], kv[0])):
        peaks = [r["peak"] for r in rs]
        p1 = percentile(peaks, .01)
        use = args.include_45 or EXCLUDE_TAG not in title or p1 / noise_max >= args.min_snr
        if use:
            included[title] = rs
        print(f"{title:24s} {rs[0]['actual_mm']:5.0f} {rs[0]['exp']:4d} {shifts[title]:+5d} {p1:6.0f} {percentile(peaks, .5):6.0f} "
              f"{p1 / noise_max:5.1f} {sum(r['target'] for r in rs):4d}/{len(rs):<3d}  {'yes' if use else 'EXCLUDED'}")

    # 3: threshold-unit scale k, per batch with mixed firmware hits/misses. The firmware detected a
    # fraction f of the batch, so its effective threshold sits at the (1 - f) quantile of the
    # batch's peaks; k = that |I,Q| / the old table's level at the arrival index.
    ks = []
    for title, rs in groups.items():
        n_hit = sum(r["target"] for r in rs)
        if 0 < n_hit < len(rs):
            cutoff = percentile([r["peak"] for r in rs], 1 - n_hit / len(rs))
            ks.append((cutoff / level_at(old_table, rs[0]["exp"]), title))
    if not ks:
        sys.exit("no batch has both firmware hits and misses - can't fit the threshold scale k")
    k = max(ks)[0]
    print("\nthreshold scale k (|I,Q| per threshold unit), per batch: "
          + ", ".join(f"{t} {kk:.2f}" for kk, t in sorted(ks)) + f" -> using {k:.2f}")

    # 4: worst-case envelope (log-linear in distance)
    # Placed at the measured arrival (mm_of(exp)), not the tape distance, so the envelope lines up
    # with sample indices even where a batch's distance was mis-measured.
    by_dist = defaultdict(list)
    for rs in included.values():
        by_dist[rs[0]["actual_mm"]].append(rs)
    env_pts = sorted((mm_of(sorted(rs[0]["exp"] for rs in bs)[len(bs) // 2]),
                      min(percentile([r["peak"] for r in rs], .01) for rs in bs)) for bs in by_dist.values())
    if len(env_pts) < 2:
        sys.exit("need at least two distances to fit the envelope")

    def envelope(mm):
        i = 1
        while i < len(env_pts) - 1 and mm > env_pts[i][0]:
            i += 1
        (d0, a0), (d1, a1) = env_pts[i - 1], env_pts[i]
        return math.exp(math.log(a0) + (math.log(a1) - math.log(a0)) * (mm - d0) / (d1 - d0))

    # 5: segments - equal log-envelope drop between ringdown end and the point where the level
    # reaches the noise floor (or max range, if sooner); beyond that the floor applies.
    floor = args.noise_mult * noise_max / k
    end_idx = index_of(args.max_range_mm)
    span_end = rd + 1
    while span_end < end_idx and args.margin * envelope(mm_of(span_end)) / k > floor:
        span_end += 1
    n_seg = NUM_THRESHOLDS - 1
    log_hi, log_lo = math.log(envelope(mm_of(rd))), math.log(envelope(mm_of(span_end)))
    starts = [rd]
    for s in range(1, n_seg):
        target = log_hi + (log_lo - log_hi) * s / n_seg
        j = starts[-1] + 1
        while j < span_end and math.log(envelope(mm_of(j))) > target:
            j += 1
        starts.append(j)
    table = [(0, min(LEVEL_MAX, math.ceil(args.noise_mult * ringdown_max / k)))]
    for s, start in enumerate(starts):
        far = starts[s + 1] if s + 1 < len(starts) else max(span_end, end_idx)
        lvl = max(floor, args.margin * envelope(mm_of(far)) / k)
        lvl = min(lvl, table[-1][1]) if s else lvl
        table.append((start, min(LEVEL_MAX, int(lvl))))

    # 6: replay
    def replay(tbl, rs):
        hit = false = 0
        for r in rs:
            j = first_detection(r["mag"], tbl, k, rd)
            if j is None:
                continue
            if r["exp"] - lead <= j <= r["exp"] + PEAK_WINDOW:
                hit += 1
            else:
                false += 1
        return hit, false

    print("\nnew table:")
    print(f"  {'start':>5s} {'from mm':>8s} {'level':>6s} {'(|I,Q|)':>8s}")
    for start, lvl in table:
        print(f"  {start:5d} {mm_of(start):8.0f} {lvl:6d} {lvl * k:8.0f}")

    print(f"\nreplay (detected at the direct path / false or late; window -{lead}..+{PEAK_WINDOW} samples "
          "around the peak):")
    print(f"{'batch':24s} {'old table':>14s} {'new table':>14s}")
    replay_lines = []
    for title, rs in sorted(groups.items(), key=lambda kv: (kv[1][0]["actual_mm"], kv[0])):
        oh, of = replay(old_table, rs)
        nh, nf = replay(table, rs)
        n = len(rs)
        tag = "" if title in included else "  (excluded)"
        print(f"{title:24s} {oh:4d}/{n:<3d} f{of:<4d} {nh:4d}/{n:<3d} f{nf:<4d}{tag}")
        replay_lines.append(f"{title}: {nh}/{n} detected, {nf} false{tag}")

    if args.dry_run:
        return
    write_c(args.out_c, table, replay_lines, dict(
        source=args.parsed_dir.name, tx_us=args.tx_us, op_freq=op_freq, odr=odr, mm_ps=mm_per_sample, offset=offset, k=k,
        margin=args.margin, noise_mult=args.noise_mult, noise_max=noise_max, rd=rd,
        excluded=sorted(set(groups) - set(included)), max_mm=args.max_range_mm, env=env_pts))
    print(f"\nwrote {args.out_c}")


def write_c(path, table, replay_lines, m):
    env = ", ".join(f"{d / 1000:.2f} m {a:.0f}" for d, a in m["env"])
    lines = [
        "/*! \\file rx_thresholds.c",
        " *",
        " * \\brief Receiver detection threshold table. GENERATED by",
        " * src/apps/pitch_catch_mode_hardware_test/receiver_readout/host/calibrate_thresholds.py",
        f" * on {date.today().isoformat()} - re-run the script rather than editing by hand. See",
        " * docs/threshold_calibration.md.",
        " *",
        f" * Source: readouts/{m['source']} (op freq {m['op_freq']:.0f} Hz, ODR {m['odr']} -> {m['mm_ps']:.2f} mm/sample",
        f" * one-way, arrival offset {m['offset']:+.2f} samples), {m['tx_us']:g} us TX burst, ringdown cancel {m['rd']} samples,",
        f" * 0-{m['max_mm']:.0f} mm.",
        f" * Worst-case direct-path |I,Q| (p1), at the measured arrival: {env}.",
        f" * Level = {m['margin']:g} x envelope at the segment's far end / k, k = {m['k']:.2f} |I,Q| per threshold unit,",
        f" * floor = {m['noise_mult']:g} x noise max ({m['noise_max']:.0f}) / k.",
        " * Excluded from the fit: " + (", ".join(m["excluded"]) if m["excluded"] else "none") + ".",
        " *",
        " * Replay of this table against the recordings:",
    ]
    lines += [f" *   {line}" for line in replay_lines]
    lines += [
        " */",
        "",
        '#include "sensor_calibration.h"',
        "",
        "const ch_thresholds_t sc_rx_thresholds = {",
        "    .threshold = {",
    ]
    lines += [f"        {{.start_sample = {s}, .level = {lvl}}}," for s, lvl in table]
    lines += ["    },", "};", ""]
    path.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
