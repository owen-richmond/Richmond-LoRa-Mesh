#!/usr/bin/env python3
"""
Headless mesh simulation analysis.
Runs five experiment scenarios and saves plots as PNG files.

Usage:
    python sim_analysis.py              # saves to simulation/results/
    python sim_analysis.py ./my_output  # saves to a custom directory

No GUI or display required.
"""

import sys
import os
import math
import random
import statistics

import matplotlib
matplotlib.use("Agg")   # non-interactive backend, no display needed
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

# ── Import the simulation model (not the GUI) ─────────────────────────────────
sys.path.insert(0, os.path.dirname(__file__))
from mesh_sim import (
    SimNetwork, SimNode, NodeState, FIELD_SIZE, DEF_RANGE,
    NOISE_FLOOR_DBM, SNR_THRESHOLD_DB, LORA_TX_POWER_DBM,
    PL_1M_DB, PATH_LOSS_EXP, SIM_METERS_PER_PX, FAST_FADE_STD_DB,
    _snr, _per,
)

# ── Output directory ───────────────────────────────────────────────────────────
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

ACCENT   = "#58a6ff"
GREEN    = "#3fb950"
ORANGE   = "#d29922"
RED      = "#f85149"
PURPLE   = "#bc8cff"
YELLOW   = "#e3b341"

print(f"Saving plots to: {OUT_DIR}\n", flush=True)

# ══════════════════════════════════════════════════════════════════════════════
# 1. PHY ERROR RATE vs DISTANCE
# ══════════════════════════════════════════════════════════════════════════════
print("Running: 1 / 5  PHY error rate vs distance ...", flush=True)

distances_px   = range(5, 235, 2)
distances_m    = [d * SIM_METERS_PER_PX for d in distances_px]
rssi_values    = []
snr_values     = []
per_values     = []

dummy = SimNode(0, 0, 0)   # no shadow offset, clean signal
for d_px in distances_px:
    d_m   = max(1.0, d_px * SIM_METERS_PER_PX)
    pl    = PL_1M_DB + 10.0 * PATH_LOSS_EXP * math.log10(d_m)
    rssi  = LORA_TX_POWER_DBM - pl
    snr   = _snr(rssi)
    per   = _per(snr)
    rssi_values.append(rssi)
    snr_values.append(snr)
    per_values.append(per * 100)

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 7), sharex=True)
fig.suptitle("Signal Model: PHY Error Rate vs Distance (868 MHz, SF12, 22 dBm)", color="#e6edf3")

ax1.plot(distances_m, rssi_values, color=ACCENT, linewidth=2, label="RSSI (dBm)")
ax1.axhline(NOISE_FLOOR_DBM + SNR_THRESHOLD_DB, color=RED, linestyle="--",
            linewidth=1.2, label=f"Sensitivity floor ({NOISE_FLOOR_DBM + SNR_THRESHOLD_DB:.0f} dBm)")
ax1.axhline(NOISE_FLOOR_DBM, color=ORANGE, linestyle=":", linewidth=1,
            label=f"Noise floor ({NOISE_FLOOR_DBM:.0f} dBm)")
ax1.set_ylabel("RSSI (dBm)")
ax1.legend(fontsize=8)
ax1.grid(True, alpha=0.4)
ax1.set_ylim(-160, -60)

ax2.plot(distances_m, per_values, color=RED, linewidth=2, label="Packet Error Rate (%)")
ax2.axvline(217, color=YELLOW, linestyle="--", linewidth=1,
            label="~217 m: PER < 1%")
ax2.axvline(325, color=ORANGE, linestyle="--", linewidth=1,
            label="~325 m: PER = 50%")
# Measured outdoor data point: 1.03 km two-hop link, 16% end-to-end loss
ax2.scatter([1030], [16], color=GREEN, s=80, zorder=5,
            label="Measured: 1.03 km outdoor (16% end-to-end)")
ax2.set_xlabel("Distance (m)")
ax2.set_ylabel("Packet Error Rate (%)")
ax2.set_ylim(-2, 102)
ax2.legend(fontsize=8)
ax2.grid(True, alpha=0.4)

plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "1_per_vs_distance.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved 1_per_vs_distance.png")


# ══════════════════════════════════════════════════════════════════════════════
# 2. SYNC CONVERGENCE — multiple node counts, averaged over trials
# ══════════════════════════════════════════════════════════════════════════════
print("Running: 2 / 5  Sync convergence by node count ...", flush=True)

NODE_COUNTS  = [10, 20, 30]
TICKS        = 200
TRIALS       = 3
COLORS_CONV  = [GREEN, ACCENT, PURPLE]

fig, ax = plt.subplots(figsize=(9, 5))
ax.set_title("Sync Convergence from Cold Start", color="#e6edf3")

