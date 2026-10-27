"""CC1101 leading-falling-edge skew sweep: modulation x TX bit rate x RX data rate x RX filter x AGC.

Drives the transmitter's console menu over serial (2 = TX rate, 4 = local register, 3 = broadcast
a register write to the receivers, 5 = collect the receivers' register dumps, SPACE = send) and a
Siglent SDS1052DL+ over VXI-11 (CH1 = receiver A GDO2, CH2 = receiver B GDO2, trigger on CH1's
falling edge). Every shot is one CSV row; see skew.py for the edge extraction and validity rule.

    python skew_sweep.py                 # run (or resume) both stages
    python skew_sweep.py --dry-run       # one config, 3 shots
    python skew_sweep.py --summarize     # ranked per-config table from the CSV
    python skew_sweep.py --restore       # put every node back to BEST and exit
    python skew_sweep.py --shortlist     # SHORTLIST (../configs_to_test.md), --shots valid shots each

Resuming skips configs that already have SHOTS_PER_CONFIG valid shots (or a config_failed row).
--shortlist writes to its own CSV (skew_confirm_<date>.csv) and resumes by topping each config up to
--shots valid shots.
"""

import argparse
import csv
import datetime
import math
import os
import re
import statistics
import sys
import time
from dataclasses import dataclass

import numpy as np
import pyvisa
import serial

import skew

SERIAL_PORT = "/dev/ttyUSB1"
SCOPE = "TCPIP::192.168.1.159::INSTR"
RX_MACS = {"B0:B2:1C:A8:AD:80", "8C:94:DF:58:7D:60"}
HERE = os.path.dirname(os.path.abspath(__file__))
READOUTS = os.path.join(HERE, "..", "readouts")
CSV_PATH = os.path.join(READOUTS, f"skew_sweep_{datetime.date.today():%Y%m%d}.csv")

SHOTS_PER_CONFIG = 10
MAX_ATTEMPTS = 25        # shots tried per config before giving up on reaching SHOTS_PER_CONFIG
VERIFY_ROUNDS = 3        # re-broadcast rounds when a receiver's dump doesn't match
XOSC_HZ = 26e6

# CC1101 registers touched by the sweep
MDMCFG4, MDMCFG3, MDMCFG2, DEVIATN = 0x10, 0x11, 0x12, 0x15
AGCCTRL2, AGCCTRL1, AGCCTRL0 = 0x1B, 0x1C, 0x1D

SWEPT = {MDMCFG4, MDMCFG3, MDMCFG2, DEVIATN, AGCCTRL2, AGCCTRL1, AGCCTRL0}
VOLATILE = {0x23, 0x24, 0x25, 0x2B}  # FSCAL3..1 (calibration results), AGCTEST (reads back varying)
GUARDED = set(range(0x2F)) - SWEPT - VOLATILE

MOD_FORMAT = {"ASK": 0x30, "FSK": 0x00, "MSK": 0x70}  # MDMCFG2, sync word off, Manchester off
BW_NIBBLE = {203: 0x80, 325: 0x50, 541: 0x20, 812: 0x00}  # MDMCFG4[7:4] CHANBW_E/M
AGC_PRESETS = {
	"best": (0x05, 0x00, 0x91),     # current_best_config.txt
	"dn022": (0x03, 0x00, 0x91),
	"lowgain": (0xC3, 0x00, 0x91),
	"default": (0x03, 0x40, 0x91),  # chip reset values
}


@dataclass(frozen=True)
class Config:
	mod: str
	tx_kbaud: float
	rx_kbaud: float
	bw_khz: int
	agc: tuple

	@property
	def cid(self):
		a = "".join(f"{x:02X}" for x in self.agc)
		return f"{self.mod}_tx{self.tx_kbaud:g}_rx{self.rx_kbaud:g}_bw{self.bw_khz}_agc{a}"


