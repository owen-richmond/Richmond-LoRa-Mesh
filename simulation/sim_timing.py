#!/usr/bin/env python3
"""
sim_timing.py — Sleep-cycle accurate LoRa mesh timing simulation.

Models the real 10-second / 220ms wake-cycle behavior of RAK4630 nodes,
using Semtech's SF12 time-on-air formula, measured current draw, and a
Monte Carlo cold-start convergence model.

Four plots:
  1. LoRa time-on-air vs payload size  (SF12 vs SF9)
  2. Cold-start first-sync latency     (Monte Carlo, 1–4 hops)
  3. Battery lifetime by node role     (leaf / relay / master)
  4. Per-role duty cycle breakdown     (TX / RX / deep-sleep)

Usage:
    python sim_timing.py              # saves to simulation/results/
    python sim_timing.py ./my_output  # custom output directory
"""

import sys
import os
import math
import random
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# ── Output directory ──────────────────────────────────────────────────────────
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "results")
os.makedirs(OUT_DIR, exist_ok=True)

STYLE = {
    "figure.facecolor": "#0d1117",
    "axes.facecolor":   "#161b22",
    "axes.edgecolor":   "#30363d",
    "axes.labelcolor":  "#e6edf3",
    "xtick.color":      "#8b949e",
    "ytick.color":      "#8b949e",
    "grid.color":       "#21262d",
    "text.color":       "#e6edf3",
    "legend.facecolor": "#161b22",
    "legend.edgecolor": "#30363d",
}
plt.rcParams.update(STYLE)
ACCENT = "#58a6ff"; GREEN = "#3fb950"; ORANGE = "#d29922"
RED = "#f85149"; PURPLE = "#bc8cff"; YELLOW = "#e3b341"

print(f"Saving timing plots to: {OUT_DIR}\n", flush=True)

# ── Physical constants (RAK4630 + SX1262, 868 MHz) ───────────────────────────
CYCLE_MS      = 10_000   # 10 s sleep cycle
WAKE_MS       = 220      # nominal wake window (ms)
BW_KHZ        = 125.0    # LoRa bandwidth
PREAMBLE_SYM  = 8        # preamble symbols (firmware default)
CR            = 1        # coding rate index (1 = 4/5)
LDO           = 1        # low data rate opt (mandatory for SF12 @ 125 kHz)
CRC           = 1        # CRC enabled

# Current draw (mA) — RAK4630 / SX1262 datasheet values at 3.3 V
SLEEP_MA      = 0.0032   # nRF52840 System Off + SX1262 Sleep
RX_MA         = 8.0      # SX1262 RX continuous ~4.8 mA + nRF52840 active ~3.2 mA
TX_22DBM_MA   = 108.0    # SX1262 @ 22 dBm ~105 mA + MCU ~3 mA


def toa_ms(payload_bytes: int, sf: int) -> float:
    """
    Semtech LoRa time-on-air (ms).
    Implements the formula from Semtech AN1200.22 / LoRa Designer's Guide.
    """
    tsym = (2 ** sf) / (BW_KHZ * 1000.0) * 1000.0      # ms per symbol
    t_preamble = (PREAMBLE_SYM + 4.25) * tsym
    # Low data rate optimisation affects the denominator
    de = LDO if (sf >= 11 and BW_KHZ <= 125) else 0
    n_sym_payload = 8 + max(
        math.ceil((8 * payload_bytes - 4 * sf + 28 + 16 * CRC) /
                  (4 * (sf - 2 * de))) * (CR + 4),
        0,
    )
    t_payload = n_sym_payload * tsym
    return t_preamble + t_payload


def energy_mah(duration_ms: float, current_ma: float) -> float:
    return duration_ms / 1000.0 / 3600.0 * current_ma


# ══════════════════════════════════════════════════════════════════════════════
# 1. TIME-ON-AIR vs PAYLOAD SIZE
# ══════════════════════════════════════════════════════════════════════════════
print("Plot 1 / 4  — ToA vs payload size ...", flush=True)

