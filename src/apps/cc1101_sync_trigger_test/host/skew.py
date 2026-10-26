"""Leading falling-edge skew between two receivers' GDO2 traces.

Used by skew_sweep.py on live scope captures, and standalone on two exported CSVs (columns t_s, v):

    python skew.py c1.csv c2.csv

Skew is CH2 - CH1 at the first falling crossing of THRESHOLD_V in each trace, linearly
interpolated between samples. A shot is only valid when both traces start the record in the same
logic state: with a steady carrier each receiver holds its last level and drifts back on its own
after a burst, and a receiver that starts in the wrong state puts its first falling edge on a later
bit.
"""

import csv
import sys

import numpy as np

THRESHOLD_V = 1.65  # half of the 3.3 V GDO2 swing


def load_csv(path):
    """Load an exported trace (t_s, v), skipping any header line."""
    t, v = [], []
    with open(path) as f:
        for row in csv.reader(f):
            try:
                t.append(float(row[0]))
                v.append(float(row[1]))
            except (ValueError, IndexError):
                pass
    return np.asarray(t), np.asarray(v)


def first_falling(t, v, threshold=THRESHOLD_V):
    """Time of the first high->low crossing of threshold, or None."""
    hi = v >= threshold
    idx = np.flatnonzero(hi[:-1] & ~hi[1:])
    if idx.size == 0:
        return None
    i = idx[0]
    a, b = v[i], v[i + 1]
    return t[i] + (threshold - a) / (b - a) * (t[i + 1] - t[i])


def leading_skew(t1, v1, t2, v2, threshold=THRESHOLD_V):
    """Return (status, skew_s, f1_s, f2_s, pre1, pre2).

    status is 'ok', 'invalid_prestate', 'ch1_missing' or 'ch2_missing'. pre1/pre2 are the logic
    states ('H'/'L') at the start of each record.
    """
    pre1 = "H" if v1[0] >= threshold else "L"
    pre2 = "H" if v2[0] >= threshold else "L"
    f1 = first_falling(t1, v1, threshold)
    f2 = first_falling(t2, v2, threshold)
    if pre1 != pre2:
        status = "invalid_prestate"
    elif f1 is None:
        status = "ch1_missing"
    elif f2 is None:
        status = "ch2_missing"
    else:
        status = "ok"
    skew = f2 - f1 if f1 is not None and f2 is not None else None
    return status, skew, f1, f2, pre1, pre2


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    t1, v1 = load_csv(argv[1])
    t2, v2 = load_csv(argv[2])
    status, skew, f1, f2, pre1, pre2 = leading_skew(t1, v1, t2, v2)
    fmt = lambda x: "none" if x is None else f"{x * 1e6:+.3f} us"
    print(f"{status}  pre C1 {pre1} C2 {pre2}  C1 F {fmt(f1)}  C2 F {fmt(f2)}  skew C2-C1 {fmt(skew)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