BEST = Config("ASK", 1.0, 160, 325, AGC_PRESETS["best"])
SHORTLIST = [  # ../configs_to_test.md, in that order
	Config("ASK", 10, 160, 541, AGC_PRESETS["best"]),
	Config("FSK", 50, 160, 541, AGC_PRESETS["lowgain"]),
	Config("FSK", 50, 160, 541, AGC_PRESETS["default"]),
	Config("ASK", 10, 160, 203, AGC_PRESETS["best"]),
	Config("FSK", 1, 160, 203, AGC_PRESETS["best"]),
	Config("FSK", 10, 160, 203, AGC_PRESETS["best"]),
]


def drate(baud):
	"""(DRATE_E, DRATE_M) closest to baud - identical to cc_radio_set_datarate() in cc_radio.c."""
	e = math.floor(math.log2(baud * (1 << 20) / XOSC_HZ))
	m = math.floor(baud * 268435456.0 / (XOSC_HZ * 2.0 ** e) - 256.0 + 0.5)  # lround() for x > 0
	if m > 255:
		m, e = 0, e + 1
	return max(0, min(15, e)), m


def deviatn_for(dev_hz):
	"""(DEVIATN byte, actual Hz) closest to dev_hz: f_dev = f_xosc / 2^17 * (8 + M) * 2^E."""
	best = None
	for e in range(8):
		for m in range(8):
			f = XOSC_HZ / 2 ** 17 * (8 + m) * 2 ** e
			if best is None or abs(f - dev_hz) < abs(best[1] - dev_hz):
				best = ((e << 4) | m, f)
	return best


def mod_regs(cfg):
	"""(MDMCFG2, DEVIATN, deviation Hz or None) for cfg's modulation."""
	r = cfg.tx_kbaud * 1000
	if cfg.mod == "FSK":
		dv, dev = deviatn_for(min(max(r / 2, 5e3), 100e3))
		return MOD_FORMAT["FSK"], dv, dev
	if cfg.mod == "MSK":
		return MOD_FORMAT["MSK"], 0x07, r / 4  # DEVIATION_M = phase-change fraction 8/8
	return MOD_FORMAT["ASK"], 0x47, None


def bw_ok(cfg):
	r = cfg.tx_kbaud * 1000
	_, _, dev = mod_regs(cfg)
	need = r + 2 * dev if dev is not None else r
	if cfg.mod == "MSK" and cfg.tx_kbaud < 26:
		return False  # CC1101 supports MSK only above 26 kBaud
	return cfg.bw_khz * 1000 >= need


def rx_regs(cfg):
	e, m = drate(cfg.rx_kbaud * 1000)
	m2, dv, _ = mod_regs(cfg)
	return {MDMCFG4: BW_NIBBLE[cfg.bw_khz] | e, MDMCFG3: m, MDMCFG2: m2, DEVIATN: dv,
	        AGCCTRL2: cfg.agc[0], AGCCTRL1: cfg.agc[1], AGCCTRL0: cfg.agc[2]}


def tx_regs(cfg):
	e, _ = drate(cfg.tx_kbaud * 1000)  # menu 2 has already written DRATE_M / DRATE_E
	m2, dv, _ = mod_regs(cfg)
	return {MDMCFG4: BW_NIBBLE[cfg.bw_khz] | e, MDMCFG2: m2, DEVIATN: dv}


def log(msg):
	print(f"{datetime.datetime.now():%H:%M:%S} {msg}", flush=True)


class Desync(Exception):
	pass


