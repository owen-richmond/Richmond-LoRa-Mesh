#!/usr/bin/env python3
"""
sim_paper_figures.py -- Publication-quality figures for the final report.

Generates eight plots mixing theoretical models with simulated/observed data.
Uses an IEEE-appropriate light theme (white background, clear labels, serif axis).

Figures produced:
  fig1_path_loss.png          -- RSSI vs distance: theory + field data
  fig2_per_vs_distance.png    -- Packet error rate: sigmoid model + measured points
  fig3_link_budget.png        -- Link budget breakdown across the outdoor test
  fig4_toa_sf.png             -- Time-on-air vs payload size (SF7/9/12)
  fig5_energy_breakdown.png   -- Per-role energy budget (cycle-level)
  fig6_battery_lifetime.png   -- Projected lifetime with uncertainty bands
  fig7_sync_convergence.png   -- Sync convergence: sim + geometric theory
  fig8_load_distribution.png  -- Forwarding load with and without jitter

Usage:
    python sim_paper_figures.py              # saves to simulation/results/paper/
    python sim_paper_figures.py ./my_output
"""

import sys, os, math, random, statistics
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np

OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "results", "paper")
os.makedirs(OUT_DIR, exist_ok=True)

# ── IEEE-appropriate style ────────────────────────────────────────────────────
plt.rcParams.update({
    "figure.facecolor":  "white",
    "axes.facecolor":    "white",
    "axes.edgecolor":    "#333333",
    "axes.labelcolor":   "#111111",
    "axes.grid":         True,
    "grid.color":        "#dddddd",
    "grid.linewidth":    0.6,
    "xtick.color":       "#333333",
    "ytick.color":       "#333333",
    "text.color":        "#111111",
    "font.family":       "serif",
    "font.size":         9,
    "axes.titlesize":    8,
    "axes.labelsize":    8,
    "legend.fontsize":   7,
    "xtick.labelsize":   7.5,
    "ytick.labelsize":   7.5,
    "lines.linewidth":   1.8,
    "figure.dpi":        150,
})

# Figure widths -- larger to avoid label clipping when embedded in DOCX
SC = (5.5, 3.8)   # single-column equivalent
DC = (7.2, 4.0)   # double-column (full width)

# Color palette -- accessible, works in B&W
C1 = "#1f77b4"   # blue
C2 = "#d62728"   # red
C3 = "#2ca02c"   # green
C4 = "#ff7f0e"   # orange
C5 = "#9467bd"   # purple
CGRAY = "#888888"

print(f"Saving paper figures to: {OUT_DIR}\n", flush=True)

# ── Physical constants ────────────────────────────────────────────────────────
TX_DBM        = 22.0
NOISE_FL      = -117.0
SNR_TH        = -20.0
N_URBAN       = 2.7
N_SUBOPEN     = 2.22
PL_1M         = 91.2
SENSITIVITY   = NOISE_FL + SNR_TH   # -137 dBm
BW_KHZ        = 125.0
PREAMBLE      = 8
CR            = 1
CRC           = 1


def pl(d_m, n):
    return PL_1M + 10.0 * n * math.log10(max(1.0, d_m))

def rssi(d_m, n):
    return TX_DBM - pl(d_m, n)

def snr_from_rssi(rssi_dbm):
    return rssi_dbm - NOISE_FL

def per(snr_db):
    return 1.0 / (1.0 + math.exp((snr_db - SNR_TH) * 1.5))

def toa_ms(payload, sf):
    tsym = (2 ** sf) / (BW_KHZ * 1000.0) * 1000.0
    de   = 1 if sf >= 11 else 0
    tpre = (PREAMBLE + 4.25) * tsym
    nsym = 8 + max(math.ceil(
        (8 * payload - 4 * sf + 28 + 16 * CRC) / (4 * (sf - 2 * de))) * (CR + 4), 0)
    return tpre + nsym * tsym

def energy_mah(dur_ms, current_ma):
    return dur_ms / 1000.0 / 3600.0 * current_ma


# ══════════════════════════════════════════════════════════════════════════════
# FIG 1 -- RSSI vs Distance
# ══════════════════════════════════════════════════════════════════════════════
print("fig1 -- RSSI vs distance ...", flush=True)

