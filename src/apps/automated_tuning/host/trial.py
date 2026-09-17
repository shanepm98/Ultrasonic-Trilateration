"""Trial statistics, acceptance gate, and ranking score - tuning_algorithm.md sections 1-2.

All pure functions: no serial I/O, so they're fully testable against synthetic telemetry
without hardware. serial_link.TelemetryFrame is the input type; see tuning_search.py for the
code that actually collects frames from a live SerialLink and calls into this module.
"""

from __future__ import annotations

import math
import statistics
from dataclasses import dataclass
from typing import List, Tuple

from serial_link import TelemetryFrame

# tuning_algorithm.md §2 default acceptance-gate thresholds.
DETECTION_RATE_MIN = 0.90
OUTLIER_RATE_MAX = 0.05


@dataclass
class TrialResult:
    n_frames: int
    n_detected: int
    detection_rate: float
    error_median_mm: float  # NaN if no inlier detections
    error_mad_mm: float  # NaN if no inlier detections
    error_p95_abs_mm: float  # NaN if no inlier detections
    outlier_rate: float
    amplitude_median: float  # NaN if no inlier detections; uncalibrated unit, relative use only


def _percentile(values: List[float], pct: float) -> float:
    """Linear-interpolation percentile (matches numpy's default 'linear' method)."""
    if not values:
        return float("nan")
    s = sorted(values)
    k = (len(s) - 1) * (pct / 100.0)
    lo, hi = math.floor(k), math.ceil(k)
    if lo == hi:
        return s[int(k)]
    return s[lo] * (hi - k) + s[hi] * (k - lo)


def compute_trial_result(frames: List[TelemetryFrame], true_distance_mm: float) -> TrialResult:
    """tuning_algorithm.md §1. `frames` should already have the warm-up frames discarded."""
    n_frames = len(frames)
    if n_frames == 0:
        raise ValueError("compute_trial_result requires at least one frame")

    detected = [f for f in frames if f.have_target]
    n_detected = len(detected)
    detection_rate = n_detected / n_frames

    outlier_threshold_mm = max(300.0, 0.15 * true_distance_mm)
    errors = [(f, f.range_mm - true_distance_mm) for f in detected]
    inliers = [(f, e) for f, e in errors if abs(e) <= outlier_threshold_mm]
    n_outliers = len(errors) - len(inliers)
    outlier_rate = n_outliers / n_frames

    if inliers:
        inlier_errors = [e for _, e in inliers]
        error_median_mm = statistics.median(inlier_errors)
        error_mad_mm = statistics.median(abs(e - error_median_mm) for e in inlier_errors)
        error_p95_abs_mm = _percentile([abs(e) for e in inlier_errors], 95)
        amplitude_median = statistics.median(f.amplitude for f, _ in inliers)
    else:
        error_median_mm = error_mad_mm = error_p95_abs_mm = amplitude_median = float("nan")

    return TrialResult(
        n_frames=n_frames,
        n_detected=n_detected,
        detection_rate=detection_rate,
        error_median_mm=error_median_mm,
        error_mad_mm=error_mad_mm,
        error_p95_abs_mm=error_p95_abs_mm,
        outlier_rate=outlier_rate,
        amplitude_median=amplitude_median,
    )


def passes_gate(result: TrialResult, true_distance_mm: float) -> Tuple[bool, List[str]]:
    """tuning_algorithm.md §2's four-criterion hard AND. Returns (passed, warnings) - warnings
    names every failing criterion (not just the first), for a useful "warnings" list in the
    saved config JSON even on failure."""
    bias_tolerance_mm = max(50.0, 0.02 * true_distance_mm)
    jitter_tolerance_mm = max(30.0, 0.01 * true_distance_mm)

    warnings: List[str] = []
    if result.detection_rate < DETECTION_RATE_MIN:
        warnings.append(
            f"detection_rate {result.detection_rate:.2f} < required {DETECTION_RATE_MIN:.2f}"
        )
    if result.outlier_rate > OUTLIER_RATE_MAX:
        warnings.append(f"outlier_rate {result.outlier_rate:.2f} > allowed {OUTLIER_RATE_MAX:.2f}")
    # `not (x <= y)` rather than `x > y`: NaN (no inlier detections at all) must fail the gate,
    # and NaN comparisons are always False either way, so `not (nan <= y)` correctly evaluates
    # to True (fails) while `nan > y` would incorrectly evaluate to False (silently "passes").
    if not (abs(result.error_median_mm) <= bias_tolerance_mm):
        warnings.append(
            f"|error_median_mm| {abs(result.error_median_mm):.1f} > allowed {bias_tolerance_mm:.1f}"
        )
    if not (result.error_mad_mm <= jitter_tolerance_mm):
        warnings.append(f"error_mad_mm {result.error_mad_mm:.1f} > allowed {jitter_tolerance_mm:.1f}")

    return (len(warnings) == 0, warnings)


def score(result: TrialResult) -> float:
    """tuning_algorithm.md §2 composite ranking score - lower is better. Only used to rank
    candidates against each other, never as the accept/reject decision (that's passes_gate).
    Returns +inf for a result with no inlier detections at all, so it always ranks last in any
    min()/sorted() comparison rather than propagating NaN (which compares False against
    everything and would sort unpredictably)."""
    if math.isnan(result.error_median_mm):
        return math.inf
    return (
        abs(result.error_median_mm) / 50.0
        + result.error_mad_mm / 30.0
        + 5.0 * result.outlier_rate
        + 3.0 * (1.0 - result.detection_rate)
    )