class Transmitter:
	"""The transmitter's console menu (tx_menu.c). Every exchange waits for the line the firmware
	prints when it finishes, so the script never types into a prompt it hasn't seen."""

	MENU_END = "dump local registers"

	def __init__(self, port):
		self.ser = serial.Serial()
		self.ser.port, self.ser.baudrate, self.ser.timeout = port, 115200, 0.05
		# Leave DTR/RTS at pyserial's defaults: forcing them low before open pulses EN through the
		# DevKit's auto-reset circuit and reboots the transmitter (seen on this CP2102 board).
		self.ser.open()
		self.buf = ""
		self.rebooted = False

	def _send(self, s):
		self.ser.write(s.encode())

	def expect(self, pattern, timeout=3.0):
		rx = re.compile(pattern)
		end = time.time() + timeout
		while True:
			m = rx.search(self.buf)
			if m:
				self.buf = self.buf[m.end():]
				return m
			if time.time() > end:
				tail = self.buf[-200:].replace("\r", "")
				raise Desync(f"timeout waiting for /{pattern}/; last output: {tail!r}")
			chunk = self.ser.read(4096).decode(errors="replace")
			if chunk:
				if "rst:" in chunk or "Calling app_main" in chunk or "configured for async" in chunk:
					self.rebooted = True
				self.buf += chunk

	def sync(self):
		"""Get back to the top-level menu. Enter is safe everywhere: at the menu it reprints it, and
		in a prompt an empty entry only re-prompts (it never commits a value)."""
		for _ in range(3):
			self.buf = ""
			self._send("\r")
			try:
				self.expect(self.MENU_END, 2.0)
				time.sleep(0.05)
				self.buf = ""
				return
			except Desync:
				pass
		raise Desync("menu not reachable with Enter - transmitter stuck in a prompt?")

	PROMPTS = {  # marker -> answers key
		"dump local registers": None,
		"register (hex": "reg",
		"value (hex): ": "val",
		"bit rate in kBaud": "rate",
		"transmit data (hex": "data",
	}

	def recover(self, answers):
		"""Serial-corruption recovery: find out which prompt the firmware is sitting in and answer it
		only with the value the interrupted operation meant to send (so re-running it is idempotent).
		The transmit-data prompt gets the firmware default, which the sweep never changes."""
		answers = dict(answers, data="AAAAAA")
		for _ in range(6):
			self.buf = ""
			self._send("\r")
			end = time.time() + 1.5
			while time.time() < end:
				self.buf += self.ser.read(4096).decode(errors="replace")
			tail = self.buf[-400:]
			if "Calling app_main" in self.buf or "rst:" in self.buf:
				self.rebooted = True
			pos = {m: tail.rfind(m) for m in self.PROMPTS}
			marker = max(pos, key=pos.get)
			if pos[marker] < 0:
				continue  # nothing recognisable yet (booting?) - try again
			key = self.PROMPTS[marker]
			if key is None:
				self.buf = ""
				return
			if key not in answers:
				raise Desync(f"stuck at prompt {marker!r} with no safe answer")
			log(f"WARN recover: answering {marker!r} with {answers[key]}")
			self._send(f"{answers[key]}\r")
			time.sleep(0.3)
		raise Desync("recover: menu not reachable")

	def op(self, fn, answers=None):
		"""Run a menu operation; on a desync recover once and re-run it (all operations are idempotent)."""
		for attempt in range(3):
			try:
				return fn()
			except Desync as e:
				if attempt == 2:
					raise
				log(f"WARN desync ({e}); recovering")
				self.recover(answers or {})

	def dump_local(self):
		"""Menu d: {addr: value} for the transmitter's config 0x00-0x2E."""
		self._send("d")
		m = self.expect(r"(?s)(.*?)PATABLE\[0\.\.1\]: \w\w \w\w")
		self.expect(self.MENU_END)
		regs = {}
		for row in re.finditer(r"0x([0-2]0):\s+((?:[0-9A-F]{2} ?)+)", m.group(1)):
			for i, b in enumerate(row.group(2).split()):
				regs[int(row.group(1), 16) + i] = int(b, 16)
		return regs

	def _reg_value_prompts(self, reg, val):
		self.expect(r"register \(hex")
		self._send(f"{reg:02X}\r")
		self.expect(r"value \(hex\): ")
		self._send(f"{val:02X}\r")

	def set_rate(self, kbaud):
		self._send("2")
		self.expect(r"bit rate in kBaud")
		self._send(f"{kbaud:g}\r")
		m = self.expect(r"rate = (\d+) Baud \(CC1101 (\d+) Baud\)|failed: (\S+)")
		self.expect(self.MENU_END)
		if m.group(3):
			raise Desync(f"rate change failed: {m.group(3)}")
		return int(m.group(1)), int(m.group(2))

	def local_write(self, reg, val):
		self._send("4")
		self._reg_value_prompts(reg, val)
		m = self.expect(r"reg 0x(\w\w) = 0x(\w\w) \(read back 0x(\w\w)\)|write failed: (\S+)")
		self.expect(self.MENU_END)
		if m.group(4) or int(m.group(3), 16) != val:
			raise Desync(f"local write 0x{reg:02X}=0x{val:02X} failed: {m.group(0)}")

	def broadcast(self, reg, val):
		self._send("3")
		self._reg_value_prompts(reg, val)
		m = self.expect(r'broadcast "\w\w:\w\w": (\w+)')
		self.expect(self.MENU_END)
		return m.group(1) == "ESP_OK"

	def collect(self):
		"""Menu 5: {mac: {addr: value}} for every receiver that replied (config 0x00-0x2E)."""
		self._send("5")
		m = self.expect(r"(?s)(.*?)(\d+) node\(s\) replied", 4.0)
		self.expect(self.MENU_END)
		nodes = {}
		for block in re.split(r"\r?\nnode ", m.group(1))[1:]:
			mac = block.split()[0].strip()
			regs = {}
			for row in re.finditer(r"0x([0-2]0):\s+((?:[0-9A-F]{2} ?)+)", block):
				base = int(row.group(1), 16)
				for i, b in enumerate(row.group(2).split()):
					regs[base + i] = int(b, 16)
			nodes[mac] = regs
		return nodes

	def send_burst(self):
		self._send(" ")
		m = self.expect(r"sent 0x\w+|send failed: (\S+)", 3.0)
		if m.group(1):
			raise Desync(f"send failed: {m.group(1)}")