for count, color in zip(NODE_COUNTS, COLORS_CONV):
    runs = []
    for _ in range(TRIALS):
        net = SimNetwork()
        net.fast_fade_enabled = False
        net.reset(count)
        series = []
        for t in range(TICKS):
            net.step()
            st = net.stats()
            series.append(st["sync_pct_alive"])
        runs.append(series)
    mean_series = [statistics.mean(run[t] for run in runs) for t in range(TICKS)]
    lo_series   = [min(run[t] for run in runs)             for t in range(TICKS)]
    hi_series   = [max(run[t] for run in runs)             for t in range(TICKS)]
    ticks_axis  = list(range(TICKS))
    ax.plot(ticks_axis, mean_series, color=color, linewidth=2, label=f"{count} nodes (avg of {TRIALS})")
    ax.fill_between(ticks_axis, lo_series, hi_series, color=color, alpha=0.12)

ax.axhline(95, color="#8b949e", linestyle="--", linewidth=1, label="95% threshold")
ax.set_xlabel("Simulation Tick")
ax.set_ylabel("Synced Nodes (% of alive)")
ax.set_ylim(0, 105)
ax.legend(fontsize=9)
ax.grid(True, alpha=0.4)
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "2_sync_convergence.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved 2_sync_convergence.png")


# ══════════════════════════════════════════════════════════════════════════════
# 3. FORWARDING LOAD DISTRIBUTION
# ══════════════════════════════════════════════════════════════════════════════
print("Running: 3 / 5  Forwarding load distribution ...", flush=True)

def run_and_collect_fwd(n_nodes, ticks=500, seed=42):
    random.seed(seed)
    net = SimNetwork()
    net.fast_fade_enabled = False
    net.reset(n_nodes)
    for _ in range(ticks):
        net.step()
    counts = sorted(
        [(n.id, n.forward_count) for n in net.nodes if n.state != NodeState.DEAD],
        key=lambda x: x[1], reverse=True
    )
    return counts, net.stats()

counts, st = run_and_collect_fwd(20, ticks=600)
node_ids    = [f"N{c[0]}" for c in counts]
fwd_counts  = [c[1] for c in counts]
total_fwd   = max(1, sum(fwd_counts))
shares      = [100.0 * c / total_fwd for c in fwd_counts]

fig, ax = plt.subplots(figsize=(11, 5))
ax.set_title("Per-Node Forwarding Share (20 nodes, 600 ticks)", color="#e6edf3")
bar_colors  = [RED if s > 30 else ORANGE if s > 15 else ACCENT for s in shares]
bars = ax.bar(node_ids, shares, color=bar_colors, edgecolor="#30363d", linewidth=0.5)
ax.axhline(100.0 / len(counts), color=GREEN, linestyle="--", linewidth=1.2,
           label=f"Equal-share baseline ({100.0/len(counts):.1f}%)")
ax.set_xlabel("Node ID (sorted by load)")
ax.set_ylabel("Share of total forwards (%)")
ax.legend(fontsize=9)
ax.grid(True, alpha=0.3, axis="y")
# Annotate the top node
ax.text(0, shares[0] + 0.5, f"{shares[0]:.1f}%", color=RED, fontsize=8,
        ha="center", va="bottom")
plt.xticks(fontsize=8)
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "3_forwarding_distribution.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved 3_forwarding_distribution.png")


# ══════════════════════════════════════════════════════════════════════════════
# 4. SCALABILITY — ticks to reach 95% sync vs node count
# ══════════════════════════════════════════════════════════════════════════════
print("Running: 4 / 5  Scalability (ticks to 80% sync) ...", flush=True)

SCALE_COUNTS  = [5, 10, 15, 20, 25]
SCALE_TRIALS  = 3
RUN_TICKS     = 200   # run each trial to steady state, then read final metrics

sync_at_end   = []
coll_at_end   = []
phy_at_end    = []

for count in SCALE_COUNTS:
    trial_sync, trial_coll, trial_phy = [], [], []
    for trial in range(SCALE_TRIALS):
        random.seed(trial * 100 + count)
        net = SimNetwork()
        net.fast_fade_enabled = False
        net.reset(count)
        for _ in range(RUN_TICKS):
            net.step()
        st = net.stats()
        trial_sync.append(st["sync_pct_alive"])
        trial_coll.append(st["collision_pct"])
        trial_phy.append(st["phy_error_pct"])
    sync_at_end.append(statistics.mean(trial_sync))
    coll_at_end.append(statistics.mean(trial_coll))
    phy_at_end.append(statistics.mean(trial_phy))
    print(f"   {count:3d} nodes -> sync={sync_at_end[-1]:.1f}%  "
          f"coll={coll_at_end[-1]:.1f}%  PHY_err={phy_at_end[-1]:.1f}%", flush=True)