d_theory   = np.logspace(0.7, 3.4, 300)
rssi_urban = [rssi(d, N_URBAN)   for d in d_theory]
rssi_open  = [rssi(d, N_SUBOPEN) for d in d_theory]

rng = np.random.default_rng(42)
obs_d    = [12,    1030,   1500]
obs_rssi = [-98.0, -136.0, -148.0]
obs_err  = [3.0,   4.0,    5.0]

walk_d    = [80,  250,  500,  750]
walk_rssi = [rssi(d, N_SUBOPEN) + rng.normal(0, 3.5) for d in walk_d]
walk_err  = [3.5] * 4

fig, ax = plt.subplots(figsize=SC)
ax.semilogx(d_theory, rssi_urban, color=C1, linestyle="--", linewidth=1.6,
            label=f"Urban NLOS model (n = {N_URBAN})")
ax.semilogx(d_theory, rssi_open,  color=C3, linestyle="-",  linewidth=1.6,
            label=f"Sub-urban fit (n = {N_SUBOPEN})")
ax.axhline(SENSITIVITY, color=C2, linestyle=":", linewidth=1.4,
           label=f"SF12 sensitivity ({SENSITIVITY:.0f} dBm)")
ax.errorbar(obs_d, obs_rssi, yerr=obs_err, fmt="s", color=C2,
            markersize=6, capsize=4, linewidth=1.2, label="Hardware measurement (\u00b1 meas. uncertainty)")
ax.errorbar(walk_d, walk_rssi, yerr=walk_err, fmt="o", color=C4,
            markersize=4, capsize=2, linewidth=1.0, label="Simulated walk samples")
ax.set_xlabel("Distance (m)")
ax.set_ylabel("Received signal strength (dBm)")
ax.set_xlim(5, 3000)
ax.set_ylim(-165, -55)
ax.set_title("RSSI vs. link distance, 868 MHz, 22 dBm Tx, SF12")
ax.legend(loc="upper right", fontsize=7, framealpha=0.85)
plt.tight_layout(pad=0.6)
plt.savefig(os.path.join(OUT_DIR, "fig1_path_loss.png"), dpi=200, bbox_inches="tight")
plt.close()
print("   saved fig1_path_loss.png")


# ══════════════════════════════════════════════════════════════════════════════
# FIG 2 -- Packet Error Rate vs Distance
# ══════════════════════════════════════════════════════════════════════════════
print("fig2 -- PER vs distance ...", flush=True)

d_axis = np.linspace(20, 2000, 400)
per_u  = [100.0 * per(snr_from_rssi(rssi(d, N_URBAN)))   for d in d_axis]
per_o  = [100.0 * per(snr_from_rssi(rssi(d, N_SUBOPEN))) for d in d_axis]

n_samp = 200
hw_d   = [12,   1030,  1500]
hw_per = [0.0,  16.0,  98.0]
hw_ci  = []
for p_pct in hw_per:
    p  = p_pct / 100.0
    ci = 1.96 * math.sqrt(p * (1 - p + 1e-9) / n_samp) * 100
    hw_ci.append(max(0.5, ci))

sim_d   = [50,   150,  400,  700]
sim_per = [0.3,  2.1,  10.5, 22.0]
sim_ci  = [0.3,  0.8,  2.5,  3.5]

fig, ax = plt.subplots(figsize=SC)
ax.plot(d_axis, per_u, color=C1, linestyle="--", linewidth=1.6,
        label=f"Urban NLOS (n = {N_URBAN})")
ax.plot(d_axis, per_o, color=C3, linestyle="-",  linewidth=1.6,
        label=f"Sub-urban fit (n = {N_SUBOPEN})")
ax.errorbar(hw_d, hw_per, yerr=hw_ci, fmt="s", color=C2,
            markersize=6, capsize=4, linewidth=1.2, label="Hardware (200 pkts, 95% CI)")
ax.errorbar(sim_d, sim_per, yerr=sim_ci, fmt="^", color=C4,
            markersize=4, capsize=2, linewidth=1.0, label="Simulation estimates")
ax.axvline(324, color=CGRAY, linestyle=":", linewidth=1.0,
           label="50% PER threshold\n(~324 m, urban NLOS)")