class Scope:
	"""Legacy Siglent SDS1000DL+ SCPI (the :TRIG:EDG:* syntax is ignored on this firmware)."""

	def __init__(self, resource):
		self.inst = pyvisa.ResourceManager("@py").open_resource(resource)
		self.inst.timeout = 10000
		self.inst.read_termination = "\n"
		self.inst.write_termination = "\n"

	def num(self, q):
		r = self.inst.query(q).strip()
		m = re.search(r"([-+]?\d+(?:\.\d+)?(?:[Ee][-+]?\d+)?)\s*([munpkMG]?)", r.split(" ", 1)[-1])
		scale = {"": 1, "m": 1e-3, "u": 1e-6, "n": 1e-9, "p": 1e-12, "k": 1e3, "M": 1e6, "G": 1e9}
		return float(m.group(1)) * scale[m.group(2)]

	def setup(self):
		for c in ("C1:VDIV 1V", "C2:VDIV 1V", "C1:OFST 0V", "C2:OFST 0V", "C1:CPL D1M", "C2:CPL D1M",
		          "TDIV 10US", "TRDL 0", "TRSE EDGE,SR,C1", "C1:TRLV 1.65V", "C1:TRSL NEG"):
			self.inst.write(c)
		time.sleep(0.5)
		self.vdiv = {ch: self.num(f"C{ch}:VDIV?") for ch in (1, 2)}
		self.ofst = {ch: self.num(f"C{ch}:OFST?") for ch in (1, 2)}
		self.sara = self.num("SARA?")
		self.trdl = self.num("TRDL?")
		log(f"scope: VDIV {self.vdiv} OFST {self.ofst} SARA {self.sara:.3g} TRDL {self.trdl:g} "
		    f"slope {self.inst.query('C1:TRSL?').strip()} level {self.inst.query('C1:TRLV?').strip()}")

	def state(self):
		return self.inst.query("SAST?").strip()

	def arm(self):
		"""Arm a single acquisition. Returns "ready", or "noise" if it triggered before any burst was
		sent (CH1 is chattering at idle - a property of the config, recorded as a shot)."""
		for _ in range(3):
			self.inst.write("TRMD SINGLE")
			end = time.time() + 2
			while time.time() < end:
				s = self.state()
				if "Ready" in s:
					return "ready"
				if "Trig" in s or "Stop" in s:
					return "noise"
				time.sleep(0.02)
			log(f"WARN scope stuck in {self.state()!r}; re-arming")
			self.inst.write("STOP")
			time.sleep(0.5)
		raise Desync("scope did not arm")

	def wait_trigger(self, timeout=2.0):
		end = time.time() + timeout
		while time.time() < end:
			s = self.state()
			if "Trig" in s or "Stop" in s:
				return True
			time.sleep(0.02)
		self.inst.write("STOP")
		return False

	def trace(self, ch):
		raw = np.asarray(self.inst.query_binary_values(f"C{ch}:WF? DAT2", datatype="b",
		                                               container=np.ndarray), dtype=np.float64)
		v = raw * self.vdiv[ch] / 25.0 - self.ofst[ch]
		t = self.trdl + (np.arange(raw.size) - raw.size / 2) / self.sara
		return t, v