fig, ax1 = plt.subplots(figsize=(9, 5))
ax1.set_title(f"Scalability: Steady-State Metrics at Tick {RUN_TICKS} (avg {SCALE_TRIALS} trials)",
              color="#e6edf3")
ax1.plot(SCALE_COUNTS, sync_at_end, color=GREEN, linewidth=2, marker="o",
         markersize=6, label="Sync % of alive nodes")
ax2 = ax1.twinx()
ax2.set_facecolor("#161b22")
ax2.plot(SCALE_COUNTS, coll_at_end, color=ORANGE, linewidth=2, marker="s",
         markersize=5, linestyle="--", label="Collision rate %")
ax2.plot(SCALE_COUNTS, phy_at_end,  color=RED,    linewidth=2, marker="^",
         markersize=5, linestyle=":",  label="PHY error rate %")
ax1.set_xlabel("Number of Nodes")
ax1.set_ylabel("Sync % (green, left axis)", color=GREEN)
ax2.set_ylabel("Error rates % (right axis)")
ax1.set_ylim(0, 105)
lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax1.legend(lines1 + lines2, labels1 + labels2, fontsize=9, loc="lower left")
ax1.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "4_scalability.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved 4_scalability.png")


# ══════════════════════════════════════════════════════════════════════════════
# 5. FAULT INJECTION & ORPHAN RECOVERY
# ══════════════════════════════════════════════════════════════════════════════
print("Running: 5 / 5  Fault injection and orphan recovery ...", flush=True)

FAULT_NODES   = 12
WARMUP_TICKS  = 80
FAULT_TICK    = 80
TOTAL_TICKS   = 200
KILL_COUNT    = 3    # relay nodes killed mid-run
FAULT_TRIALS  = 3

all_sync  = []
all_orph  = []
all_phy   = []
all_coll  = []

for trial in range(FAULT_TRIALS):
    random.seed(trial + 99)
    net = SimNetwork()
    net.fast_fade_enabled = False
    net.reset(FAULT_NODES)
    sync_s, orph_s, phy_s, coll_s = [], [], [], []
    for t in range(TOTAL_TICKS):
        if t == FAULT_TICK:
            net.kill_random(KILL_COUNT)
        net.step()
        st = net.stats()
        sync_s.append(st["sync_pct_alive"])
        orph_s.append(100.0 * st["orphans"] / max(1, st["alive"]))
        phy_s.append(st["phy_error_pct"])
        coll_s.append(st["collision_pct"])
    all_sync.append(sync_s)
    all_orph.append(orph_s)
    all_phy.append(phy_s)
    all_coll.append(coll_s)

def avg_series(runs):
    return [statistics.mean(r[t] for r in runs) for t in range(TOTAL_TICKS)]

taxis = list(range(TOTAL_TICKS))
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
fig.suptitle(
    f"Fault Injection: {KILL_COUNT} nodes killed at tick {FAULT_TICK} "
    f"({FAULT_NODES} total, avg of {FAULT_TRIALS} trials)",
    color="#e6edf3"
)

ax1.plot(taxis, avg_series(all_sync), color=GREEN,  linewidth=2, label="Sync % (alive)")
ax1.plot(taxis, avg_series(all_orph), color=ORANGE, linewidth=2, label="Orphan % (alive)")
ax1.axvline(FAULT_TICK, color=RED, linestyle="--", linewidth=1.2, label=f"Fault at tick {FAULT_TICK}")
ax1.set_ylabel("% of alive nodes")
ax1.set_ylim(-2, 110)
ax1.legend(fontsize=9)
ax1.grid(True, alpha=0.4)

ax2.plot(taxis, avg_series(all_phy),  color=PURPLE, linewidth=2, label="PHY error rate (%)")
ax2.plot(taxis, avg_series(all_coll), color=RED,    linewidth=2, label="Collision rate (%)")
ax2.axvline(FAULT_TICK, color=RED, linestyle="--", linewidth=1.2)
ax2.set_xlabel("Simulation Tick")
ax2.set_ylabel("Error rate (%)")
ax2.legend(fontsize=9)
ax2.grid(True, alpha=0.4)

plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "5_fault_recovery.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved 5_fault_recovery.png")


# ── Summary ────────────────────────────────────────────────────────────────────
print(f"\nAll done. Five plots saved to: {OUT_DIR}")
print("  1_per_vs_distance.png       — RSSI and packet error rate vs distance")
print("  2_sync_convergence.png      — sync speed for 10/20/40 nodes")
print("  3_forwarding_distribution.png — per-node relay load (who does the work)")
print("  4_scalability.png           — ticks to 95% sync across node counts")
print("  5_fault_recovery.png        — sync rate and error rates after node failure")
