# Automatic tuning algorithm

Design for the *automatic* mode of `host/tuner.py` (not yet written): given a fixed physical
distance between the transmitter and receiver (user-supplied ground truth), find a sensor
configuration that reliably and accurately reports that distance, using the RPC opcodes defined
in `../include/at_protocol.h` / mirrored in `protocol.py`.

*Manual* mode needs no algorithm — it's direct user-driven RPC calls plus a live telemetry
printout. It should reuse this design's trial-statistics code (§1) for its live display, but has
no search/scoring logic of its own.

This document is the algorithm specification; `tuner.py` should be implementable directly from
it once hardware is available to validate the search against. Every numeric default here is a
starting point grounded in TDK's AN-000175 (SonicLib Programmer's Guide) and this repo's existing
bring-up code, not something bench-validated yet — see the flagged assumptions inline.

## Why a staged, physically-informed search, not a generic optimizer

The full joint parameter space (ODR × TX cycles × pulse_width × phase × gain_reduce × atten ×
ringdown/static-filter samples × 8 threshold levels) is far too large for blind grid/Bayesian/
genetic search when every trial costs real wall-clock time on physical hardware (~1-3s per trial
at the receiver's fixed ~10Hz telemetry rate, `AT_TRIGGER_INTERVAL_MS` in `at_common.h`) — even a
3-point grid across 7 axes is ~2000 trials (hours). But AN-000175 establishes that most of these
parameters are individually monotonic or saturating: more gain, more TX energy, more filtering
each has one clear beneficial direction until diminishing returns, and only `phase` behaves
non-monotonically. There are exactly two real cross-parameter couplings: TX `num_cycles` must
equal the receiver's count-segment cycles (a hard correctness constraint, not a tuning axis of
its own — AN-000175 §5.4), and threshold-zone placement depends on where the target's echo is
expected to land, which depends on ODR. A coordinate-wise staged search exploits this directly:
tune one physically-motivated group at a time, holding the rest fixed, converging in an estimated
**~2-2.5 minutes of trial time per calibration distance** instead of hours for a joint search.

## 1. Trial protocol

Every configuration under test is evaluated by collecting telemetry frames
(`at_telemetry_t`: `meas_seq`, `have_target`, `range_q5` mm×32, `amplitude`) and reducing them to
a small set of statistics against the user-supplied `true_distance_mm`.

