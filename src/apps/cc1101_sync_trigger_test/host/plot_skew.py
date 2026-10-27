"""Plot a skew_sweep.py CSV: p90 |skew| of every config by RX data rate, next to the per-shot skew of
the best configs.

    python plot_skew.py ../readouts/skew_sweep_20261025.csv out.png [--top 12]
    python plot_skew.py <csv> out.png --p90-only [--by rx|tx|bw] [--rx 160]

    python plot_skew.py <csv> out.png --config ASK_tx10_rx160_bw541_agc050091

--p90-only plots just the p90 panel, grouped by --by; --rx keeps only configs at that RX data rate.
--config plots every shot of one config: a histogram (clipped to +-SHOT_XLIM, outliers listed) and
the skew of each shot in order (symlog, so whole-bit outliers stay on scale).
"""

import argparse
import math

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from skew_sweep import load_rows, p90_abs, per_config, rank_key

MOD_COLOR = {"ASK": "#2a78d6", "FSK": "#eb6834", "MSK": "#1baf7a"}  # categorical slots 1-3
MODS = list(MOD_COLOR)
INK, INK2, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"
LIMIT_US = 1.0     # goal: sub-microsecond skew
SHOT_XLIM = 2.0    # per-shot panel is clipped to +-SHOT_XLIM us; outliers are drawn as an arrow
GROUP_BY = {  # --by: (CSV column, standalone x-axis label)
	"rx": ("rx_kbaud", "Receiver data rate (kBaud)"),
	"tx": ("tx_kbaud", "Transmitter data rate (kBaud)"),
	"bw": ("bw_khz", "Filter bandwidth (kHz)"),
}


def label(r):
	agc = r["agc"][:2]
	return f'{r["mod"]} TX {float(r["tx_kbaud"]):g}k / RX {r["rx_kbaud"]}k, {r["bw_khz"]} kHz, AGC {agc}'


def style(ax):
	ax.set_facecolor(SURFACE)
	for s in ("top", "right"):
		ax.spines[s].set_visible(False)
	for s in ("left", "bottom"):
		ax.spines[s].set_color(GRID)
	ax.tick_params(colors=INK2, labelsize=9, length=0)


def plot_p90(ax, pc, by="rx", standalone=False):
	"""Every config's p90 |skew|, grouped by the `by` column, offset and coloured by modulation."""
	col, xlabel = GROUP_BY[by]
	rates = sorted({float(d["rows"][col]) for d in pc.values()})
	rng = np.random.default_rng(1)
	for cid, d in pc.items():
		if not d["skews"]:
			continue
		r = d["rows"]
		x = rates.index(float(r[col])) + (MODS.index(r["mod"]) - 1) * 0.25 + rng.uniform(-0.07, 0.07)
		ax.scatter(x, p90_abs(d["skews"]), s=36, color=MOD_COLOR[r["mod"]], edgecolor=SURFACE,
		           linewidth=1.5, zorder=3)
	ax.axhline(LIMIT_US, color=INK2, linewidth=1, linestyle="--", zorder=2)
	ax.text(len(rates) - 0.5, LIMIT_US * 1.08, "1 µs goal", color=INK2, fontsize=9, ha="right", va="bottom")
	ax.set_yscale("log")
	ax.set_xticks(range(len(rates)), [f"{r:g}" if standalone else f"{r:g} kBaud" for r in rates])
	ax.set_xlim(-0.5, len(rates) - 0.5)
	ax.set_xlabel(xlabel if standalone else "Receiver data rate (MDMCFG4/3)",
	              color=INK2, fontsize=10)
	ax.set_ylabel("90th-percentile skew (µs)" if standalone else "p90 |skew| per config (µs, log)",
	              color=INK2, fontsize=10)
	ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}"))
	ax.grid(axis="y", color=GRID, linewidth=0.8, zorder=0)
	if not standalone:
		ax.set_title("All configs: receiver data rate dominates", loc="left", color=INK, fontsize=12)