payloads = list(range(8, 129, 4))
toa_sf12 = [toa_ms(p, 12) for p in payloads]
toa_sf9  = [toa_ms(p,  9) for p in payloads]

SYNC_PAYLOAD = 16
SYNC_TOA_SF12 = toa_ms(SYNC_PAYLOAD, 12)

fig, ax = plt.subplots(figsize=(9, 5))
fig.suptitle("LoRa Time-on-Air vs Payload Size (868 MHz, 125 kHz BW, 22 dBm)", color="#e6edf3")
ax.plot(payloads, toa_sf12, color=RED,    linewidth=2, label="SF12  (max range, ~500 m)")
ax.plot(payloads, toa_sf9,  color=ACCENT, linewidth=2, label="SF9   (faster, ~200 m)")
ax.axvline(SYNC_PAYLOAD, color=YELLOW, linestyle="--", linewidth=1,
           label=f"Sync payload ({SYNC_PAYLOAD} B)")
ax.axhline(WAKE_MS, color=GREEN, linestyle=":", linewidth=1.2,
           label=f"Wake window ({WAKE_MS} ms) — packet always > window")
ax.scatter([SYNC_PAYLOAD], [SYNC_TOA_SF12], color=RED, s=70, zorder=5)
ax.annotate(f"  {SYNC_TOA_SF12:.0f} ms", xy=(SYNC_PAYLOAD, SYNC_TOA_SF12),
            color=RED, fontsize=8, va="center")
ax.set_xlabel("Payload size (bytes)")
ax.set_ylabel("Time-on-Air (ms)")
ax.set_ylim(0, max(toa_sf12) * 1.1)
ax.legend(fontsize=8)
ax.grid(True, alpha=0.4)
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "T1_toa_vs_payload.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved T1_toa_vs_payload.png")


# ══════════════════════════════════════════════════════════════════════════════
# 2. COLD-START CONVERGENCE LATENCY  (Monte Carlo)
# ══════════════════════════════════════════════════════════════════════════════
print("Plot 2 / 4  — Cold-start convergence latency ...", flush=True)

# When a node is unsynced it wakes at a random phase each cycle and listens
# for a preamble.  It can detect the preamble if any part of the preamble
# overlaps its wake window.
#
# Preamble duration = (PREAMBLE_SYM + 4.25) * Tsym
# Detection needs ≥5 symbols of preamble overlap (~164 ms at SF12).
#
# Detection window (phase offsets that allow a catch):
#   phase_k in (-WAKE_MS + detect_min, preamble_duration - detect_min)
#   Clamped to [0, CYCLE_MS].
#
# P(catch per cycle) = detection_window / CYCLE_MS

TRIALS     = 2000
MAX_HOPS   = 4
TOA_SF12   = toa_ms(SYNC_PAYLOAD, 12)          # ~1319 ms
TSYM_SF12  = (2**12) / (BW_KHZ * 1000) * 1000  # ms
PREAMBLE_MS = (PREAMBLE_SYM + 4.25) * TSYM_SF12
DETECT_MIN  = 5 * TSYM_SF12                     # 5-symbol minimum
# How wide is the "catchable" window for a single cycle broadcast?
DETECT_WINDOW = max(0.0, PREAMBLE_MS - DETECT_MIN + WAKE_MS)
P_CATCH       = DETECT_WINDOW / CYCLE_MS        # ≈ 0.046

random.seed(0)
latency_by_hop = []
for hop_depth in range(1, MAX_HOPS + 1):
    latencies_s = []
    for _ in range(TRIALS):
        total_cycles = 0
        for _ in range(hop_depth):
            # Geometric: how many cycles until this tier first catches the relay?
            # After prior tier syncs, relay fires every cycle, same window.
            c = math.ceil(math.log(1.0 - random.random()) / math.log(max(1e-9, 1.0 - P_CATCH)))
            total_cycles += c
        latencies_s.append(total_cycles * CYCLE_MS / 1000.0)
    latency_by_hop.append(latencies_s)

