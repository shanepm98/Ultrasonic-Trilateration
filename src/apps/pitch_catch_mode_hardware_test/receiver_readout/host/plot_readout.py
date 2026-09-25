#!/usr/bin/env python3
"""Render a receiver_readout I/Q recording (CSV) as an interactive HTML waveform plot.

Standard library only - no venv/requirements.txt needed, unlike automated_tuning/host/.

Input CSV: one row per sample, with columns for the sample index (optional - inferred
from row order if absent) and the raw I/Q pair. A header row is auto-detected; recognized
column names (case-insensitive) are idx/index/sample for the index and i/q for the
quadrature components. Both the on-device console format (`idx,i,q`, as printed by
receiver_readout's IQ_BEGIN/IQ_END dump) and a plain headerless `i,q` export work.

Usage:
    ./plot_readout.py recording.csv
    ./plot_readout.py recording.csv -o out.html --open
    ./plot_readout.py recording.csv --target 1 --range-mm 193.2 --amp 4667
    ./plot_readout.py recording.csv --op-freq 84930   # exact reading -> exact distance axis

The ringdown-cancel window and detection thresholds default to the values currently
configured in receiver_readout/main/readout_loop.c (RX_RINGDOWN_CANCEL_SAMPLES=20,
rx_thresholds); override with --ringdown-samples / --threshold if that firmware config
has since been re-tuned.

Sample index is converted to one-way distance using SonicLib's own formula
(ch_common_samples_to_mm() in ch_common.c): mm = samples * 343 * 1000 * 2^(7-odr) / (op_freq * 2).
--odr defaults to 4 (CH_ODR_FREQ_DIV_8 / CH_ODR_DEFAULT, used throughout this project via
PC_ODR in pitch_catch_common.h). --op-freq defaults to 85000 Hz, the operating frequency
observed during this project's own hardware bring-up (docs/hardware_bringup.md; ICU-20201
is rated 70-95 kHz per DS-000478) - it is NOT calibrated to your actual sensor and will be
off by a few percent. For an accurate distance axis, pass the real value: it's logged as
"op frequency" by icu_post's POST output, or "op freq" by receiver_readout's own startup log.
"""

import argparse
import csv
import json
import sys
import webbrowser
from pathlib import Path
from string import Template

DEFAULT_RINGDOWN_SAMPLES = 20
DEFAULT_THRESHOLDS = [(0, 2500), (30, 1200), (60, 700), (120, 450), (200, 350)]
CH_SPEEDOFSOUND_MPS = 343
DEFAULT_ODR = 4  # CH_ODR_FREQ_DIV_8 / CH_ODR_DEFAULT - see soniclib.h
DEFAULT_OP_FREQ_HZ = 85000  # observed during bring-up (docs/hardware_bringup.md); not per-unit calibrated


def parse_threshold_arg(spec):
    try:
        start_s, level_s = spec.split(":", 1)
        return (int(start_s), int(level_s))
    except ValueError:
        raise argparse.ArgumentTypeError(f"threshold must be START:LEVEL, got {spec!r}")


def is_number(s):
    try:
        float(s)
        return True
    except ValueError:
        return False


def load_iq_csv(path):
    with open(path, newline="") as f:
        rows = [r for r in csv.reader(f) if r and any(c.strip() for c in r)]
    if not rows:
        raise SystemExit(f"{path}: no data rows")

    first = rows[0]
    has_header = not all(is_number(c) for c in first)

    if has_header:
        cols = [c.strip().lower() for c in first]

        def find(names):
            for n in names:
                if n in cols:
                    return cols.index(n)
            return None

        i_col = find(["i", "i_raw", "iq_i"])
        q_col = find(["q", "q_raw", "iq_q"])
        if i_col is None or q_col is None:
            raise SystemExit(f"{path}: header {first!r} has no recognizable I/Q columns (want i, q)")
        data_rows = rows[1:]
    else:
        # Headerless: idx,i,q if 3+ columns, else i,q
        i_col, q_col = (1, 2) if len(first) >= 3 else (0, 1)
        data_rows = rows

    iq = []
    for r in data_rows:
        iq.append((int(float(r[i_col])), int(float(r[q_col]))))
    return iq