def plot_shots(ax, pc, top):
	"""Per-shot signed skew of the best `top` configs (by the sweep's rank), best at the top."""
	best = sorted((d for d in pc.values() if d["skews"]), key=rank_key)[:top]
	for i, d in enumerate(best):
		y = top - 1 - i
		c = MOD_COLOR[d["rows"]["mod"]]
		p = p90_abs(d["skews"])
		ax.plot([-p, p], [y, y], color=c, alpha=0.25, linewidth=8, solid_capstyle="round", zorder=2)
		s = np.asarray(d["skews"])
		inside = np.abs(s) <= SHOT_XLIM
		ax.scatter(s[inside], np.full(inside.sum(), y), s=28, color=c, edgecolor=SURFACE, linewidth=1.2,
		           zorder=3)
		for v in s[~inside]:
			edge = math.copysign(SHOT_XLIM * 0.97, v)
			ax.annotate(f"{v:+.0f}", (edge, y), xytext=(-14 if v > 0 else 14, 0), textcoords="offset points",
			            ha="right" if v > 0 else "left", va="center", fontsize=8, color=INK2,
			            arrowprops=dict(arrowstyle="->", color=c, linewidth=1.2))
	ax.set_yticks(range(top), [label(d["rows"]) for d in reversed(best)], fontsize=9, color=INK)
	for x in (-LIMIT_US, LIMIT_US):
		ax.axvline(x, color=INK2, linewidth=1, linestyle="--", zorder=1)
	ax.axvline(0, color=GRID, linewidth=1, zorder=1)
	ax.set_xlim(-SHOT_XLIM, SHOT_XLIM)
	ax.set_ylim(-0.6, top - 0.4)
	ax.set_xlabel("Skew CH2 − CH1 per shot (µs); band = ±p90", color=INK2, fontsize=10)
	ax.grid(axis="x", color=GRID, linewidth=0.8, zorder=0)
	ax.set_title(f"Top {top} configs: every shot", loc="left", color=INK, fontsize=12)


def describe(r):
	agc = "/".join(r["agc"][i:i + 2] for i in (0, 2, 4))
	return (f'{r["mod"]}, TX {float(r["tx_kbaud"]):g} kBaud, RX {r["rx_kbaud"]} kBaud, '
	        f'{r["bw_khz"]} kHz filter, AGC {agc}')


def plot_config(pc, cid, png):
	"""Every valid shot of one config: distribution on the left, shot order on the right."""
	d = pc[cid]
	r = d["rows"]
	c = MOD_COLOR[r["mod"]]
	s = np.asarray(d["skews"])
	a = np.abs(s)
	fig, (h, t) = plt.subplots(1, 2, figsize=(14, 5.5), gridspec_kw={"width_ratios": [1, 1.4]})
	fig.patch.set_facecolor(SURFACE)
	for ax in (h, t):
		style(ax)

	inside = s[np.abs(s) <= SHOT_XLIM]
	h.hist(inside, bins=np.arange(-SHOT_XLIM, SHOT_XLIM + 1e-9, 0.1), color=c, edgecolor=SURFACE,
	       linewidth=1, zorder=3)
	for x in (-LIMIT_US, LIMIT_US):
		h.axvline(x, color=INK2, linewidth=1, linestyle="--", zorder=2)
	h.set_xlim(-SHOT_XLIM, SHOT_XLIM)
	h.set_xlabel("Skew CH2 − CH1 (µs)", color=INK2, fontsize=10)
	h.set_ylabel("Shots per 0.1 µs bin", color=INK2, fontsize=10)
	h.grid(axis="y", color=GRID, linewidth=0.8, zorder=0)
	h.set_title("Distribution", loc="left", color=INK, fontsize=12)
	out = s[np.abs(s) > SHOT_XLIM]
	note = (f"{out.size} outlier{'s' if out.size != 1 else ''} beyond ±{SHOT_XLIM:g} µs: "
	        + ", ".join(f"{v:+.1f}" for v in out)) if out.size else f"No shots beyond ±{SHOT_XLIM:g} µs"
	h.text(0.02, 0.97, note, transform=h.transAxes, ha="left", va="top", fontsize=9, color=INK2, zorder=5,
	       bbox=dict(facecolor=SURFACE, edgecolor="none", pad=2))

	n = np.arange(s.size)
	t.axhspan(-LIMIT_US, LIMIT_US, color=GRID, alpha=0.5, zorder=1, linewidth=0)
	t.scatter(n, s, s=10, color=c, linewidth=0, zorder=3)
	big = a > LIMIT_US
	t.scatter(n[big], s[big], s=36, facecolor="none", edgecolor=INK, linewidth=1, zorder=4)
	t.set_yscale("symlog", linthresh=1)
	t.set_ylim(-200, 200)
	t.set_yticks([-100, -10, -1, 0, 1, 10, 100])
	t.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}"))
	t.set_xlim(-5, s.size + 5)
	t.set_xlabel("Shot number (~1.4 s apart)", color=INK2, fontsize=10)
	t.set_ylabel("Skew CH2 − CH1 (µs, symlog)", color=INK2, fontsize=10)
	t.grid(axis="y", color=GRID, linewidth=0.8, zorder=0)
	t.set_title("Every shot in order (shaded = ±1 µs, circled = over 1 µs)", loc="left", color=INK, fontsize=12)

	stats = (f"median {np.median(s):+.2f}   p90 {np.percentile(a, 90):.2f}   p95 {np.percentile(a, 95):.2f}   "
	         f"p99 {np.percentile(a, 99):.2f}   max {a.max():.2f} µs   over 1 µs: {int(big.sum())}/{s.size}")
	fig.text(0.01, 0.975, "Relative latency between 2 CC1101 receivers", ha="left", va="top", color=INK,
	         fontsize=13, fontweight="bold")
	fig.text(0.01, 0.925, f"{describe(r)}: {s.size} samples", ha="left", va="top", color=INK2, fontsize=10)
	fig.text(0.01, 0.885, stats, ha="left", va="top", color=INK2, fontsize=10)
	fig.tight_layout(rect=(0, 0, 1, 0.86))
	fig.savefig(png, dpi=150, facecolor=SURFACE)