COLORS_HOP = [GREEN, ACCENT, ORANGE, RED]
fig, ax = plt.subplots(figsize=(10, 5))
ax.set_title(
    f"Cold-Start First-Sync Latency by Hop Depth  (P(catch/cycle)={P_CATCH:.3f},  {TRIALS} trials each)",
    color="#e6edf3"
)
for i, (lats, color) in enumerate(zip(latency_by_hop, COLORS_HOP)):
    med = statistics.median(lats)
    p90 = sorted(lats)[int(0.9 * len(lats))]
    ax.hist(lats, bins=50, color=color, alpha=0.55, density=True,
            label=f"Hop {i+1}  (median {med:.0f} s, 90th pct {p90:.0f} s)")

ax.set_xlabel("Time to first sync (seconds)")
ax.set_ylabel("Probability density")
ax.legend(fontsize=8)
ax.grid(True, alpha=0.4)
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "T2_coldstart_latency.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved T2_coldstart_latency.png")


# ══════════════════════════════════════════════════════════════════════════════
# 3. BATTERY LIFETIME BY ROLE
# ══════════════════════════════════════════════════════════════════════════════
print("Plot 3 / 4  — Battery lifetime by role ...", flush=True)

# Per-cycle energy (mAh) for each role.
# Relay/master stay awake beyond the 220 ms window when actively TX/RX.
#
# Leaf    : RX=220 ms, sleep=9780 ms
# Relay   : RX=TOA (stays awake to receive), TX=TOA (then immediately relays)
# Master  : TX=TOA (broadcasts each cycle), RX=220 ms (listens for RTR)

JITTER_AVG_MS = 15.0  # expected jitter before relay
TOA_MS        = SYNC_TOA_SF12

e_leaf_cycle   = energy_mah(WAKE_MS, RX_MA) + energy_mah(CYCLE_MS - WAKE_MS, SLEEP_MA)

relay_rx_ms    = TOA_MS                              # receive full packet
relay_tx_ms    = TOA_MS                              # then relay
relay_sleep_ms = CYCLE_MS - relay_rx_ms - relay_tx_ms - JITTER_AVG_MS
e_relay_cycle  = (energy_mah(relay_rx_ms, RX_MA) +
                  energy_mah(relay_tx_ms, TX_22DBM_MA) +
                  energy_mah(max(0, relay_sleep_ms), SLEEP_MA))

master_tx_ms    = TOA_MS
master_rx_ms    = WAKE_MS                            # listen for RTR requests
master_sleep_ms = CYCLE_MS - master_tx_ms - master_rx_ms
e_master_cycle  = (energy_mah(master_tx_ms, TX_22DBM_MA) +
                   energy_mah(master_rx_ms, RX_MA) +
                   energy_mah(max(0, master_sleep_ms), SLEEP_MA))

CYCLES_PER_HOUR = 3600.0 / (CYCLE_MS / 1000.0)
roles = ["Leaf\n(no relay)", "Relay\n(every cycle)", "Master\n(TX every cycle)"]
e_per_hour = [
    e_leaf_cycle   * CYCLES_PER_HOUR,
    e_relay_cycle  * CYCLES_PER_HOUR,
    e_master_cycle * CYCLES_PER_HOUR,
]
battery_mah = [1_000, 3_000, 10_000]
batt_labels = ["1 000 mAh\n(small LiPo)", "3 000 mAh\n(large LiPo)", "10 000 mAh\n(6×AA pack)"]
batt_colors = [ACCENT, GREEN, PURPLE]

x = np.arange(len(roles))
width = 0.25
fig, ax = plt.subplots(figsize=(10, 5))
ax.set_title("Projected Battery Lifetime by Node Role (SF12, 10 s cycle, 22 dBm TX)", color="#e6edf3")
for i, (cap, label, col) in enumerate(zip(battery_mah, batt_labels, batt_colors)):
    days = [(cap / e_hr) / 24.0 for e_hr in e_per_hour]
    bars = ax.bar(x + (i - 1) * width, days, width, color=col, alpha=0.85,
                  edgecolor="#30363d", linewidth=0.5, label=label)
    for bar, d in zip(bars, days):
        label_str = f"{d:.0f} d" if d < 365 else f"{d/365:.1f} yr"
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 1.5,
                label_str, ha="center", va="bottom", fontsize=7, color="#e6edf3")