PAGE_TEMPLATE = Template(r"""<title>$title</title>
<style>
  :root {
    color-scheme: light;
    --page:        #f9f9f7;
    --surface-1:   #fcfcfb;
    --text-primary:#0b0b0b;
    --text-secondary:#52514e;
    --text-muted:  #898781;
    --gridline:    #e1e0d9;
    --baseline:    #c3c2b7;
    --border:      rgba(11,11,11,0.10);
    --amp:         #2a78d6;
    --amp-wash:    rgba(42,120,214,0.10);
    --series-i:    #eb6834;
    --series-q:    #1baf7a;
    --status-good: #0ca30c;
    --status-warning: #fab219;
    --status-warning-ink: #7a5200;
    --status-good-ink: #0a5c0a;
    --ringdown-fill: rgba(11,11,11,0.045);
    --ringdown-line: rgba(11,11,11,0.18);
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      color-scheme: dark;
      --page:        #0d0d0d;
      --surface-1:   #1a1a19;
      --text-primary:#ffffff;
      --text-secondary:#c3c2b7;
      --text-muted:  #898781;
      --gridline:    #2c2c2a;
      --baseline:    #383835;
      --border:      rgba(255,255,255,0.10);
      --amp:         #3987e5;
      --amp-wash:    rgba(57,135,229,0.14);
      --series-i:    #d95926;
      --series-q:    #199e70;
      --status-good: #0ca30c;
      --status-warning: #fab219;
      --status-warning-ink: #ffd27a;
      --status-good-ink: #7fe07f;
      --ringdown-fill: rgba(255,255,255,0.06);
      --ringdown-line: rgba(255,255,255,0.22);
    }
  }
  :root[data-theme="dark"] {
    color-scheme: dark;
    --page:        #0d0d0d;
    --surface-1:   #1a1a19;
    --text-primary:#ffffff;
    --text-secondary:#c3c2b7;
    --text-muted:  #898781;
    --gridline:    #2c2c2a;
    --baseline:    #383835;
    --border:      rgba(255,255,255,0.10);
    --amp:         #3987e5;
    --amp-wash:    rgba(57,135,229,0.14);
    --series-i:    #d95926;
    --series-q:    #199e70;
    --status-good: #0ca30c;
    --status-warning: #fab219;
    --status-warning-ink: #ffd27a;
    --status-good-ink: #7fe07f;
    --ringdown-fill: rgba(255,255,255,0.06);
    --ringdown-line: rgba(255,255,255,0.22);
  }

  * { box-sizing: border-box; }
  body {
    margin: 0;
    background: var(--page);
    color: var(--text-primary);
    font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif;
    padding-inline: 20px;
  }
  .mono { font-family: ui-monospace, "SF Mono", "Cascadia Code", Menlo, monospace; }
  .wrap { max-width: 900px; margin: 0 auto; padding-block: 28px 48px; }

  header { margin-bottom: 20px; }
  .eyebrow {
    font-size: 12px; font-weight: 600; letter-spacing: .06em; text-transform: uppercase;
    color: var(--text-muted); margin: 0 0 4px;
  }
  h1 { font-size: 26px; font-weight: 700; margin: 0 0 6px; letter-spacing: -0.01em; }
  .sub { color: var(--text-secondary); font-size: 14px; margin: 0; }

  .stats {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 1px;
    background: var(--border); border: 1px solid var(--border); border-radius: 10px;
    overflow: hidden; margin: 20px 0 24px;
  }
  .stat { background: var(--surface-1); padding: 14px 16px; display: flex; flex-direction: column; gap: 4px; }
  .stat .label { font-size: 11px; color: var(--text-muted); text-transform: uppercase; letter-spacing: .04em; }
  .stat .value { font-size: 20px; font-weight: 600; font-variant-numeric: tabular-nums; }
  .stat .foot { font-size: 12px; color: var(--text-secondary); }
  .tag {
    display: inline-flex; align-items: center; gap: 5px; width: fit-content;
    font-size: 11px; font-weight: 600; padding: 2px 7px 2px 6px; border-radius: 999px;
  }
  .tag::before { content: ""; width: 6px; height: 6px; border-radius: 50%; flex: none; }
  .tag.warn { background: color-mix(in srgb, var(--status-warning) 18%, transparent); color: var(--status-warning-ink); }
  .tag.warn::before { background: var(--status-warning); }
  .tag.good { background: color-mix(in srgb, var(--status-good) 16%, transparent); color: var(--status-good-ink); }
  .tag.good::before { background: var(--status-good); }

  .panel { background: var(--surface-1); border: 1px solid var(--border); border-radius: 10px; padding: 16px 16px 10px; }
  .panel + .panel { margin-top: 2px; border-radius: 0 0 10px 10px; border-top: none; }
  .panel:first-of-type { border-radius: 10px 10px 0 0; }
  .panel-head { display: flex; align-items: baseline; justify-content: space-between; margin-bottom: 4px; }
  .panel-title { font-size: 13px; font-weight: 600; color: var(--text-primary); }
  .legend { display: flex; gap: 14px; font-size: 12px; color: var(--text-secondary); }
  .legend .key { display: inline-flex; align-items: center; gap: 5px; }
  .legend .swatch { width: 10px; height: 2px; border-radius: 1px; display: inline-block; }

  svg { display: block; width: 100%; height: auto; overflow: visible; }
  .axis-label { fill: var(--text-muted); font-size: 10.5px; font-family: ui-monospace, monospace; }
  .grid-line { stroke: var(--gridline); stroke-width: 1; }
  .baseline { stroke: var(--baseline); stroke-width: 1; }

  .chart-wrap { position: relative; }
  .crosshair { stroke: var(--text-muted); stroke-width: 1; stroke-dasharray: 2 3; pointer-events: none; opacity: 0; }
  .hover-dot { pointer-events: none; opacity: 0; }
  .hover-rect { fill: transparent; cursor: crosshair; }

  .tooltip {
    position: absolute; pointer-events: none; opacity: 0; transition: opacity .08s;
    background: var(--text-primary); color: var(--page);
    font-family: ui-monospace, monospace; font-size: 11.5px; line-height: 1.5;
    padding: 6px 9px; border-radius: 6px; white-space: nowrap; z-index: 5;
    box-shadow: 0 4px 14px var(--border);
  }
  .tooltip b { font-weight: 600; }

  figcaption, .note { font-size: 12.5px; color: var(--text-secondary); margin: 10px 0 0; max-width: 68ch; }
  .note b { color: var(--text-primary); font-weight: 600; }

  details { margin-top: 22px; }
  summary {
    cursor: pointer; font-size: 13px; font-weight: 600; color: var(--text-secondary);
    padding: 8px 0; list-style: none; display: flex; align-items: center; gap: 6px;
  }
  summary::-webkit-details-marker { display: none; }
  summary::before { content: "▸"; font-size: 10px; color: var(--text-muted); transition: transform .12s; }
  details[open] summary::before { transform: rotate(90deg); }
  .table-wrap { max-height: 320px; overflow-y: auto; border: 1px solid var(--border); border-radius: 8px; margin-top: 6px; }
  table { width: 100%; border-collapse: collapse; font-family: ui-monospace, monospace; font-size: 12px; }
  thead th {
    position: sticky; top: 0; background: var(--surface-1); text-align: right; padding: 6px 10px;
    color: var(--text-muted); font-weight: 600; border-bottom: 1px solid var(--border); font-size: 10.5px;
    text-transform: uppercase; letter-spacing: .03em;
  }
  tbody td { text-align: right; padding: 3px 10px; font-variant-numeric: tabular-nums; color: var(--text-secondary); }
  tbody tr:nth-child(even) { background: var(--ringdown-fill); }
  tbody td:first-child { color: var(--text-primary); }
  tbody tr.mark td { color: var(--amp); font-weight: 600; }

  footer { margin-top: 26px; padding-top: 14px; border-top: 1px solid var(--border); font-size: 12px; color: var(--text-muted); }
</style>

<div class="wrap">
  <header>
    <p class="eyebrow">pitch_catch_mode_hardware_test / receiver_readout</p>
    <h1>$title</h1>
    <p class="sub">$subtitle</p>
  </header>

  <div class="stats" id="statsRow"></div>

  <div class="chart-wrap" id="chartWrap">
    <div class="panel">
      <div class="panel-head"><span class="panel-title">Amplitude envelope &mdash; &radic;(I&sup2;+Q&sup2;)</span></div>
      <svg id="ampChart" viewBox="0 0 880 260" preserveAspectRatio="none"></svg>
      <figcaption id="ampCaption"></figcaption>
    </div>
    <div class="panel">
      <div class="panel-head">
        <span class="panel-title">Raw quadrature components</span>
        <span class="legend">
          <span class="key"><span class="swatch" style="background:var(--series-i)"></span>I</span>
          <span class="key"><span class="swatch" style="background:var(--series-q)"></span>Q</span>
        </span>
      </div>
      <svg id="iqChart" viewBox="0 0 880 190" preserveAspectRatio="none"></svg>
    </div>
    <div class="tooltip" id="tooltip"></div>
  </div>

  <p class="note" id="secondaryNote" hidden></p>

  <details>
    <summary>Show raw sample table</summary>
    <div class="table-wrap">
      <table>
        <thead><tr><th>idx</th><th>mm</th><th>I</th><th>Q</th><th>amp</th></tr></thead>
        <tbody id="tableBody"></tbody>
      </table>
    </div>
  </details>

  <footer>$footer</footer>
</div>

<script>
(function () {
  "use strict";

  var I = $data_i;
  var Q = $data_q;
  var CFG = $config_json;

  var N = I.length;
  var amp = I.map(function (v, k) { return Math.hypot(v, Q[k]); });

  // ---- primary + secondary peak detection (generic: no per-recording assumptions) ----
  var peakIdx = 0;
  for (var k = 1; k < N; k++) if (amp[k] > amp[peakIdx]) peakIdx = k;
  var peakAmp = amp[peakIdx];

  var MIN_SEPARATION = 15;   // samples away from the primary peak to count as a distinct feature
  var SECONDARY_FRACTION = 0.5; // must reach at least this fraction of the primary peak
  var secondaryIdx = -1, secondaryAmp = 0;
  for (k = 0; k < N; k++) {
    if (Math.abs(k - peakIdx) < MIN_SEPARATION) continue;
    if (amp[k] > secondaryAmp) { secondaryAmp = amp[k]; secondaryIdx = k; }
  }
  var hasSecondary = secondaryIdx >= 0 && secondaryAmp >= SECONDARY_FRACTION * peakAmp;

  // ---- threshold lookup (piecewise step function, per CFG.thresholds) ----
  function thresholdAt(idx) {
    var level = CFG.thresholds.length ? CFG.thresholds[0][1] : null;
    for (var t = 0; t < CFG.thresholds.length; t++) {
      if (idx >= CFG.thresholds[t][0]) level = CFG.thresholds[t][1];
    }
    return level;
  }
  var peakThreshold = thresholdAt(peakIdx);
  var aboveThreshold = peakThreshold != null && peakAmp >= peakThreshold;

  // ---- sample index -> one-way distance (mm), per SonicLib's ch_common_samples_to_mm():
  //   mm = samples * CH_SPEEDOFSOUND_MPS * 1000 * 2^(7-odr) / (op_freq * 2)
  function distanceMmAt(idx) {
    var shift = Math.pow(2, 7 - CFG.odr);
    return (idx * 343 * 1000 * shift) / (CFG.op_freq_hz * 2);
  }
  function fmtDistance(mm) {
    return mm.toFixed(0) + " mm (" + (mm / 304.8).toFixed(1) + " ft)";
  }
  var peakDistanceMm = distanceMmAt(peakIdx);

  // ---- stat tiles ----
  var stats = [];
  stats.push({
    label: "Peak return", value: peakAmp.toFixed(0), foot: "at sample " + peakIdx
  });
  stats.push({
    label: "Peak distance", value: fmtDistance(peakDistanceMm),
    foot: CFG.op_freq_calibrated ? "op freq " + CFG.op_freq_hz + " Hz" : "approx. - pass --op-freq for the real value"
  });
  if (peakThreshold != null) {
    stats.push({
      label: "Detection threshold", value: peakThreshold.toLocaleString(),
      tag: aboveThreshold ? "good" : "warn",
      tagText: aboveThreshold ? "above threshold" : "below threshold"
    });
  }
  if (CFG.target !== null) {
    stats.push({
      label: "Firmware result", value: "target=" + CFG.target,
      foot: CFG.range_mm !== null ? CFG.range_mm.toFixed(1) + " mm" + (CFG.amp !== null ? " (amp " + CFG.amp + ")" : "") : (CFG.target === 0 ? "no target reported" : "")
    });
  }
  stats.push({ label: "Ringdown window", value: "0–" + (CFG.ringdown_samples - 1), foot: "excluded from detection" });
  stats.push({ label: "Samples", value: N, foot: CFG.csv_name });

  var statsRow = document.getElementById("statsRow");
  statsRow.innerHTML = stats.map(function (s) {
    return '<div class="stat"><span class="label">' + s.label + '</span>' +
      '<span class="value">' + s.value + '</span>' +
      (s.tag ? '<span class="tag ' + s.tag + '">' + s.tagText + '</span>' : (s.foot ? '<span class="foot">' + s.foot + '</span>' : '')) +
      '</div>';
  }).join("");

  document.getElementById("ampCaption").innerHTML =
    "Top axis = one-way distance" + (CFG.op_freq_calibrated ? "" : " (approx. - see Peak distance tile)") + ". " +
    "Shaded band = ringdown-cancel window (0–" + (CFG.ringdown_samples - 1) + "), excluded from detection. " +
    "Peak (" + peakAmp.toFixed(0) + " @ " + fmtDistance(peakDistanceMm) + ") is " + (aboveThreshold ? "above" : "below") +
    (peakThreshold != null ? " the " + peakThreshold.toLocaleString() + " threshold configured for that window." : " threshold.");

  if (hasSecondary) {
    var note = document.getElementById("secondaryNote");
    note.hidden = false;
    note.innerHTML = "<b>Note:</b> a second, comparable-height return (" + secondaryAmp.toFixed(0) +
      " @ " + fmtDistance(distanceMmAt(secondaryIdx)) + ") is visible in the trace — worth a look if unexpected.";
  }

  // ---- table ----
  var tbody = document.getElementById("tableBody");
  var rows = "";
  for (k = 0; k < N; k++) {
    var cls = (k === peakIdx) ? ' class="mark"' : "";
    rows += "<tr" + cls + "><td>" + k + "</td><td>" + distanceMmAt(k).toFixed(0) + "</td><td>" + I[k] + "</td><td>" + Q[k] + "</td><td>" + amp[k].toFixed(1) + "</td></tr>";
  }
  tbody.innerHTML = rows;

  // ---- shared x scale ----
  var svgNS = "http://www.w3.org/2000/svg";
  var M = { l: 44, r: 12, t: 10 };
  var W = 880;
  var plotW = W - M.l - M.r;
  function xPix(idx) { return M.l + (idx / (N - 1)) * plotW; }
  function idxAt(frac) { return Math.max(0, Math.min(N - 1, Math.round(frac * (N - 1)))); }

  function el(tag, attrs, text) {
    var e = document.createElementNS(svgNS, tag);
    for (var kk in attrs) e.setAttribute(kk, attrs[kk]);
    if (text != null) e.textContent = text;
    return e;
  }

  // ================= amplitude chart =================
  (function () {
    var H = 260, Mb = 22, Mt = 24, plotH = H - Mt - Mb;
    var yMax = Math.max(100, Math.ceil(peakAmp * 1.18 / 100) * 100);
    function yPix(v) { return Mt + plotH - (v / yMax) * plotH; }

    var svg = document.getElementById("ampChart");
    svg.setAttribute("viewBox", "0 0 " + W + " " + H);

    if (CFG.ringdown_samples > 0) {
      svg.appendChild(el("rect", { x: xPix(0), y: Mt, width: xPix(CFG.ringdown_samples) - xPix(0), height: plotH, fill: "var(--ringdown-fill)" }));
      svg.appendChild(el("line", { x1: xPix(CFG.ringdown_samples), x2: xPix(CFG.ringdown_samples), y1: Mt, y2: Mt + plotH, stroke: "var(--ringdown-line)", "stroke-width": 1, "stroke-dasharray": "2 3" }));
      svg.appendChild(el("text", { class: "axis-label", x: xPix(0) + 4, y: Mt + 12 }, "ringdown-cancel"));
    }

    for (var gv = 0; gv <= yMax; gv += yMax / 4) {
      var gy = yPix(gv);
      svg.appendChild(el("line", { class: "grid-line", x1: M.l, x2: W - M.r, y1: gy, y2: gy }));
      svg.appendChild(el("text", { class: "axis-label", x: M.l - 6, y: gy + 3, "text-anchor": "end" }, Math.round(gv)));
    }
    var xStep = Math.max(10, Math.round((N / 8) / 10) * 10);
    for (var xi = 0; xi <= N - 1; xi += xStep) {
      svg.appendChild(el("text", { class: "axis-label", x: xPix(xi), y: H - 6, "text-anchor": "middle" }, xi));
      svg.appendChild(el("text", { class: "axis-label", x: xPix(xi), y: 9, "text-anchor": "middle" }, (distanceMmAt(xi) / 304.8).toFixed(0) + "ft"));
    }
    svg.appendChild(el("text", { class: "axis-label", x: xPix(N - 1), y: H - 6, "text-anchor": "end" }, N - 1));

    var pts = amp.map(function (v, kk2) { return xPix(kk2) + "," + yPix(v); }).join(" ");
    var areaPts = xPix(0) + "," + yPix(0) + " " + pts + " " + xPix(N - 1) + "," + yPix(0);
    svg.appendChild(el("polygon", { points: areaPts, fill: "var(--amp-wash)" }));
    svg.appendChild(el("polyline", { points: pts, fill: "none", stroke: "var(--amp)", "stroke-width": 2, "stroke-linejoin": "round", "stroke-linecap": "round" }));

    svg.appendChild(el("circle", { cx: xPix(peakIdx), cy: yPix(peakAmp), r: 5, fill: "var(--amp)", stroke: "var(--surface-1)", "stroke-width": 2 }));
    svg.appendChild(el("text", { x: xPix(peakIdx) + 9, y: yPix(peakAmp) - 6, "font-size": 11.5, "font-weight": 600, fill: "var(--text-primary)" }, peakAmp.toFixed(0) + " @ " + fmtDistance(peakDistanceMm)));

    if (hasSecondary) {
      svg.appendChild(el("circle", { cx: xPix(secondaryIdx), cy: yPix(secondaryAmp), r: 4, fill: "none", stroke: "var(--text-muted)", "stroke-width": 1.5 }));
      svg.appendChild(el("text", { x: xPix(secondaryIdx) + 8, y: yPix(secondaryAmp) - 6, "font-size": 11, fill: "var(--text-muted)" }, secondaryAmp.toFixed(0) + " @ " + fmtDistance(distanceMmAt(secondaryIdx))));
    }

    var crosshair = el("line", { class: "crosshair", x1: 0, x2: 0, y1: Mt, y2: Mt + plotH });
    var hoverDot = el("circle", { class: "hover-dot", r: 4, fill: "var(--amp)", stroke: "var(--surface-1)", "stroke-width": 2 });
    svg.appendChild(crosshair);
    svg.appendChild(hoverDot);
    var rect = el("rect", { class: "hover-rect", x: M.l, y: 0, width: plotW, height: H });
    svg.appendChild(rect);
    window.__ampHover = { svg: svg, crosshair: crosshair, dot: hoverDot, yPix: yPix, rect: rect };
  })();

  // ================= I/Q chart =================
  (function () {
    var H = 190, Mb = 22, Mt = M.t, plotH = H - Mt - Mb;
    var maxAbs = Math.max.apply(null, I.map(Math.abs).concat(Q.map(Math.abs))) * 1.15 || 1;
    function yPix(v) { return Mt + plotH / 2 - (v / maxAbs) * (plotH / 2); }

    var svg = document.getElementById("iqChart");
    svg.setAttribute("viewBox", "0 0 " + W + " " + H);

    if (CFG.ringdown_samples > 0) {
      svg.appendChild(el("rect", { x: xPix(0), y: Mt, width: xPix(CFG.ringdown_samples) - xPix(0), height: plotH, fill: "var(--ringdown-fill)" }));
    }

    svg.appendChild(el("line", { class: "baseline", x1: M.l, x2: W - M.r, y1: yPix(0), y2: yPix(0) }));
    svg.appendChild(el("text", { class: "axis-label", x: M.l - 6, y: yPix(0) + 3, "text-anchor": "end" }, "0"));
    svg.appendChild(el("text", { class: "axis-label", x: M.l - 6, y: yPix(maxAbs) + 3, "text-anchor": "end" }, Math.round(maxAbs)));
    svg.appendChild(el("text", { class: "axis-label", x: M.l - 6, y: yPix(-maxAbs) + 3, "text-anchor": "end" }, "-" + Math.round(maxAbs)));

    var xStep2 = Math.max(10, Math.round((N / 8) / 10) * 10);
    for (var xi2 = 0; xi2 <= N - 1; xi2 += xStep2) {
      svg.appendChild(el("text", { class: "axis-label", x: xPix(xi2), y: H - 6, "text-anchor": "middle" }, xi2));
    }
    svg.appendChild(el("text", { class: "axis-label", x: xPix(N - 1) - 20, y: H - 6, "text-anchor": "end" }, "sample idx"));

    var ptsI = I.map(function (v, kk3) { return xPix(kk3) + "," + yPix(v); }).join(" ");
    var ptsQ = Q.map(function (v, kk4) { return xPix(kk4) + "," + yPix(v); }).join(" ");
    svg.appendChild(el("polyline", { points: ptsI, fill: "none", stroke: "var(--series-i)", "stroke-width": 2, "stroke-linejoin": "round", "stroke-linecap": "round" }));
    svg.appendChild(el("polyline", { points: ptsQ, fill: "none", stroke: "var(--series-q)", "stroke-width": 2, "stroke-linejoin": "round", "stroke-linecap": "round" }));

    svg.appendChild(el("text", { x: xPix(N - 1) - 4, y: yPix(I[N - 1]) - 8, "text-anchor": "end", "font-size": 11, "font-weight": 600, fill: "var(--series-i)" }, "I"));
    svg.appendChild(el("text", { x: xPix(N - 1) - 4, y: yPix(Q[N - 1]) + 14, "text-anchor": "end", "font-size": 11, "font-weight": 600, fill: "var(--series-q)" }, "Q"));

    var crosshair = el("line", { class: "crosshair", x1: 0, x2: 0, y1: Mt, y2: Mt + plotH });
    var dotI = el("circle", { class: "hover-dot", r: 4, fill: "var(--series-i)", stroke: "var(--surface-1)", "stroke-width": 2 });
    var dotQ = el("circle", { class: "hover-dot", r: 4, fill: "var(--series-q)", stroke: "var(--surface-1)", "stroke-width": 2 });
    svg.appendChild(crosshair);
    svg.appendChild(dotI);
    svg.appendChild(dotQ);
    var rect = el("rect", { class: "hover-rect", x: M.l, y: 0, width: plotW, height: H });
    svg.appendChild(rect);
    window.__iqHover = { svg: svg, crosshair: crosshair, dotI: dotI, dotQ: dotQ, yPix: yPix, rect: rect };
  })();

  // ================= synced hover =================
  var tooltip = document.getElementById("tooltip");
  var wrap = document.getElementById("chartWrap");
  var ampH = window.__ampHover, iqH = window.__iqHover;

  function showAt(clientX, clientY, sourceSvg) {
    var svgRect = sourceSvg.getBoundingClientRect();
    var frac = (clientX - svgRect.left - M.l) / plotW;
    frac = Math.max(0, Math.min(1, frac));
    var idx = idxAt(frac);
    var px = xPix(idx);

    ampH.crosshair.setAttribute("x1", px); ampH.crosshair.setAttribute("x2", px); ampH.crosshair.style.opacity = 1;
    ampH.dot.setAttribute("cx", px); ampH.dot.setAttribute("cy", ampH.yPix(amp[idx])); ampH.dot.style.opacity = 1;

    iqH.crosshair.setAttribute("x1", px); iqH.crosshair.setAttribute("x2", px); iqH.crosshair.style.opacity = 1;
    iqH.dotI.setAttribute("cx", px); iqH.dotI.setAttribute("cy", iqH.yPix(I[idx])); iqH.dotI.style.opacity = 1;
    iqH.dotQ.setAttribute("cx", px); iqH.dotQ.setAttribute("cy", iqH.yPix(Q[idx])); iqH.dotQ.style.opacity = 1;

    tooltip.innerHTML = "<b>idx " + idx + "</b>&nbsp;&nbsp;" + distanceMmAt(idx).toFixed(0) + " mm<br>amp " + amp[idx].toFixed(1) + "&nbsp;&nbsp;I " + I[idx] + "&nbsp;&nbsp;Q " + Q[idx];
    tooltip.style.opacity = 1;
    var wrapRect = wrap.getBoundingClientRect();
    var left = svgRect.left - wrapRect.left + (px / W) * svgRect.width;
    var top = clientY - wrapRect.top;
    tooltip.style.left = Math.min(left + 12, wrapRect.width - 150) + "px";
    tooltip.style.top = Math.max(top - 44, 0) + "px";
  }
  function hide() {
    [ampH.crosshair, ampH.dot, iqH.crosshair, iqH.dotI, iqH.dotQ].forEach(function (n) { n.style.opacity = 0; });
    tooltip.style.opacity = 0;
  }

  [ampH.rect, iqH.rect].forEach(function (rect, i2) {
    var svgEl = i2 === 0 ? ampH.svg : iqH.svg;
    svgEl.addEventListener("mousemove", function (e) { showAt(e.clientX, e.clientY, svgEl); });
    svgEl.addEventListener("mouseleave", hide);
    svgEl.addEventListener("touchmove", function (e) {
      var t = e.touches[0]; if (t) { showAt(t.clientX, t.clientY, svgEl); e.preventDefault(); }
    }, { passive: false });
    svgEl.addEventListener("touchend", hide);
  });
})();
</script>
""")