FIELDS = ["time", "stage", "config", "mod", "tx_kbaud", "rx_kbaud", "bw_khz", "agc", "shot",
          "attempt", "status", "skew_us", "f1_us", "f2_us", "pre1", "pre2", "settle_s"]


class Sweep:
	def __init__(self, tx, scope, csv_path):
		self.tx, self.scope, self.csv_path = tx, scope, csv_path
		self.rx_state = {}  # mac -> last verified registers
		self.tx_state = {}  # register cache, plus "rate"
		self.settle = 1.0
		self.bad_streak = 0
		self.capture_baseline()
		new = not os.path.exists(csv_path)
		self.f = open(csv_path, "a", newline="")
		self.w = csv.DictWriter(self.f, FIELDS)
		if new:
			self.w.writeheader()

	def row(self, stage, cfg, **kw):
		r = dict(time=f"{datetime.datetime.now():%H:%M:%S}", stage=stage, config=cfg.cid, mod=cfg.mod,
		         tx_kbaud=f"{cfg.tx_kbaud:g}", rx_kbaud=f"{cfg.rx_kbaud:g}", bw_khz=cfg.bw_khz,
		         agc="".join(f"{x:02X}" for x in cfg.agc))
		r.update(kw)
		self.w.writerow(r)
		self.f.flush()

	def capture_baseline(self):
		"""Registers the sweep must never change (everything but the swept and self-calibrating
		ones), so a corrupted keystroke that lands on another register gets caught and repaired."""
		self.base_tx = {r: v for r, v in self.tx.op(self.tx.dump_local).items() if r in GUARDED}
		nodes = {mac: regs for mac, regs in self.tx.op(self.tx.collect).items() if mac in RX_MACS}
		if set(nodes) != RX_MACS:
			raise Desync(f"baseline: receivers missing: {sorted(RX_MACS - set(nodes))}")
		a, b = list(nodes.values())
		self.base_rx = {r: a[r] for r in GUARDED if a.get(r) == b.get(r)}
		log(f"baseline: {len(self.base_tx)} TX regs, {len(self.base_rx)} RX regs guarded")

	def check_reboot(self):
		if self.tx.rebooted:
			log("WARN transmitter rebooted - re-applying its config")
			self.tx.rebooted = False
			self.tx_state.clear()
			self.tx.op(self.tx.sync)

	def apply(self, cfg):
		"""Configure the transmitter (local) and both receivers (broadcast + verified)."""
		self.check_reboot()
		tx = self.tx
		if self.tx_state.get("rate") != cfg.tx_kbaud:
			tx.op(lambda: tx.set_rate(cfg.tx_kbaud), {"rate": f"{cfg.tx_kbaud:g}"})
			self.tx_state["rate"] = cfg.tx_kbaud
			self.tx_state.pop(MDMCFG4, None)  # menu 2 rewrote DRATE_E
		for reg, val in tx_regs(cfg).items():
			if self.tx_state.get(reg) != val:
				tx.op(lambda: tx.local_write(reg, val), {"reg": f"{reg:02X}", "val": f"{val:02X}"})
				self.tx_state[reg] = val
		local = tx.op(tx.dump_local)
		for reg, val in self.base_tx.items():
			if local.get(reg) != val:
				log(f"WARN TX reg 0x{reg:02X} drifted to 0x{local.get(reg, 0):02X} - restoring 0x{val:02X}")
				tx.op(lambda: tx.local_write(reg, val), {"reg": f"{reg:02X}", "val": f"{val:02X}"})
		if any(local.get(r) != v for r, v in tx_regs(cfg).items()):
			self.tx_state.clear()  # cache was wrong (reboot?) - rewrite next time
			log("WARN TX swept regs don't match after apply")
			return False

		want = dict(self.base_rx)
		want.update(rx_regs(cfg))
		pending = {r: v for r, v in want.items()
		           if len(self.rx_state) < len(RX_MACS) or any(s.get(r) != v for s in self.rx_state.values())}
		for attempt in range(VERIFY_ROUNDS + 1):
			for reg, val in pending.items():
				if not tx.op(lambda: tx.broadcast(reg, val), {"reg": f"{reg:02X}", "val": f"{val:02X}"}):
					log(f"WARN broadcast 0x{reg:02X}=0x{val:02X} not sent")
			nodes = tx.op(tx.collect)
			self.rx_state = {mac: regs for mac, regs in nodes.items() if mac in RX_MACS}
			missing = RX_MACS - set(self.rx_state)
			pending = {r: v for r, v in want.items()
			           if missing or any(s.get(r) != v for s in self.rx_state.values())}
			if not pending:
				return True
			log(f"WARN verify round {attempt}: missing {sorted(missing) or '-'}, "
			    f"mismatch {[hex(r) for r in pending]}")
		return False

	def shoot(self, stage, cfg, shots):
		ok, attempt, skews = 0, 0, []
		counts = {}
		max_attempts = max(MAX_ATTEMPTS, math.ceil(shots * 1.5))
		while ok < shots and attempt < max_attempts:
			attempt += 1
			time.sleep(self.settle)
			if self.tx.rebooted:
				self.check_reboot()
				if not self.apply(cfg):
					raise Desync("re-apply after transmitter reboot failed")
			if self.scope.arm() == "noise":
				self.scope.inst.write("STOP")
				counts["noise_trigger"] = counts.get("noise_trigger", 0) + 1
				self.row(stage, cfg, shot=ok, attempt=attempt, status="noise_trigger", settle_s=self.settle)
				continue
			try:
				self.tx.send_burst()
			except Desync as e:
				log(f"WARN send: {e}; recovering")
				self.scope.inst.write("STOP")
				self.tx.recover({})
				continue
			if not self.scope.wait_trigger():
				status, sk, f1, f2, p1, p2 = "no_trigger", None, None, None, "", ""
			else:
				t1, v1 = self.scope.trace(1)
				t2, v2 = self.scope.trace(2)
				status, sk, f1, f2, p1, p2 = skew.leading_skew(t1, v1, t2, v2)
			counts[status] = counts.get(status, 0) + 1
			us = lambda x: "" if x is None else f"{x * 1e6:.4f}"
			self.row(stage, cfg, shot=ok, attempt=attempt, status=status, skew_us=us(sk), f1_us=us(f1),
			         f2_us=us(f2), pre1=p1, pre2=p2, settle_s=self.settle)
			if status == "ok":
				ok += 1
				skews.append(sk * 1e6)
				self.settle = max(1.0, self.settle / 1.5)
			elif status == "invalid_prestate":
				self.settle = min(20.0, self.settle * 2)  # let both receivers drift back to idle
		return skews, counts, attempt

	def run_config(self, stage, cfg, shots=SHOTS_PER_CONFIG):
		if not bw_ok(cfg):
			self.row(stage, cfg, status="skipped_bw")
			return
		t0 = time.time()
		if not self.apply(cfg):
			log(f"WARN {cfg.cid}: receivers never matched - config_failed")
			self.row(stage, cfg, status="config_failed")
			return
		skews, counts, attempts = self.shoot(stage, cfg, shots)
		bad = attempts - len(skews)
		self.bad_streak = self.bad_streak + 1 if bad * 2 >= attempts else 0
		if skews:
			log(f"{cfg.cid}: ok {len(skews)}/{attempts} median {statistics.median(skews):+.3f} "
			    f"std {statistics.pstdev(skews):.3f} max|{max(abs(s) for s in skews):.3f}| us "
			    f"{ {k: v for k, v in counts.items() if k != 'ok'} } ({time.time() - t0:.0f} s)")
		else:
			log(f"{cfg.cid}: no valid shots {counts}")
		if self.bad_streak >= 3:
			log(f"WARN {self.bad_streak} configs in a row with >=50% invalid shots")