- **Warm-up discard**: drop the first **3** frames after any reconfiguration (`AT_OP_SET_MODE`).
  The ESP-NOW config relay to the transmitter is best-effort/asynchronous by design (a dropped
  packet just means the transmitter keeps running last iteration's config one cycle longer — see
  `at_protocol.h`'s own design note), so the first frame or two after a change may still reflect
  the transmitter's *previous* configuration.
- **Screening trial**: N=10 frames (~1.3s including warm-up) — cheap, used for coarse/early-stage
  sweeps to reject obviously-bad candidates fast.
- **Confirmation trial**: N=30 frames (~3.3s) — used to accept a stage's winner, and for the
  final overall result. At detection rate p≈0.9, N=30 gives a 95% CI half-width of about ±9
  percentage points (`1.96·√(0.9·0.1/30) ≈ 0.107`), tight enough to separate "clearly good"
  (>90%) from "clearly bad" (<70%) without spending excessive wall-clock time on every candidate.

Per trial, compute:

```
detection_rate = count(have_target) / N

range_mm[i]  = range_q5[i] / 32.0                    for each detected frame i
error_mm[i]  = range_mm[i] - true_distance_mm

outlier_threshold_mm = max(300, 0.15 * true_distance_mm)
is_outlier[i]        = have_target[i] and abs(error_mm[i]) > outlier_threshold_mm
outlier_rate         = count(is_outlier) / N

# inliers = detected AND NOT outlier - the outlier-rate metric is scored separately (§2) so a
# handful of ringdown false-positives don't corrupt the "true" accuracy read of good detections.
error_median_mm = median(error_mm[i] for i in inliers)
error_mad_mm    = median(abs(error_mm[i] - error_median_mm) for i in inliers)
error_p95_abs_mm = 95th_percentile(abs(error_mm[i]) for i in inliers)

amplitude_median = median(amplitude[i] for i in inliers)   # health signal only, see below
```

`amplitude_median` is never compared against an absolute number — AN-000175 §4.3 states
amplitude is an internal, uncalibrated unit with expected part-to-part variation. It's used only
as a *relative* signal within a single search run (e.g. "did increasing TX energy actually help
amplitude on this unit").

```python
@dataclass
class TrialResult:
    n_frames: int
    n_detected: int
    detection_rate: float
    error_median_mm: float
    error_mad_mm: float
    error_p95_abs_mm: float
    outlier_rate: float
    amplitude_median: float
```

## 2. Acceptance gate and ranking

A configuration **passes** iff *all four* hold — a hard AND, not a weighted average, because a
config that's accurate-but-unreliable or reliable-but-biased is not usable either way and there's
no tradeoff a downstream trilateration consumer can make for a config that fails outright:

| Criterion | Default | Why |
|---|---|---|
| `detection_rate` | ≥ 0.90 | Matches the N=30 CI reasoning in §1 |
| `outlier_rate` | ≤ 0.05 | Spurious ringdown-range hits corrupt downstream trilateration; near-zero tolerance since these are categorically false locks, not ordinary jitter |
| `\|error_median_mm\|` | ≤ max(50, 0.02 × true_distance_mm) | 2% of range or 50mm floor — typical ultrasonic ranging accuracy expectation; floor avoids being unrealistically tight at short distances |
| `error_mad_mm` | ≤ max(30, 0.01 × true_distance_mm) | Jitter tolerance set tighter than bias tolerance — jitter reflects SNR/margin quality more directly than bias, which can be a fixable calibration artifact |

Ranking (used only to pick a winner among candidates within/across stages, never as the
accept/reject decision itself):

```
score = abs(error_median_mm) / 50.0
      + error_mad_mm / 30.0
      + 5 * outlier_rate
      + 3 * (1 - detection_rate)
```

Detection failure and outlier presence are weighted far above jitter/bias magnitude (5×, 3×),
matching the gate's own priority order — a config's score is only meaningful for comparing
candidates, not for deciding pass/fail on its own.

## 3. Staged search order

Each stage takes the previous stage's winner as its fixed starting point (coordinate descent).
Seed values (the starting point for stage 1) come from this repo's existing, already-used-but-
never-bench-validated bring-up defaults (`pitch_catch_mode_hardware_test/receiver/main/
receiver_loop.c`): ODR=default, TX pulse_width=3/phase=8/80µs burst, RX gain_reduce=0/atten=0,
ringdown_cancel_samples=20, static_filter_samples=0,
thresholds `{0:2500, 30:1200, 60:700, 120:450, 200:350}`.

Stages 4-6 repeat once more after stage 6 (capped at 2 total passes; skip the 2nd pass if the 1st
changed nothing), since threshold levels interact weakly with gain settings chosen before they
were known.

### Stage 1 — ODR
```
candidates = [CH_ODR_FREQ_DIV_8 (default), CH_ODR_FREQ_DIV_4, CH_ODR_FREQ_DIV_16]
for odr in candidates:
    configure(odr=odr, <baseline TX/RX/threshold seed>)
    result = run_screening_trial(N=10)
    if result passes acceptance gate: break   # keep first that works, don't over-search
best_odr = last configured odr (even if none strictly passed, by score, for downstream stages)
```
Fixed 3-candidate ordered try, not exhaustive — ODR is a coarse structural choice (AN-000175
§5.3: "increasing ODR by one step increases noise by a factor of √2"), and it must be fixed
before anything sample-index-dependent (thresholds, ringdown sizing) is meaningful.

### Stage 2 — TX drive strength (`num_cycles`, `pulse_width`)
```
pulse_width = 3            # seed value, relative amplitude 0.924 (of max 1.0 at pulse_width=4)
num_cycles  = min_cycles_to_saturate(best_odr)   # AN-000175 Table 5, per-ODR lookup; a fixed
                                                  # starting point, not searched from zero
for i in range(3):
    result = run_screening_trial(N=10)
    if amplitude_median shows comfortable headroom above the lowest active threshold level:
        break                        # saturates - AN-000175 explicit that gains diminish fast
    elif pulse_width < 4:
        pulse_width += 1
    else:
        num_cycles = min(num_cycles * 1.5, MAX_TX_CYCLES)
    update_receiver_count_segment(num_cycles)   # HARD CONSTRAINT, AN-000175 §5.4 - the count
                                                 # segment must always match the TX cycle count
```
Capped at 3 iterations: a monotonic "increase until the plateau is directly observed" line
search, not an open-ended optimizer, since the docs are explicit that returns diminish fast.

### Stage 3 — TX `phase`
```
coarse = [0, 4, 8, 12]
for phase in coarse:
    result = run_screening_trial(N=10)
    record amplitude_median
best_coarse = argmax(amplitude_median) among coarse
for phase in [best_coarse - 2, best_coarse - 1, best_coarse + 1, best_coarse + 2]:  # local refine
    result = run_screening_trial(N=10)
    record amplitude_median
best_phase = argmax(amplitude_median) overall, tie-break by detection_rate
```
≤8 trials total. `phase` has "no stated monotonic effect... appears to be a resonance-alignment
parameter" (AN-000175 §5.3) — a coarse quarter-step probe plus local refine is appropriate; never
exhaustively sweep all 16 values.

### Stage 4 — RX `gain_reduce` / `atten`, two-segment restructure
Adopt a **two-RX-segment layout** on the receiver (COUNT[matched to TX] → RX_near[attenuated,
first K samples] → RX_far[normal gain, remaining samples]) per AN-000175 §5.5's stated standard
pattern ("often, reduced gain settings are used for a small number of samples very close to the
sensor... to limit the effect of ringdown artifacts") — this is a structural upgrade from the
current single-RX-segment bring-up code, not something to discover via search.

```
K_near_samples = min(ringdown_seed_estimate, expected_target_sample_index - margin)  # see §4b/Stage 5

# atten: near segment only
atten = 0
while atten < 3:
    result = run_screening_trial(N=10)
    if near-field outliers (detections landing well below expected_target_sample_index) absent:
        break
    atten += 1

# gain_reduce: far/target segment only, fixed 5-point sweep (28-31 all ≈ equal per AN-000175 §5.5)
for gain_reduce in [0, 8, 16, 24, 28]:
    result = run_screening_trial(N=10)
    record (passes_gate, amplitude_median)
best_gain_reduce = argmax(amplitude_median) among candidates where passes_gate
```
5-8 trials: gain_reduce's effect (less reduction → more gain → more amplitude, until clipping) is
monotonic-ish with no evidence of a sharp interior optimum, so a coarse sweep + "pick best
passing" suffices — the actual clipping/saturation risk is what the `atten` search on the near
segment already isolates.

### Stage 5 — `ringdown_cancel_samples` / `static_filter_samples`
```
# CRITICAL GUARD: since the calibration target is stationary, ringdown-cancel/static-filter
# zones must never extend far enough to cover the target's own expected sample position - doing
# so would suppress the real target exactly like clutter (AN-000175 §4.10, §4.8).
safety_margin        = max(10, 0.20 * expected_target_sample_index)
max_safe_filter_len  = max(0, expected_target_sample_index - safety_margin)

ringdown_cancel_samples = seed (20)
while ringdown_cancel_samples < max_safe_filter_len:
    result = run_screening_trial(N=10)
    if near-field ringdown outliers absent: break
    ringdown_cancel_samples = min(ringdown_cancel_samples * 2, max_safe_filter_len)
# if the cap is reached without eliminating ringdown outliers, STOP - do not encroach further;
# Stage 6's near-field threshold zone is the designed fallback.

static_filter_samples = 0   # seed; only try one step (e.g. ringdown_cancel_samples / 2, capped
                             # the same way) if ringdown_cancel_samples alone didn't clear outliers
```
`expected_target_sample_index` is computed in Stage 6 (§4) but must be available before Stage 5
runs — compute it once, immediately after Stage 1 fixes the ODR (it depends only on ODR and the
known true distance), and refine it with Stage 6's empirical correction before Stage 5's final
pass in the repeat loop.

### Stage 6 — Threshold zones
See §4 below — the only stage that encodes the target's known distance into the configuration.

### Stage 7 — Final confirmation
One N=30 confirmation trial on the overall winner from stages 1-6 (post any repeat passes)
before accept/save.

**Wall-clock budget** (screening ≈1.3s, confirmation ≈3.3s): stage 1 ≈4s, stage 2 ≈4s, stage 3
≈10s, stage 4 ≈13s, stage 5 ≈8s, stage 6 ≈25s (see §4), one repeat of stages 4-6 ≈45s, final
confirmation ≈3s → **total ≈2-2.5 minutes per calibration distance**. This is the concrete
argument for staging over a joint search: a 3-point grid across the same 7 axes would be
3⁷≈2187 trials (hours), not minutes.

## 4. Threshold-zone placement (Stage 6 detail)

### 4a. Expected sample index

```
effective_sample_rate_hz = op_freq_hz >> (7 - odr_shift)
# op_freq_hz: read via AT_OP_GET_STATUS -> at_resp_get_status_t.op_freq_hz (= ch_get_frequency())
# odr_shift: the CH_ODR_* enum value the host itself configured via AT_OP_MEAS_INIT/AT_OP_SET_ODR
#            (CH_ODR_FREQ_DIV_8=4 -> divisor 2^(7-4)=8, CH_ODR_FREQ_DIV_2=6 -> divisor 2, etc. -
#            confirmed self-consistent with soniclib.h's ch_odr_t enum comments)

expected_target_sample_index = round(
    true_distance_mm * effective_sample_rate_hz / (SPEED_OF_SOUND_MPS * 1000)
)
# SPEED_OF_SOUND_MPS default 343 (20degC air); expose as a CLI override since bench conditions
# vary and this isn't temperature-compensated.
#
# NOTE: deliberately NOT the same formula as SonicLib's own ch_mm_to_samples()/
# ch_common_meas_samples_to_mm() - those bake in a round-trip /2 that only applies when
# range_type == CH_RANGE_ECHO_ONE_WAY (confirmed in ch_common.c's ch_common_range_lsb_to_mm():
# the halving is explicitly gated on that enum value and never applied for CH_RANGE_DIRECT).
# Pitch-catch here uses CH_RANGE_DIRECT (one-way), so no halving belongs in this formula.
```

**This is a prior, not ground truth.** It can't account for per-device runtime calibration state
(`rtc_cal_result`, `pmut_clock_fcount`, RX pretrigger offsets) that only exists on the sensor
itself and isn't derivable from static source. So Stage 6 opens with an empirical self-check
before trusting it:

```
# Stage 6, step 0: empirical calibration check
configure a single maximally-permissive threshold zone (start_sample=0, a low level - e.g. half
    the seed's zone-0 level) spanning the whole active range
result = run_screening_trial(N=10)
# the real target should be trivially found with everything wide open
observed_range_mm = median(range_mm for detected inlier frames)

if abs(observed_range_mm - true_distance_mm) is small (within ~1 zone-width in mm):
    proceed with expected_target_sample_index as computed above
    expected_target_sample_index_source = "formula"
else:
    # range_q5 is the firmware's own calibrated output - trust it over the reverse formula
    expected_target_sample_index = round(
        expected_target_sample_index * true_distance_mm / observed_range_mm
    )
    expected_target_sample_index_source = "corrected_from_calibration_trial"
    # log a warning: the analytical estimate needed hardware correction
```

### 4b. Zone layout

Of the 8 available threshold zones (`AT_NUM_THRESHOLDS` / `CH_NUM_THRESHOLDS`), only 4 are used;
the rest are left `{start_sample: 0, level: 0}` matching this repo's existing convention for
unused trailing entries:

| Zone | `start_sample` | `level` | Role |
|---|---|---|---|
| 0 | 0 | high, fixed (conservative multiplier of zone 2's found level, e.g. ×2-3) | Reject ringdown; doesn't need to detect anything, since AN-000175 §8.5 notes direct-path signal is normally much stronger than a reflection |
| 1 | just before the ringdown-cancel/static-filter boundary from Stage 5 | moderate, fixed (e.g. ×1.5 of zone 2) | Transition/guard band - avoids a sharp level discontinuity right at a boundary, which could itself create a borderline detection artifact |
| **2 (target zone)** | `expected_target_sample_index - 15%` | **searched** (see below) | The only zone whose level is actually searched — covers the target's likely position with margin for the estimate's own uncertainty |
| 3 | `expected_target_sample_index + 15%` | moderate-high, fixed (e.g. ×1.5-2 of zone 2) | Beyond-target - guards against multipath/clutter arriving after the direct path; no real target expected here in a fixed-distance calibration |

Width of zone 2 (±15% of `expected_target_sample_index`) deliberately errs wide: being wide costs
a slightly higher false-positive risk from the searched level, but being narrow risks missing the
real target entirely if the sample-index prior was off. Zone 0/1/3 levels are fixed multipliers
off zone 2's found level rather than independently searched — a scope simplification to keep the
stage's trial count bounded; flagged as a place a future iteration could add a secondary,
lower-cost search if bench data shows these fixed multipliers don't hold for a given unit.

### 4c. Searching zone 2's level

```
candidates = [200, 350, 500, 700, 1000, 1500, 2500]   # matches the seed table's existing range
for level in candidates:   # ascending: most sensitive first
    set zone 2's level; run_screening_trial(N=10)
    if result passes the acceptance gate (§2): break   # take the FIRST (most sensitive) passer
# if none pass: keep the candidate with the best score (§2); this stage's contribution to the
# final pass/fail is "did not pass" and propagates to the overall result (§5)
```
Prefer the most sensitive *passing* level, not the most conservative — pitch-catch direct-path
SNR is normally generous (AN-000175 §8.5), so a more sensitive threshold gives better margin
against unit-to-unit/environmental amplitude variation than an overly conservative one would.

## 5. Stopping / failure handling

The pipeline is finite by construction — "convergence" means pipeline completion (§3's bounded
stage trial counts) plus the final confirmation trial (Stage 7). The only open-ended element is
the capped 2-pass repeat of stages 4-6: stop after pass 2 regardless, and stop after pass 1
already if its score doesn't differ from a hypothetical pass 2 by more than 2% (i.e., skip a
pass that clearly won't move the needle).

**No hard failure if nothing passes the gate.** Save the best-scoring config found across the
whole search (by §2's ranking formula, computed even for failing configs) to
`configs/<N>meter.config`, with `"passed_acceptance_gate": false` and a `"warnings"` list naming
which specific criteria failed and by how much (e.g. `"detection_rate 0.72 < required 0.90"`).
This is a bring-up tool for physical hardware — a hard failure with no artifact would force a
from-scratch re-run of a multi-minute search even though the best-effort result is almost always
still diagnostically useful, and it matches the README's own framing ("save the best
configuration values found so far").

**Early abort** (don't spend the full multi-minute budget) if Stage 1's very first screening
trial shows catastrophically low detection (<10%) — that's almost certainly a physical
setup/wiring problem (boards not paired, wrong distance, dead transducer), not a tuning-parameter
problem, and the tool should say so distinctly rather than grinding through six more stages that
can't fix a hardware fault.

## 6. Output format — `configs/<N>meter.config`

```json
{
  "schema_version": 1,
  "protocol_version": 1,
  "created_at": "2026-09-16T21:00:00Z",
  "true_distance_mm": 3000,
  "speed_of_sound_mps": 343,
  "passed_acceptance_gate": true,
  "warnings": [],

  "measurement": { "meas_num": 0, "odr": 4, "meas_period": 0, "mode": 0 },

  "segments": [
    { "type": "count", "num_cycles": 27, "int_enable": 0 },
    { "type": "rx", "num_samples": 20, "gain_reduce": 0, "atten": 2, "int_enable": 0 },
    { "type": "rx", "num_samples": 480, "gain_reduce": 8, "atten": 0, "int_enable": 1 }
  ],
  "tx_segment": { "num_cycles": 27, "pulse_width": 3, "phase": 9, "int_enable": 1 },
  "max_range_mm": 5000,

  "algo": {
    "ringdown_cancel_samples": 20,
    "static_filter_samples": 0,
    "iq_output_format": 0,
    "num_ranges": 1,
    "filter_update_interval": 0
  },
  "thresholds": [
    { "start_sample": 0,   "level": 2000 },
    { "start_sample": 15,  "level": 1200 },
    { "start_sample": 250, "level": 550 },
    { "start_sample": 330, "level": 1600 },
    { "start_sample": 0, "level": 0 },
    { "start_sample": 0, "level": 0 },
    { "start_sample": 0, "level": 0 },
    { "start_sample": 0, "level": 0 }
  ],

  "expected_target_sample_index": 290,
  "expected_target_sample_index_source": "corrected_from_calibration_trial",

  "performance": {
    "detection_rate": 0.97,
    "outlier_rate": 0.0,
    "error_median_mm": 12.3,
    "error_mad_mm": 8.1,
    "error_p95_abs_mm": 22.4,
    "amplitude_median": 3140,
    "n_frames_confirmation_trial": 30
  },

  "search_metadata": {
    "total_trials_run": 47,
    "total_search_wall_clock_s": 132.4
  }
}
```

`tx_segment` and `segments[]` are kept separate even though `segments[0].num_cycles` (the
receiver's count segment) must equal `tx_segment.num_cycles` — this makes the coupling explicit
and machine-checkable at load time (`assert segments[0]["num_cycles"] ==
tx_segment["num_cycles"]`) rather than silently duplicated data that could drift if hand-edited.
`expected_target_sample_index_source` records whether §4a's formula needed the empirical
correction — useful for diagnosing whether the sample↔mm formula assumption holds across units
over time. This schema contains every value needed to replay the exact opcode sequence
(`AT_OP_MEAS_RESET → AT_OP_MEAS_INIT → AT_OP_ADD_SEGMENT_* (once per entry in segments[], plus
once on the transmitter for tx_segment) → AT_OP_MEAS_WRITE_CONFIG → AT_OP_SET_MAX_RANGE(_MEAS) →
AT_OP_SET_THRESHOLDS → AT_OP_GPT_ALGO_CONFIGURE → AT_OP_SET_MODE`) with no re-derivation.

## 7. `ch_meas_optimize()` — deferred, not in v1

SonicLib's automatic ringdown-dampening TX-segment optimizer (`ch_meas_optimize()`) isn't exposed
by any opcode in `at_protocol.h` today. **Decision: defer as documented future work.**

- It requires a new opcode plus firmware changes on both `at_protocol.h` and the receiver
  (`rpc_dispatch.c`/`at_sensor_config.c`) — real scope beyond algorithm design, and every new
  opcode is a two-file (C header + `protocol.py`), hand-synced change per the protocol's own
  design (no codegen).
- It costs ~250ms per call and is explicitly documented as unsafe to call repeatedly on an
  already-optimized measurement queue — each call must be fed the *original* pre-optimization
  queue (via `ch_meas_get_queue()` or the combined import+optimize entry point), which would add
  real state-management complexity to a v1 whose primary goal is establishing the coordinate-wise
  search loop itself.
- Stages 4-6 above already achieve the same practical goal (ringdown suppression) through
  existing knobs: the two-segment RX gain/atten split, the `ringdown_cancel_samples`/
  `static_filter_samples` search, and the near-field threshold zone.

If bench validation later shows this manual/coordinate-wise approach insufficient for some
unit/distance combination, add `AT_OP_MEAS_OPTIMIZE` as a v2 opcode, called once after Stage 2
(TX energy settled, so the optimizer dampens the actual drive configuration that will be used)
and before Stage 4 (so RX gain tuning happens against the auto-inserted dampening segments,
not before them) — always fed from a host-tracked copy of the pre-optimization queue.