ax.set_yscale("log")
ax.set_xticks(x)
ax.set_xticklabels(roles, fontsize=9)
ax.set_ylabel("Battery life (days, log scale)")
ax.legend(fontsize=8)
ax.grid(True, alpha=0.3, axis="y")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "T3_battery_lifetime.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved T3_battery_lifetime.png")


# ══════════════════════════════════════════════════════════════════════════════
# 4. DUTY CYCLE BREAKDOWN BY ROLE
# ══════════════════════════════════════════════════════════════════════════════
print("Plot 4 / 4  — Duty cycle breakdown ...", flush=True)

# Express each phase as % of one 10 s cycle.
def pct(ms):
    return 100.0 * ms / CYCLE_MS

duty = {
    "Leaf":   {"TX": 0,             "RX": pct(WAKE_MS),      "Sleep": pct(CYCLE_MS - WAKE_MS)},
    "Relay":  {"TX": pct(TOA_MS),   "RX": pct(TOA_MS),       "Sleep": pct(max(0, relay_sleep_ms))},
    "Master": {"TX": pct(TOA_MS),   "RX": pct(master_rx_ms), "Sleep": pct(max(0, master_sleep_ms))},
}

phases  = ["TX (22 dBm)", "RX active", "Deep sleep"]
p_colors = [RED, ACCENT, "#3d444d"]
fig, ax = plt.subplots(figsize=(8, 5))
ax.set_title("Per-Role Radio Duty Cycle  (one 10 s wake-cycle)", color="#e6edf3")

role_labels = list(duty.keys())
bottoms = [0.0] * len(role_labels)
for phase, color in zip(["TX", "RX", "Sleep"], p_colors):
    vals = [duty[r][phase] for r in role_labels]
    bars = ax.bar(role_labels, vals, bottom=bottoms, color=color,
                  edgecolor="#30363d", linewidth=0.5,
                  label="TX (22 dBm)" if phase == "TX" else
                        "RX active" if phase == "RX" else "Deep sleep")
    for bar, v, bot in zip(bars, vals, bottoms):
        if v > 1.5:
            ax.text(bar.get_x() + bar.get_width() / 2, bot + v / 2,
                    f"{v:.1f}%", ha="center", va="center", fontsize=8, color="white", fontweight="bold")
    bottoms = [b + v for b, v in zip(bottoms, vals)]

ax.set_ylabel("% of 10 s cycle")
ax.set_ylim(0, 110)
ax.legend(fontsize=9, loc="upper right")
ax.grid(True, alpha=0.3, axis="y")

# Annotate total awake %
for i, role in enumerate(role_labels):
    awake = duty[role]["TX"] + duty[role]["RX"]
    ax.text(i, 102, f"Awake: {awake:.1f}%", ha="center", va="bottom", fontsize=8, color=YELLOW)

plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "T4_duty_cycle.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved T4_duty_cycle.png")

print(f"\nAll timing plots saved to: {OUT_DIR}")
print(f"  T1_toa_vs_payload.png      — SF12 vs SF9 time-on-air (sync packet = {SYNC_TOA_SF12:.0f} ms)")
print(f"  T2_coldstart_latency.png   — first-sync time per hop depth (P(catch)={P_CATCH:.3f}/cycle)")
print(f"  T3_battery_lifetime.png    — days of life: leaf {1000/e_leaf_cycle/CYCLES_PER_HOUR/24:.0f} d  vs relay {1000/e_relay_cycle/CYCLES_PER_HOUR/24:.0f} d (1000 mAh)")
print(f"  T4_duty_cycle.png          — TX/RX/sleep breakdown by role")