def legend(fig):
	handles = [plt.Line2D([], [], marker="o", linestyle="", markersize=8, color=MOD_COLOR[m], label=m)
	           for m in MODS]
	fig.legend(handles=handles, loc="upper right", ncol=3, frameon=False, fontsize=10, labelcolor=INK)


def plot_p90_only(pc, png, by, rx):
	fig, ax = plt.subplots(figsize=(8, 6.5))
	fig.patch.set_facecolor(SURFACE)
	style(ax)
	plot_p90(ax, pc, by, standalone=True)
	legend(fig)
	n = sum(len(d["skews"]) for d in pc.values())
	fig.text(0.02, 0.975, "Relative latency between 2 CC1101 receivers", ha="left", va="top", color=INK,
	         fontsize=13, fontweight="bold")
	scope = f"Receiver data rate {rx:g} kBaud: " if rx is not None else ""
	fig.text(0.02, 0.925, f"{scope}{len(pc)} configurations, {n} samples", ha="left", va="top", color=INK2, fontsize=10)
	fig.tight_layout(rect=(0, 0, 1, 0.9))
	fig.savefig(png, dpi=150, facecolor=SURFACE)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("csv")
	ap.add_argument("png")
	ap.add_argument("--top", type=int, default=12)
	ap.add_argument("--p90-only", action="store_true", help="only the p90 panel")
	ap.add_argument("--by", choices=GROUP_BY, default="rx", help="x-axis grouping for --p90-only")
	ap.add_argument("--rx", type=float, help="only configs at this RX data rate (kBaud)")
	ap.add_argument("--config", help="plot every shot of this config ID")
	args = ap.parse_args()
	pc = per_config(load_rows(args.csv))
	if args.rx is not None:
		pc = {cid: d for cid, d in pc.items() if float(d["rows"]["rx_kbaud"]) == args.rx}
	if args.config:
		return plot_config(pc, args.config, args.png)
	if args.p90_only:
		return plot_p90_only(pc, args.png, args.by, args.rx)
	fig, (a, b) = plt.subplots(1, 2, figsize=(15, 6.5), gridspec_kw={"width_ratios": [1, 1.35]})
	fig.patch.set_facecolor(SURFACE)
	for ax in (a, b):
		style(ax)
	plot_p90(a, pc)
	plot_shots(b, pc, args.top)
	legend(fig)
	n = sum(len(d["skews"]) for d in pc.values())
	fig.suptitle(f"CC1101 leading falling-edge skew between two receivers ({len(pc)} configs, {n} valid shots)",
	             x=0.01, ha="left", color=INK, fontsize=13, fontweight="bold")
	fig.tight_layout(rect=(0, 0, 1, 0.95))
	fig.savefig(args.png, dpi=150, facecolor=SURFACE)


if __name__ == "__main__":
	main()