def stage1_grid():
	agc = AGC_PRESETS["best"]
	for mod in ("ASK", "FSK", "MSK"):
		for tx in (1.0, 10, 50, 150):
			for rx in (10, 50, 160):
				for bw in (203, 325, 541):
					cfg = Config(mod, tx, rx, bw, agc)
					if mod == "MSK" and tx < 26:
						continue
					yield cfg


def load_rows(path):
	if not os.path.exists(path):
		return []
	with open(path) as f:
		return list(csv.DictReader(f))


def per_config(rows):
	out = {}
	for r in rows:
		d = out.setdefault(r["config"], {"rows": r, "skews": [], "status": {}})
		d["status"][r["status"]] = d["status"].get(r["status"], 0) + 1
		if r["status"] == "ok":
			d["skews"].append(float(r["skew_us"]))
	return out


def done_configs(rows):
	return {cid for cid, d in per_config(rows).items()
	        if len(d["skews"]) >= SHOTS_PER_CONFIG or sum(d["status"].values()) >= MAX_ATTEMPTS
	        or set(d["status"]) & {"config_failed", "skipped_bw"}}


FAIL_STATUSES = {"ch2_missing", "ch1_missing", "noise_trigger", "no_trigger"}  # not invalid_prestate


def p90_abs(s):
	a = sorted(abs(x) for x in s)
	return a[min(len(a) - 1, math.ceil(0.9 * len(a)) - 1)]


