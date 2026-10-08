#!/usr/bin/env python3
"""Split a captured receiver_readout console log into one CSV per measurement.

Standard library only. Parses the `BEGIN / title= / actual_mm= / meas=... / IQ,<idx>,<i>,<q> / END`
blocks receiver_readout prints (see receiver_readout/main/readout_loop.c's print_iq_block()),
ignoring any interleaved picocom/ESP_LOGx lines, and writes each block as its own idx,i,q CSV -
the format plot_readout.py expects - plus a manifest.csv with one row per block (title, actual
distance, firmware summary, ODR, operating frequency, file). Error blocks (`meas=<n> error=<reason>`) get a manifest row
but no CSV. Older captures in the `IQ_BEGIN ... / IQ_END` format are still accepted.

Output files are named <prefix>_<meas>.csv, where prefix is --prefix if given, else the block's
batch title (non-filename characters replaced with '_'), else the capture file's stem.

The sensor's operating frequency and ODR set the sample spacing, so they affect every distance
derived from the I/Q data. They are recorded in each manifest row from --op-freq / --odr: take
them from receiver_readout's startup log ("op freq ... Hz" and "sensor ODR: N"). --odr defaults to
4 (CH_ODR_DEFAULT, PC_ODR); --op-freq has no default and is left blank if not given.

Usage:
    ./extract_measurements.py capture.txt --op-freq 84930 --odr 4
    ./extract_measurements.py capture.txt -o readouts/bench --op-freq 84930
    ./extract_measurements.py old_capture.txt --prefix 25ft -o readouts/25ft
"""

import argparse
import csv
import re
import sys
from pathlib import Path

DEFAULT_ODR = 4  # CH_ODR_FREQ_DIV_8 / CH_ODR_DEFAULT - PC_ODR in pitch_catch_common.h

SUMMARY_RE = re.compile(
    r"^meas=(?P<meas>\d+) num_samples=(?P<num_samples>\d+) "
    r"target=(?P<target>\d) range_mm=(?P<range_mm>\S+) amp=(?P<amp>\d+)"
)
ERROR_RE = re.compile(r"^meas=(?P<meas>\d+) error=(?P<error>\S+)")
LEGACY_BEGIN_RE = re.compile(r"^IQ_BEGIN (?P<summary>meas=.*)")
SAMPLE_RE = re.compile(r"^IQ,(?P<idx>-?\d+),(?P<i>-?\d+),(?P<q>-?\d+)$")

BLANK_HEADER = {"title": "", "actual_mm": "", "meas": None, "num_samples": "", "target": "",
                "range_mm": "", "amp": "", "error": ""}


def parse_measurements(text):
    """Yield (header_dict, [(idx, i, q), ...]) per block, in file order."""
    header = None
    samples = None
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line == "BEGIN":
            header, samples = dict(BLANK_HEADER), []
            continue
        m = LEGACY_BEGIN_RE.match(line)
        if m:
            header, samples = dict(BLANK_HEADER), []
            line = m.group("summary")  # fall through to the summary match below
        if line in ("END", "IQ_END"):
            if header is not None and header["meas"] is not None:
                yield header, samples
            header = samples = None
            continue
        if header is None:
            continue
        if line.startswith("title="):
            header["title"] = line[len("title="):]
        elif line.startswith("actual_mm="):
            header["actual_mm"] = line[len("actual_mm="):]
        elif (m := SUMMARY_RE.match(line)) or (m := ERROR_RE.match(line)):
            header.update(m.groupdict())
            header["meas"] = int(header["meas"])
        elif m := SAMPLE_RE.match(line):
            samples.append((int(m.group("idx")), int(m.group("i")), int(m.group("q"))))
        # silently skip anything else mid-block (a stray log line, a corrupted byte) -
        # matches the wire-format contract documented in README.md's "Raw data readout"


def safe_name(s):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", s).strip("_")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", help="captured console log (e.g. picocom session, session.txt)")
    parser.add_argument("--prefix", help="output filename prefix (default: batch title, else capture file's stem)")
    parser.add_argument("-o", "--outdir", help="output directory (default: alongside the capture file)")
    parser.add_argument("--op-freq", type=int, metavar="HZ",
                        help="sensor operating frequency for this capture, from the startup log's "
                             "'op freq' line (default: blank in the manifest)")
    parser.add_argument("--odr", type=int, default=DEFAULT_ODR, choices=[2, 3, 4, 5, 6], metavar="N",
                        help=f"CH_ODR_FREQ_DIV_* value for this capture, from the startup log's "
                             f"'sensor ODR' line (default: {DEFAULT_ODR}, CH_ODR_DEFAULT - PC_ODR)")
    args = parser.parse_args()
    if args.op_freq is None:
        print("note: no --op-freq given - manifest op_freq_hz column left blank", file=sys.stderr)

    capture_path = Path(args.capture)
    outdir = Path(args.outdir) if args.outdir else capture_path.parent
    outdir.mkdir(parents=True, exist_ok=True)

    text = capture_path.read_text(errors="replace")
    found = list(parse_measurements(text))
    if not found:
        raise SystemExit(f"{capture_path}: no BEGIN/END (or IQ_BEGIN/IQ_END) blocks found")

    manifest_path = outdir / "manifest.csv"
    fields = ["title", "actual_mm", "meas", "num_samples", "target", "range_mm", "amp", "error",
              "odr", "op_freq_hz", "file"]
    capture_cfg = {"odr": args.odr, "op_freq_hz": "" if args.op_freq is None else args.op_freq}
    print(f"{'title':>16} {'actual':>7} {'meas':>5} {'target':>6} {'range_mm':>9} {'amp':>6}  file")
    with open(manifest_path, "w", newline="") as mf:
        mw = csv.DictWriter(mf, fieldnames=fields)
        mw.writeheader()
        for header, samples in found:
            meas = header["meas"]
            file_prefix = args.prefix or safe_name(header["title"]) or capture_path.stem
            out_path = ""
            if not header["error"]:
                out_path = outdir / f"{file_prefix}_{meas}.csv"
                with open(out_path, "w", newline="") as f:
                    w = csv.writer(f)
                    w.writerow(["idx", "i", "q"])
                    w.writerows(samples)
            mw.writerow({**header, **capture_cfg, "file": out_path and out_path.name})
            result = f"error={header['error']}" if header["error"] else out_path
            print(f"{header['title'][:16]:>16} {header['actual_mm']:>7} {meas:>5} {header['target']:>6} "
                  f"{header['range_mm']:>9} {header['amp']:>6}  {result}")

    print(f"\nwrote {len(found)} measurement(s) to {outdir}/ (summary in {manifest_path.name})")


if __name__ == "__main__":
    main()