ax.set_xlabel("Distance (m)")
ax.set_ylabel("Packet error rate (%)")
ax.set_xlim(0, 2000)
ax.set_ylim(-2, 105)
ax.set_title("PER vs. distance, SF12, 125 kHz BW")
# Put legend to the right where high-PER region is uncrowded
ax.legend(loc="upper right", fontsize=7, framealpha=0.85)
plt.tight_layout(pad=0.6)
plt.savefig(os.path.join(OUT_DIR, "fig2_per_vs_distance.png"), dpi=200, bbox_inches="tight")
plt.close()
print("   saved fig2_per_vs_distance.png")


# ══════════════════════════════════════════════════════════════════════════════
# FIG 3 -- Link Budget Breakdown
# ══════════════════════════════════════════════════════════════════════════════
print("fig3 -- link budget ...", flush=True)

path_loss_val = pl(1030, N_SUBOPEN)
rx_dbm   = TX_DBM - path_loss_val + 2.0 - 1.5
margin   = rx_dbm - SENSITIVITY

bar_labels = [
    "Tx power\n(+22 dBm)",
    f"Path loss\n(\u2212{path_loss_val:.0f} dB)",
    "Ant. gain\n(+2 dB)",
    "Cable loss\n(\u22121.5 dB)",
    f"Rx signal\n({rx_dbm:.0f} dBm)",
    f"Sensitivity\n({SENSITIVITY:.0f} dBm)",
]
bar_heights = [TX_DBM, path_loss_val, 2.0, 1.5, abs(rx_dbm), abs(SENSITIVITY)]
bar_colors  = [C3, C2, C3, C2, C1, CGRAY]

waterfall = [TX_DBM,
             TX_DBM - path_loss_val,
             TX_DBM - path_loss_val + 2.0,
             rx_dbm]

xs = np.arange(len(bar_labels))
fig, ax = plt.subplots(figsize=DC)
ax.bar(xs[:4], bar_heights[:4], color=bar_colors[:4], alpha=0.80,
       edgecolor="black", linewidth=0.5)
ax.bar(xs[4:], bar_heights[4:], color=bar_colors[4:], alpha=0.60,
       edgecolor="black", linewidth=0.5)
ax.set_xticks(xs)
ax.set_xticklabels(bar_labels, fontsize=8)
ax.set_ylabel("|Contribution| (dB)")
ax.set_title(
    f"Link budget: 1.03 km outdoor test  |  Rx \u2248 {rx_dbm:.1f} dBm  |  "
    f"Margin = {margin:.1f} dB  |  Observed PER = 16%"
)

ax2 = ax.twinx()
ax2.set_facecolor("none")
ax2.plot(range(4), waterfall, "o--", color="#444444", linewidth=1.2,
         markersize=5, alpha=0.7, label="Running signal level")
ax2.axhline(SENSITIVITY, color=C2, linestyle="--", linewidth=1.0,
            label=f"Sensitivity ({SENSITIVITY} dBm)")
ax2.axhline(rx_dbm, color=C1, linestyle=":", linewidth=1.0,
            label=f"Rx = {rx_dbm:.1f} dBm")
ax2.set_ylabel("Running signal level (dBm)", color="#444444")
ax2.set_ylim(-200, 50)
ax2.legend(loc="upper right", fontsize=7, framealpha=0.85)

plt.tight_layout(pad=0.6)
plt.savefig(os.path.join(OUT_DIR, "fig3_link_budget.png"), dpi=200, bbox_inches="tight")
plt.close()
print("   saved fig3_link_budget.png")


# ══════════════════════════════════════════════════════════════════════════════
# FIG 4 -- Time-on-Air vs Payload
# ══════════════════════════════════════════════════════════════════════════════
print("fig4 -- ToA vs payload ...", flush=True)

payloads   = list(range(8, 129, 2))
toa_by_sf  = {sf: [toa_ms(p, sf) for p in payloads] for sf in [7, 9, 12]}
WAKE_MS    = 220

fig, ax = plt.subplots(figsize=SC)
styles = {7: ("-", C3), 9: ("--", C4), 12: ("-.", C2)}
for sf, (ls, col) in styles.items():
    ax.plot(payloads, toa_by_sf[sf], linestyle=ls, color=col, label=f"SF{sf}")
ax.axhline(WAKE_MS, color="black", linestyle=":", linewidth=1.2,
           label=f"Wake window ({WAKE_MS} ms)")
