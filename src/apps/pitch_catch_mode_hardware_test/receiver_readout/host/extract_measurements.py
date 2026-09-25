#!/usr/bin/env python3
"""Split a captured receiver_readout console log into one CSV per measurement.

Standard library only. Parses the `IQ_BEGIN ... / IQ,<idx>,<i>,<q> / IQ_END` blocks
receiver_readout prints (see receiver_readout/main/readout_loop.c's print_iq_dump()),
ignoring any interleaved picocom/ESP_LOGx lines, and writes each block as its own
idx,i,q CSV - the format plot_readout.py expects.

Usage:
    ./extract_measurements.py capture.txt
    ./extract_measurements.py capture.txt --prefix 25ft -o readouts/25ft
"""

import argparse
import csv
import re
import sys
from pathlib import Path

BEGIN_RE = re.compile(
    r"^IQ_BEGIN meas=(?P<meas>\d+) num_samples=(?P<num_samples>\d+) "
    r"target=(?P<target>\d) range_mm=(?P<range_mm>\S+) amp=(?P<amp>\d+)"
)
SAMPLE_RE = re.compile(r"^IQ,(?P<idx>-?\d+),(?P<i>-?\d+),(?P<q>-?\d+)$")


def parse_measurements(text):
    """Yield (meas_num, header_dict, [(idx, i, q), ...]) in file order."""
    cur_meas = None
    cur_header = None
    cur_samples = None
    for raw_line in text.splitlines():
        line = raw_line.strip()
        m = BEGIN_RE.match(line)
        if m:
            cur_meas = int(m.group("meas"))
            cur_header = m.groupdict()
            cur_samples = []
            continue
        if line == "IQ_END":
            if cur_meas is not None:
                yield cur_meas, cur_header, cur_samples
            cur_meas = cur_header = cur_samples = None
            continue
        if cur_samples is not None:
            m = SAMPLE_RE.match(line)
            if m:
                cur_samples.append((int(m.group("idx")), int(m.group("i")), int(m.group("q"))))
            # silently skip anything else mid-block (a stray log line, a corrupted byte) -
            # matches the wire-format contract documented in README.md's "Raw data readout"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", help="captured console log (e.g. picocom session, session.txt)")
    parser.add_argument("--prefix", help="output filename prefix (default: capture file's stem)")
    parser.add_argument("-o", "--outdir", help="output directory (default: alongside the capture file)")
    args = parser.parse_args()

    capture_path = Path(args.capture)
    prefix = args.prefix or capture_path.stem
    outdir = Path(args.outdir) if args.outdir else capture_path.parent
    outdir.mkdir(parents=True, exist_ok=True)

    text = capture_path.read_text(errors="replace")
    found = list(parse_measurements(text))
    if not found:
        raise SystemExit(f"{capture_path}: no IQ_BEGIN/IQ_END blocks found")

    print(f"{'meas':>5} {'target':>6} {'range_mm':>9} {'amp':>6}  file")
    for meas, header, samples in found:
        out_path = outdir / f"{prefix}_{meas}.csv"
        with open(out_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["idx", "i", "q"])
            w.writerows(samples)
        print(f"{meas:>5} {header['target']:>6} {header['range_mm']:>9} {header['amp']:>6}  {out_path}")

    print(f"\nwrote {len(found)} measurement(s) to {outdir}/")


if __name__ == "__main__":
    main()