def render_html(iq, args):
    i_vals = [p[0] for p in iq]
    q_vals = [p[1] for p in iq]

    title = args.title or Path(args.csv).stem
    subtitle = f"Raw I/Q trace &middot; {len(iq)} samples &middot; <code class=\"mono\">{Path(args.csv).name}</code>"
    footer = "Sensor: ICU-20201 &middot; receiver_readout raw I/Q dump &middot; rendered by plot_readout.py"

    config = {
        "ringdown_samples": args.ringdown_samples,
        "thresholds": [list(t) for t in args.threshold],
        "target": args.target,
        "range_mm": args.range_mm,
        "amp": args.amp,
        "csv_name": Path(args.csv).name,
        "op_freq_hz": args.op_freq,
        "odr": args.odr,
        "op_freq_calibrated": args.op_freq != DEFAULT_OP_FREQ_HZ,
    }

    return PAGE_TEMPLATE.substitute(
        title=title,
        subtitle=subtitle,
        footer=footer,
        data_i=json.dumps(i_vals),
        data_q=json.dumps(q_vals),
        config_json=json.dumps(config),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", help="CSV file of I/Q samples (idx,i,q or i,q; header optional)")
    parser.add_argument("-o", "--output", help="output HTML path (default: <csv>.html)")
    parser.add_argument("--title", help="page title (default: CSV filename)")
    parser.add_argument("--open", action="store_true", help="open the rendered HTML in the default browser")
    parser.add_argument("--ringdown-samples", type=int, default=DEFAULT_RINGDOWN_SAMPLES,
                         help=f"samples excluded from detection (default: {DEFAULT_RINGDOWN_SAMPLES}, "
                              f"matching RX_RINGDOWN_CANCEL_SAMPLES in readout_loop.c)")
    parser.add_argument("--threshold", type=parse_threshold_arg, action="append", metavar="START:LEVEL",
                         help="detection threshold segment, repeatable (default: matches readout_loop.c's "
                              "rx_thresholds - " + ", ".join(f"{s}:{l}" for s, l in DEFAULT_THRESHOLDS) + ")")
    parser.add_argument("--target", type=int, choices=[0, 1], default=None,
                         help="firmware-reported target flag, if known (from the IQ_BEGIN header)")
    parser.add_argument("--range-mm", type=float, default=None, help="firmware-reported range_mm, if known")
    parser.add_argument("--amp", type=int, default=None, help="firmware-reported amplitude, if known")
    parser.add_argument("--op-freq", type=float, default=DEFAULT_OP_FREQ_HZ, metavar="HZ",
                         help=f"sensor operating frequency, for the sample->distance conversion (default: "
                              f"{DEFAULT_OP_FREQ_HZ} Hz, an approximation observed during this project's bring-up "
                              f"- pass the real value logged as \"op frequency\"/\"op freq\" by icu_post/"
                              f"receiver_readout for an accurate distance axis)")
    parser.add_argument("--odr", type=int, default=DEFAULT_ODR, choices=[2, 3, 4, 5, 6], metavar="N",
                         help=f"CH_ODR_FREQ_DIV_* enum value used for the recording (default: {DEFAULT_ODR}, "
                              f"CH_ODR_DEFAULT - PC_ODR in pitch_catch_common.h)")
    args = parser.parse_args()

    if not args.threshold:
        args.threshold = list(DEFAULT_THRESHOLDS)

    if args.op_freq == DEFAULT_OP_FREQ_HZ:
        print(f"note: using the default --op-freq ({DEFAULT_OP_FREQ_HZ} Hz, not calibrated to your sensor) "
              f"- the distance axis will be approximate", file=sys.stderr)

    iq = load_iq_csv(args.csv)
    if not iq:
        raise SystemExit(f"{args.csv}: parsed zero samples")

    html = render_html(iq, args)

    out_path = Path(args.output) if args.output else Path(args.csv).with_suffix(".html")
    out_path.write_text(html)
    print(f"wrote {out_path} ({len(iq)} samples)")

    if args.open:
        webbrowser.open(out_path.resolve().as_uri())


if __name__ == "__main__":
    main()