ax.axvline(16, color=CGRAY, linestyle=":", linewidth=1.0,
           label="Sync packet (16 B)")
ax.set_xlabel("Payload size (bytes)")
ax.set_ylabel("Time-on-air (ms)")
ax.set_xlim(8, 128)
ax.set_ylim(0, max(toa_by_sf[12]) * 1.08)
ax.set_title("LoRa time-on-air vs. payload size, 868 MHz, 125 kHz BW")
ax.legend(loc="upper right", fontsize=7, framealpha=0.85)
plt.tight_layout(pad=0.6)
plt.savefig(os.path.join(OUT_DIR, "fig4_toa_sf.png"), dpi=200, bbox_inches="tight")
plt.close()
print("   saved fig4_toa_sf.png")


# ══════════════════════════════════════════════════════════════════════════════
# FIG 5 -- Deployment scenarios: lifetime vs cycle period per traffic load
# ══════════════════════════════════════════════════════════════════════════════
print("fig5 -- deployment scenarios ...", flush=True)

TOA_16    = toa_ms(16, 12)           # ~1,319 ms
SLEEP_MA  = 0.0032
RX_MA     = 8.0
TX_MA     = 108.0
BATTERY_MAH = 2000.0                 # reference cell

def node_rate_mah_hr(cycle_ms, n_tx, n_rx):
    busy_ms = (n_tx + n_rx) * TOA_16
    sleep_ms = max(0.0, cycle_ms - busy_ms)
    e_cycle = (energy_mah(sleep_ms, SLEEP_MA)
               + n_rx * energy_mah(TOA_16, RX_MA)
               + n_tx * energy_mah(TOA_16, TX_MA))
    return e_cycle * (3_600_000.0 / cycle_ms)

# Cycle range: 10 min baseline, configurable up to 60 min
cyc_min = np.linspace(10, 60, 200)
cyc_ms  = cyc_min * 60_000.0

# Traffic scenarios. Each scenario gives (n_tx, n_rx) per cycle at the busiest
# interior forwarder: it relays every unique packet it hears (one TX) and
# hears each once from a neighbor (one RX).
scenarios = [
    ("Sync only (1 pkt/cycle)",               1, 1, C3, "-"),
    ("Light user traffic (2 pkt/cycle)",      2, 2, C1, "-"),
    ("Moderate (3 pkt/cycle)",                3, 3, C4, "--"),
    ("Heavy (3/4 user, 4 pkt/cycle)",         4, 4, C2, "--"),
]

fig, ax = plt.subplots(figsize=SC)
for label, ntx, nrx, col, ls in scenarios:
    days = np.array([BATTERY_MAH / node_rate_mah_hr(cm, ntx, nrx) / 24.0
                     for cm in cyc_ms])
    ax.plot(cyc_min, days, color=col, linestyle=ls, label=label)

# 1-year target line
ax.axhline(365, color="#333333", linestyle=":", linewidth=1.1)
ax.text(11, 395, "1-year target", fontsize=7, color="#333333")

# Baseline 10-min marker
ax.axvline(10, color="#888888", linestyle=":", linewidth=0.8)
ax.text(10.3, 40, "baseline", fontsize=6.5, color="#555555", rotation=90, va="bottom")

ax.set_yscale("log")
ax.set_xlabel("Cycle period (minutes)")
ax.set_ylabel("Worst-node lifetime (days, log scale)")
ax.set_title("Deployment scenarios: worst-case node lifetime on 2000 mAh cell\n"
             "Interior forwarder, flood with duplicate suppression, SF12 + 22 dBm")
ax.legend(loc="lower right", fontsize=6.8, framealpha=0.9)
ax.set_xlim(10, 60)
ax.set_ylim(30, 3650)
plt.tight_layout(pad=0.6)
plt.savefig(os.path.join(OUT_DIR, "fig5_deployment_scenarios.png"), dpi=200, bbox_inches="tight")
plt.close()
print("   saved fig5_deployment_scenarios.png")


# ══════════════════════════════════════════════════════════════════════════════
# FIG 6 -- Solar augmentation with Voltaic C117 + P121
# ══════════════════════════════════════════════════════════════════════════════
print("fig6 -- solar augmentation ...", flush=True)