def fail_frac(d):
	fails = sum(n for k, n in d["status"].items() if k in FAIL_STATUSES)
	return fails / max(1, fails + len(d["skews"]))


def rank_key(d):
	"""Worst-case-oriented: configs that miss or noise-trigger on >30% of shots sort last, then by
	90th-percentile |skew| (a trigger cares about the bad shots, not the median), then std."""
	s = d["skews"]
	return (fail_frac(d) > 0.3, p90_abs(s), statistics.pstdev(s))


def stage2_grid(rows):
	"""Top 3 stage-1 configs per modulation (median |skew|, then std) x the other AGC presets."""
	pc = per_config([r for r in rows if r["stage"] == "1"])
	for mod in ("ASK", "FSK", "MSK"):
		cands = [(rank_key(d), d["rows"]) for d in pc.values()
		         if d["rows"]["mod"] == mod and len(d["skews"]) >= 5]
		cands.sort(key=lambda c: c[0])
		for _, r in cands[:3]:
			for name, agc in AGC_PRESETS.items():
				if name != "best":
					yield Config(mod, float(r["tx_kbaud"]), float(r["rx_kbaud"]), int(r["bw_khz"]), agc)


def summarize(path):
	pc = per_config(load_rows(path))
	lines = []
	for cid, d in pc.items():
		s = d["skews"]
		other = {k: v for k, v in d["status"].items() if k != "ok"}
		if s:
			lines.append((rank_key(d), f"{cid:42s} n {len(s):2d}  median {statistics.median(s):+8.3f}  "
			                           f"p90|{p90_abs(s):7.3f}|  std {statistics.pstdev(s):7.3f}  "
			                           f"max|{max(abs(x) for x in s):7.3f}|  fail {fail_frac(d):4.0%}  {other or ''}"))
		else:
			lines.append(((True, math.inf, math.inf), f"{cid:42s} n  0  {other}"))
	lines.sort(key=lambda x: x[0])
	print("config                                      skew C2-C1 in us (first falling edge)")
	for _, l in lines:
		print(l)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--dry-run", action="store_true")
	ap.add_argument("--summarize", action="store_true")
	ap.add_argument("--restore", action="store_true")
	ap.add_argument("--port", default=SERIAL_PORT, help="transmitter serial port")
	ap.add_argument("--shortlist", action="store_true")
	ap.add_argument("--shots", type=int, default=100, help="valid shots per config for --shortlist")
	ap.add_argument("--only", help="comma-separated 1-based SHORTLIST entries, e.g. 1,2")
	ap.add_argument("--csv")
	args = ap.parse_args()
	if args.csv is None:
		args.csv = CSV_PATH.replace("skew_sweep_", "skew_confirm_") if args.shortlist else CSV_PATH
	os.makedirs(READOUTS, exist_ok=True)
	if args.summarize:
		summarize(args.csv)
		return 0

	tx = Transmitter(args.port)
	scope = Scope(SCOPE)
	scope.setup()
	tx.op(tx.sync)
	sweep = Sweep(tx, scope, args.csv)
	rc = 0
	try:
		if args.restore:
			pass
		elif args.dry_run:
			sweep.run_config("dry", BEST, shots=3)
		elif args.shortlist:
			have = per_config(load_rows(args.csv))
			picks = [int(x) for x in args.only.split(",")] if args.only else range(1, len(SHORTLIST) + 1)
			for i in picks:
				cfg = SHORTLIST[i - 1]
				need = args.shots - len(have.get(cfg.cid, {}).get("skews", []))
				log(f"[confirm {i}/{len(SHORTLIST)}] {cfg.cid}: {max(0, need)} shots to go")
				if need > 0:
					sweep.run_config("confirm", cfg, shots=need)
		else:
			for stage, grid in (("1", lambda rows: stage1_grid()), ("2", stage2_grid)):
				rows = load_rows(args.csv)
				done = done_configs(rows)
				todo = [c for c in dict.fromkeys(grid(rows)) if c.cid not in done]
				log(f"stage {stage}: {len(todo)} configs to run")
				for i, cfg in enumerate(todo, 1):
					log(f"[{stage} {i}/{len(todo)}] {cfg.cid}")
					sweep.run_config(stage, cfg)
	except Desync as e:
		log(f"FATAL {e}")
		rc = 1
	except KeyboardInterrupt:
		log("interrupted")
		rc = 130
	finally:
		try:
			tx.sync()
			sweep.apply(BEST)
			log(f"restored {BEST.cid}")
		except Exception as e:  # noqa: BLE001 - report and leave it to the operator
			log(f"FATAL restore failed: {e}")
			rc = rc or 1
	return rc


if __name__ == "__main__":
	sys.exit(main())
