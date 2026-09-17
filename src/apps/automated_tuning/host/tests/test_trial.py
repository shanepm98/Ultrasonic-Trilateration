import math

import pytest

from serial_link import TelemetryFrame
from trial import compute_trial_result, passes_gate, score


def _frame(range_mm, amp=3000, target=True, seq=0):
    return TelemetryFrame(meas_seq=seq, have_target=target, range_q5=int(range_mm * 32), amplitude=amp)


def test_good_trial_tight_cluster_passes():
    true_dist = 3000.0
    frames = [_frame(true_dist + d) for d in [-5, -3, 0, 2, 4, -2, 1, 3, -4, 0, 1, -1, 2, -2, 0]]
    result = compute_trial_result(frames, true_dist)
    assert result.detection_rate == 1.0
    assert result.outlier_rate == 0.0
    assert abs(result.error_median_mm) < 5
    passed, warnings = passes_gate(result, true_dist)
    assert passed, warnings
    assert score(result) < 1.0


def test_outlier_detection_flags_ringdown_false_lock_without_corrupting_inlier_stats():
    true_dist = 3000.0
    frames = [_frame(true_dist) for _ in range(9)] + [_frame(50, amp=5000)]  # a near-field false lock
    result = compute_trial_result(frames, true_dist)
    assert result.outlier_rate == pytest.approx(0.1)
    assert abs(result.error_median_mm) < 1  # the outlier must not pull the inlier median


def test_no_detections_fails_gate_and_scores_worst():
    frames = [_frame(0, amp=0, target=False) for _ in range(10)]
    result = compute_trial_result(frames, 3000.0)
    assert result.detection_rate == 0.0
    assert math.isnan(result.error_median_mm)
    passed, warnings = passes_gate(result, 3000.0)
    assert not passed
    assert any("detection_rate" in w for w in warnings)
    assert math.isinf(score(result))


def test_compute_trial_result_requires_at_least_one_frame():
    with pytest.raises(ValueError):
        compute_trial_result([], 3000.0)


def test_gate_bias_tolerance_scales_with_distance():
    # 40mm bias is under the 50mm floor at 1m, and well under 2%*10000=200mm at 10m.
    close = [_frame(1000 + 40) for _ in range(10)]
    far = [_frame(10000 + 40) for _ in range(10)]
    passed_close, _ = passes_gate(compute_trial_result(close, 1000.0), 1000.0)
    passed_far, _ = passes_gate(compute_trial_result(far, 10000.0), 10000.0)
    assert passed_close
    assert passed_far


def test_gate_rejects_bias_exceeding_the_floor():
    frames = [_frame(1000 + 80) for _ in range(10)]  # 80mm bias > max(50, 2%*1000=20)
    passed, warnings = passes_gate(compute_trial_result(frames, 1000.0), 1000.0)
    assert not passed
    assert any("error_median_mm" in w for w in warnings)