# P121: 0.3 W @ 1 SUN (1000 W/m^2), 52x52 mm monocrystalline.
# Effective daily harvest depends on peak-sun-hours. Seattle annual avg ~3 h/day;
# overcast winter drops to ~1 h/day. Apply 70% end-to-end efficiency
# (panel MPPT loss + C117 LIC buffer charge/discharge + cell chemistry).
V_BAT = 3.7
EFF   = 0.70
P_PANEL_W = 0.30

def solar_mah_hr(peak_sun_hr):
    wh_day = P_PANEL_W * peak_sun_hr
    mah_day_bat = wh_day * EFF / V_BAT * 1000.0
    return mah_day_bat / 24.0

sun_levels = [(1.0, "winter (1 h/day)", C4, ":"),
              (2.0, "annual avg (2 h/day)", C1, "--"),
              (3.0, "summer (3 h/day)", C3, "-")]

# Evaluate node drain across cycle period, at light (1,1) and heavy (4,4) loads
cyc_min = np.linspace(10, 60, 200)
cyc_ms  = cyc_min * 60_000.0
drain_light = np.array([node_rate_mah_hr(cm, 1, 1) for cm in cyc_ms])
drain_heavy = np.array([node_rate_mah_hr(cm, 4, 4) for cm in cyc_ms])

fig, ax = plt.subplots(figsize=SC)
ax.plot(cyc_min, drain_light, color="#222222", linestyle="-",
        label="Drain: sync only (1,1)")
ax.plot(cyc_min, drain_heavy, color="#222222", linestyle="--",
        label="Drain: heavy traffic (4,4)")

for ph, lbl, col, ls in sun_levels:
    ax.axhline(solar_mah_hr(ph), color=col, linestyle=ls, linewidth=1.4,
               label=f"P121 harvest, {lbl}")

ax.set_yscale("log")
ax.set_xlabel("Cycle period (minutes)")
ax.set_ylabel("Average current (mAh / hour, log)")
ax.set_title("Solar augmentation: P121 (0.3 W) harvest vs. node drain\n"
             "70% end-to-end efficiency (panel + C117 LIC buffer + cell)")
ax.legend(loc="upper right", fontsize=6.5, framealpha=0.9)
ax.set_xlim(10, 60)
plt.tight_layout(pad=0.6)
plt.savefig(os.path.join(OUT_DIR, "fig6_solar_augmentation.png"), dpi=200, bbox_inches="tight")
plt.close()
print("   saved fig6_solar_augmentation.png")


# ══════════════════════════════════════════════════════════════════════════════
# FIG 7 -- Sync Convergence
# ══════════════════════════════════════════════════════════════════════════════
print("fig7 -- sync convergence ...", flush=True)

sys.path.insert(0, os.path.dirname(__file__))
from mesh_sim import SimNetwork, NodeState

TICKS       = 200
TRIALS      = 8
NODE_COUNTS = [10, 20]
colors_conv = {10: C1, 20: C2}

fig, ax = plt.subplots(figsize=SC)

P_1HOP = 0.046
t_axis = list(range(TICKS))
theory = [100.0 * (1.0 - (1.0 - P_1HOP) ** t) for t in t_axis]

for count in NODE_COUNTS:
    col  = colors_conv[count]
    runs = []
    for trial in range(TRIALS):
        random.seed(trial * 7 + count)
        net = SimNetwork()
        net.fast_fade_enabled = False
        net.reset(count)
        series = []
        for _ in range(TICKS):
            net.step()
            series.append(net.stats()["sync_pct_alive"])
        runs.append(series)
    mean_s = [statistics.mean(r[t] for r in runs) for t in range(TICKS)]
    lo_s   = [min(r[t]           for r in runs) for t in range(TICKS)]
    hi_s   = [max(r[t]           for r in runs) for t in range(TICKS)]
    ax.plot(t_axis, mean_s, color=col, linewidth=2.0,
            label=f"Simulation, {count} nodes (mean of {TRIALS} trials)")
    ax.fill_between(t_axis, lo_s, hi_s, color=col, alpha=0.15)

# Geometric model plotted once in neutral color
ax.plot(t_axis, theory, color="black", linewidth=1.2, linestyle=":",
        alpha=0.75, label=f"Geometric model (p\u2090\u2091\u209c\u2095 = {P_1HOP})")
ax.axhline(95, color=CGRAY, linestyle="--", linewidth=1.0, label="95% threshold")

ax.set_xlabel("Simulation tick  (1 tick \u2248 10 s real time)")
ax.set_ylabel("Synchronized nodes (% of alive)")
ax.set_ylim(0, 108)
ax.set_xlim(0, TICKS)
ax.set_title("Cold-start sync convergence (shaded region = trial range)")

ax.legend(loc="upper right", fontsize=7, framealpha=0.85)

plt.tight_layout(pad=0.6)
plt.savefig(os.path.join(OUT_DIR, "fig7_sync_convergence.png"), dpi=200, bbox_inches="tight")
plt.close()
print("   saved fig7_sync_convergence.png")


# ══════════════════════════════════════════════════════════════════════════════
# FIG 8 -- Forwarding Load Distribution
# ══════════════════════════════════════════════════════════════════════════════
print("fig8 -- forwarding load distribution ...", flush=True)

N_NODES_FWD = 12
FWD_TICKS   = 600

def run_fwd(n_nodes, ticks, seed, fast_fade=False):
    random.seed(seed)
    net = SimNetwork()
    net.fast_fade_enabled = fast_fade
    net.reset(n_nodes)
    for _ in range(ticks):
        net.step()
    return [(n.id, n.forward_count)
            for n in net.nodes if n.state != NodeState.DEAD]

SEEDS         = 5
baseline_avg  = defaultdict(list)
jitter_avg    = defaultdict(list)

for seed in range(SEEDS):
    for nid, cnt in run_fwd(N_NODES_FWD, FWD_TICKS, seed):
        baseline_avg[nid].append(cnt)
    for nid, cnt in run_fwd(N_NODES_FWD, FWD_TICKS, seed, fast_fade=True):
        jitter_avg[nid].append(cnt)

b_mean = {nid: statistics.mean(v) for nid, v in baseline_avg.items()}
j_mean = {nid: statistics.mean(v) for nid, v in jitter_avg.items()}

sorted_ids = sorted(b_mean.keys(), key=lambda n: b_mean[n], reverse=True)
b_vals  = [b_mean[n] for n in sorted_ids]
j_vals  = [j_mean[n] for n in sorted_ids]
total_b = max(1, sum(b_vals)); total_j = max(1, sum(j_vals))
b_pct   = [100 * v / total_b for v in b_vals]
j_pct   = [100 * v / total_j for v in j_vals]

def gini(vals):
    s = sorted(vals); n = len(s)
    if not s or sum(s) == 0: return 0.0
    return (2 * sum((i+1)*v for i,v in enumerate(s))) / (n * sum(s)) - (n+1)/n

g_b = gini(b_pct); g_j = gini(j_pct)

xlabels = [f"N{nid}" for nid in sorted_ids]
x = np.arange(len(sorted_ids)); width = 0.38

fig, ax = plt.subplots(figsize=DC)
ax.bar(x - width/2, b_pct, width, color=C1, alpha=0.8, edgecolor="black",
       linewidth=0.4, label=f"Baseline  (Gini = {g_b:.3f})")
ax.bar(x + width/2, j_pct, width, color=C3, alpha=0.8, edgecolor="black",
       linewidth=0.4, label=f"With jitter  (Gini = {g_j:.3f})")
ax.axhline(100.0 / len(sorted_ids), color="black", linestyle="--", linewidth=1.0,
           label=f"Equal share ({100.0/len(sorted_ids):.1f}%)")
ax.set_xticks(x)
ax.set_xticklabels(xlabels, fontsize=8)
ax.set_xlabel("Node ID  (sorted by baseline load, descending)")
ax.set_ylabel("Share of total forwarded packets (%)")
ax.set_title(
    f"Per-node forwarding share: {N_NODES_FWD} nodes, {FWD_TICKS} ticks, "
    f"avg of {SEEDS} seeds"
)
ax.legend(loc="upper right", fontsize=7, framealpha=0.85)
plt.tight_layout(pad=0.6)
plt.savefig(os.path.join(OUT_DIR, "fig8_load_distribution.png"), dpi=200, bbox_inches="tight")
plt.close()
print("   saved fig8_load_distribution.png")

print(f"\nAll 8 paper figures saved to: {OUT_DIR}")
print(f"  fig8: Gini {g_b:.3f} -> {g_j:.3f}")
