#!/usr/bin/env python3
"""
sim_heuristic.py — Load-aware forwarding heuristic evaluation.

Compares the RSSI-only baseline against the load-jitter heuristic (Week 11-12
firmware) across three deterministic relay topologies.

The model separates two concepts:
  - schedule_count / recent rate: updated when a node schedules a relay,
    so the load penalty is visible to the NEXT packet that arrives.
  - win_count: updated only when a node wins the jitter race and is first
    to deliver each packet.  Used for Gini and the per-node share plot.

Heuristic parameters match platformio.ini:
  QUEUE_SCALE_MS = 25  RATE_SCALE_MS = 15  RATE_WINDOW = 50

Four plots:
  1. Per-node forwarding share  (asymmetric 2-path: who wins the race?)
  2. Forwarding Gini coefficient (all topologies × both modes)
  3. End-to-end delivery rate   vs injection rate
  4. Relay latency distribution  baseline vs load-jitter

Usage:
    python sim_heuristic.py              # saves to simulation/results/
    python sim_heuristic.py ./my_output
"""

import sys, os, math, random, statistics
from collections import deque, defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "results")
os.makedirs(OUT_DIR, exist_ok=True)

STYLE = {
    "figure.facecolor": "#0d1117",  "axes.facecolor":   "#161b22",
    "axes.edgecolor":   "#30363d",  "axes.labelcolor":  "#e6edf3",
    "xtick.color":      "#8b949e",  "ytick.color":      "#8b949e",
    "grid.color":       "#21262d",  "text.color":       "#e6edf3",
    "legend.facecolor": "#161b22",  "legend.edgecolor": "#30363d",
}
plt.rcParams.update(STYLE)
ACCENT = "#58a6ff"; GREEN = "#3fb950"; ORANGE = "#d29922"
RED = "#f85149"; PURPLE = "#bc8cff"; YELLOW = "#e3b341"

print(f"Saving heuristic plots to: {OUT_DIR}\n", flush=True)

# ── Heuristic parameters (matches firmware Week 11-12 values) ─────────────────
JITTER_MAX_MS    = 30
RSSI_THRESH_DBM  = -80
RSSI_SCALE       = 4
RSSI_PENALTY_MAX = 60
QUEUE_SCALE_MS   = 25
RATE_SCALE_MS    = 15
RATE_WINDOW      = 50
LOAD_JITTER_CAP  = 300


class RelayNode:
    def __init__(self, nid: int, rssi_from_parent: float = -70.0):
        self.id               = nid
        self.rssi_from_parent = rssi_from_parent
        self.win_count        = 0   # times this node was first to deliver (Gini input)
        self._fwd_ticks: deque = deque()   # tick of each scheduled relay (rate window)

    def recent_rate(self, tick: int) -> int:
        while self._fwd_ticks and tick - self._fwd_ticks[0] > RATE_WINDOW:
            self._fwd_ticks.popleft()
        return len(self._fwd_ticks)

    def record_schedule(self, tick: int):
        """Called when this node schedules an outgoing relay — feeds load tracking."""
        self._fwd_ticks.append(tick)

    def rssi_penalty(self) -> int:
        if self.rssi_from_parent >= RSSI_THRESH_DBM:
            return 0
        return min(int((RSSI_THRESH_DBM - self.rssi_from_parent) * RSSI_SCALE),
                   RSSI_PENALTY_MAX)

    def compute_jitter(self, pending: int, tick: int, load_jitter: bool) -> int:
        base = random.randint(0, JITTER_MAX_MS) + self.rssi_penalty()
        if not load_jitter:
            return base
        rate    = self.recent_rate(tick)
        penalty = min(pending * QUEUE_SCALE_MS + rate * RATE_SCALE_MS, LOAD_JITTER_CAP)
        return base + penalty


def run_topology(topo: dict, n_packets: int, load_jitter: bool, seed: int = 0) -> dict:
    """
    Event-driven flood relay simulation.

    topo keys:
      node_rssi       {id: rssi_dBm}  — all nodes including source & sink
      edges           {id: [(neighbor_id, link_reliability)]}
      source          source node id
      sink            sink node id
      inject_interval ticks between packet injections
    """
    random.seed(seed)
    source_id       = topo["source"]
    sink_id         = topo["sink"]
    topology        = topo["edges"]
    inject_interval = topo.get("inject_interval", 10)

    nodes = {nid: RelayNode(nid, rssi) for nid, rssi in topo["node_rssi"].items()}

    # events: (fire_at_tick, pid, from_id, to_id)
    events: list = []
    seen: dict   = {}       # pid -> set of node_ids that have received this pkt
    delivered    = 0
    packet_born: dict = {}
    latencies: list   = []

    # extra buffer for load-jitter stragglers
    total_ticks = n_packets * inject_interval + LOAD_JITTER_CAP + RATE_WINDOW + 100

    for tick in range(total_ticks):
        # ── Inject ──────────────────────────────────────────────────────────
        if tick % inject_interval == 0:
            pid = tick // inject_interval
            if pid < n_packets:
                seen[pid] = {source_id}
                packet_born[pid] = tick
                for (nbr, rel) in topology.get(source_id, []):
                    if random.random() < rel:
                        events.append((tick + 1, pid, source_id, nbr))

        # ── Process due events ───────────────────────────────────────────────
        due    = [e for e in events if e[0] <= tick]
        events = [e for e in events if e[0] > tick]

        for (_, pid, from_id, to_id) in due:
            if to_id in seen.get(pid, set()):
                continue                       # dup suppression

            seen.setdefault(pid, set()).add(to_id)

            if to_id == sink_id:
                # Credit the SENDER with a delivery win (load tracking + Gini)
                if from_id != source_id:
                    sender = nodes.get(from_id)
                    if sender is not None:
                        sender.win_count += 1
                        sender.record_schedule(tick)  # load credit only for winner
                delivered += 1
                if pid in packet_born:
                    latencies.append(tick - packet_born[pid])
                continue

            relay = nodes.get(to_id)
            if relay is None:
                continue

            pending = sum(1 for e in events if e[2] == to_id)
            jitter  = relay.compute_jitter(pending, tick, load_jitter)

            for (nbr, rel) in topology.get(to_id, []):
                if nbr not in seen.get(pid, set()):
                    if random.random() < rel:
                        events.append((tick + 1 + jitter, pid, to_id, nbr))

    relay_ids = [nid for nid in topo["node_rssi"]
                 if nid not in (source_id, sink_id)]
    return {
        "delivered":  delivered,
        "injected":   n_packets,
        "fwd_counts": {nid: nodes[nid].win_count for nid in relay_ids},
        "latencies":  latencies,
    }


def gini(values: list) -> float:
    vals = sorted(v for v in values if v >= 0)
    if not vals or sum(vals) == 0:
        return 0.0
    n = len(vals)
    return (2 * sum((i + 1) * v for i, v in enumerate(vals))) / (n * sum(vals)) - (n + 1) / n


def avg_over_seeds(tname: str, topo: dict, load_jitter: bool,
                   n_packets: int = 300, n_seeds: int = 6) -> dict:
    all_fwd: dict = defaultdict(list)
    all_del, all_lat = [], []
    for seed in range(n_seeds):
        r = run_topology(topo, n_packets, load_jitter,
                         seed=seed * 17 + abs(hash(tname)) % 89)
        for nid, cnt in r["fwd_counts"].items():
            all_fwd[nid].append(cnt)
        all_del.append(r["delivered"] / max(1, r["injected"]))
        all_lat.extend(r["latencies"])
    return {
        "fwd_counts":    {nid: statistics.mean(v) for nid, v in all_fwd.items()},
        "delivery_rate": statistics.mean(all_del),
        "latencies":     all_lat,
    }


# ── Topology definitions ───────────────────────────────────────────────────────
TOPOLOGIES = {
    # 1. Single forced path — no redistribution possible.
    #    Load jitter can only hurt (adds delay, no alternative path).
    "Linear chain\n(single path)": {
        "node_rssi":      {0: -60, 1: -72, 2: -76, 3: -80, 4: -60},
        "edges":          {0: [(1, 0.92)], 1: [(2, 0.88)],
                           2: [(3, 0.85)], 3: [(4, 0.82)]},
        "source": 0, "sink": 4, "inject_interval": 20,
    },

    # 2. Asymmetric 2-path: N1 close (RSSI=-68, base jitter 0-30 ms),
    #    N2 far (RSSI=-85, base jitter 20-50 ms due to RSSI penalty).
    #    Without load jitter N1 wins ~94% of jitter races.
    #    Larger inject_interval (20) ensures delivery credit for packet N
    #    is recorded before packet N+1 needs jitter computed.
    "Asymmetric\n2-path": {
        "node_rssi":      {0: -60, 1: -68, 2: -85, 3: -60},
        "edges":          {0: [(1, 0.95), (2, 0.90)],
                           1: [(3, 0.95)], 2: [(3, 0.90)]},
        "source": 0, "sink": 3, "inject_interval": 20,
    },

    # 3. 3-relay mesh: three parallel relays with increasing RSSI penalty.
    #    N1 almost always wins without load jitter.
    #    Load jitter cascades: N1 builds load → N2 wins → N2 builds load → N3 wins.
    "3-relay\nmesh": {
        "node_rssi":      {0: -60, 1: -65, 2: -77, 3: -90, 4: -60},
        "edges":          {0: [(1, 0.96), (2, 0.91), (3, 0.85)],
                           1: [(4, 0.96)], 2: [(4, 0.92)], 3: [(4, 0.87)]},
        "source": 0, "sink": 4, "inject_interval": 20,
    },
}

N_PACKETS = 500
N_SEEDS   = 6

print("Running relay simulations ...", flush=True)
results = {}
for tname, topo in TOPOLOGIES.items():
    b = avg_over_seeds(tname, topo, load_jitter=False, n_packets=N_PACKETS, n_seeds=N_SEEDS)
    j = avg_over_seeds(tname, topo, load_jitter=True,  n_packets=N_PACKETS, n_seeds=N_SEEDS)
    results[tname] = {"baseline": b, "jitter": j}
    bg = gini(list(b["fwd_counts"].values()))
    jg = gini(list(j["fwd_counts"].values()))
    print(f"   {tname!r:36s}  Gini  {bg:.3f} -> {jg:.3f}  "
          f"  delivery  {b['delivery_rate']:.3f} -> {j['delivery_rate']:.3f}", flush=True)


# ══════════════════════════════════════════════════════════════════════════════
# 1. PER-NODE FORWARDING SHARE — asymmetric 2-path
# ══════════════════════════════════════════════════════════════════════════════
print("\nPlot 1 / 4  — Per-node forwarding share ...", flush=True)

tname = "Asymmetric\n2-path"
topo  = TOPOLOGIES[tname]
b_fwd = results[tname]["baseline"]["fwd_counts"]
j_fwd = results[tname]["jitter"]["fwd_counts"]
relay_ids = sorted(b_fwd.keys())

def to_pct(fd, ids):
    total = max(1, sum(fd[i] for i in ids))
    return [100.0 * fd[i] / total for i in ids]

b_pct = to_pct(b_fwd, relay_ids)
j_pct = to_pct(j_fwd, relay_ids)
rssi_vals = topo["node_rssi"]
xlabels = [f"N{nid}\n({rssi_vals[nid]} dBm)" for nid in relay_ids]

x = np.arange(len(relay_ids)); width = 0.35
fig, ax = plt.subplots(figsize=(8, 5))
ax.set_title(
    f"Per-Node Forwarding Win Share — {tname.replace(chr(10),' ')}\n"
    f"(N{relay_ids[0]} close/fast, N{relay_ids[-1]} far/slow  →  both route to same sink)",
    color="#e6edf3",
)
bars_b = ax.bar(x - width/2, b_pct, width, color=ACCENT, alpha=0.85,
                edgecolor="#30363d", linewidth=0.5, label="Baseline (RSSI jitter only)")
bars_j = ax.bar(x + width/2, j_pct, width, color=GREEN,  alpha=0.85,
                edgecolor="#30363d", linewidth=0.5, label="With load jitter")
for bar, v in zip(bars_b, b_pct):
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.8,
            f"{v:.1f}%", ha="center", va="bottom", fontsize=8, color=ACCENT)
for bar, v in zip(bars_j, j_pct):
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.8,
            f"{v:.1f}%", ha="center", va="bottom", fontsize=8, color=GREEN)
ax.axhline(100.0 / len(relay_ids), color=YELLOW, linestyle="--", linewidth=1,
           label=f"Equal share ({100.0/len(relay_ids):.0f}%)")
ax.set_xticks(x); ax.set_xticklabels(xlabels, fontsize=9)
ax.set_ylabel("Share of total delivery wins (%)")
ax.set_ylim(0, 105)
ax.legend(fontsize=8)
ax.grid(True, alpha=0.3, axis="y")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "H1_forwarding_share.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved H1_forwarding_share.png")


# ══════════════════════════════════════════════════════════════════════════════
# 2. GINI COEFFICIENT — all topologies × both modes
# ══════════════════════════════════════════════════════════════════════════════
print("Plot 2 / 4  — Gini coefficient comparison ...", flush=True)

topo_names = list(TOPOLOGIES.keys())
gini_b = [gini(list(results[t]["baseline"]["fwd_counts"].values())) for t in topo_names]
gini_j = [gini(list(results[t]["jitter"]["fwd_counts"].values()))   for t in topo_names]

x = np.arange(len(topo_names)); width = 0.35
fig, ax = plt.subplots(figsize=(9, 5))
ax.set_title("Forwarding Load Inequality (Gini Coefficient)\n"
             "Lower = more balanced  |  0.0 = equal  |  1.0 = one node does all",
             color="#e6edf3")
bars_b = ax.bar(x - width/2, gini_b, width, color=ACCENT, alpha=0.85,
                edgecolor="#30363d", label="Baseline (RSSI jitter only)")
bars_j = ax.bar(x + width/2, gini_j, width, color=GREEN,  alpha=0.85,
                edgecolor="#30363d", label="With load jitter")
for bar, v in list(zip(bars_b, gini_b)) + list(zip(bars_j, gini_j)):
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.008,
            f"{v:.3f}", ha="center", va="bottom", fontsize=8, color="#e6edf3")
ax.set_xticks(x); ax.set_xticklabels(topo_names, fontsize=8)
ax.set_ylabel("Gini coefficient")
ax.set_ylim(0, 0.75)
ax.legend(fontsize=9)
ax.grid(True, alpha=0.3, axis="y")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "H2_gini_coefficient.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved H2_gini_coefficient.png")


# ══════════════════════════════════════════════════════════════════════════════
# 3. DELIVERY RATE vs INJECTION RATE — asymmetric 2-path
# ══════════════════════════════════════════════════════════════════════════════
print("Plot 3 / 4  — Delivery rate vs injection rate ...", flush=True)

base_topo  = TOPOLOGIES["Asymmetric\n2-path"]
intervals  = [8, 12, 16, 20, 30, 50, 80]
del_b, del_j = [], []

for iv in intervals:
    t  = {**base_topo, "inject_interval": iv}
    rb = avg_over_seeds("Asymmetric\n2-path", t, load_jitter=False, n_packets=300, n_seeds=N_SEEDS)
    rj = avg_over_seeds("Asymmetric\n2-path", t, load_jitter=True,  n_packets=300, n_seeds=N_SEEDS)
    del_b.append(rb["delivery_rate"] * 100)
    del_j.append(rj["delivery_rate"] * 100)

fig, ax = plt.subplots(figsize=(9, 5))
ax.set_title("End-to-End Delivery Rate vs Traffic Intensity\n"
             "(Asymmetric 2-path, averaged over 6 seeds)",
             color="#e6edf3")
ax.plot(intervals, del_b, color=ACCENT, linewidth=2, marker="o", markersize=6,
        label="Baseline (RSSI jitter only)")
ax.plot(intervals, del_j, color=GREEN,  linewidth=2, marker="s", markersize=6,
        label="With load jitter")
ax.set_xlabel("Ticks between injections  (left = high traffic, right = low)")
ax.set_ylabel("Delivered / injected (%)")
ax.set_ylim(0, 105)
ax.set_xticks(intervals)
ax.set_xticklabels([f"1/{iv}" for iv in intervals], fontsize=8)
ax.legend(fontsize=9)
ax.grid(True, alpha=0.4)
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "H3_delivery_vs_load.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved H3_delivery_vs_load.png")


# ══════════════════════════════════════════════════════════════════════════════
# 4. RELAY LATENCY DISTRIBUTION — 3-relay mesh
# ══════════════════════════════════════════════════════════════════════════════
print("Plot 4 / 4  — Relay latency distribution ...", flush=True)

t3 = TOPOLOGIES["3-relay\nmesh"]
rb = avg_over_seeds("3-relay\nmesh", t3, load_jitter=False, n_packets=600, n_seeds=N_SEEDS)
rj = avg_over_seeds("3-relay\nmesh", t3, load_jitter=True,  n_packets=600, n_seeds=N_SEEDS)
lats_b = rb["latencies"] or [0]
lats_j = rj["latencies"] or [0]

fig, axes = plt.subplots(1, 2, figsize=(11, 5), sharey=True)
fig.suptitle("Delivery Latency Distribution — 3-relay mesh\n"
             "(3 parallel relays of decreasing RSSI quality to master)",
             color="#e6edf3")

for ax, lats, color, label in [
    (axes[0], lats_b, ACCENT, "Baseline (RSSI jitter only)"),
    (axes[1], lats_j, GREEN,  "With load jitter"),
]:
    med = statistics.median(lats)
    p90 = sorted(lats)[int(0.9 * len(lats))]
    ax.hist(lats, bins=40, color=color, alpha=0.8, edgecolor="#30363d", linewidth=0.3)
    ax.axvline(med, color=YELLOW, linestyle="--", linewidth=1.2,
               label=f"Median: {med:.0f} ticks")
    ax.axvline(p90, color=RED,    linestyle=":",  linewidth=1.2,
               label=f"90th pct: {p90:.0f} ticks")
    ax.set_title(label, color="#e6edf3", fontsize=9)
    ax.set_xlabel("Delivery latency (ticks from injection)")
    ax.set_ylabel("Count")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.4)

plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "H4_latency_distribution.png"), dpi=150, bbox_inches="tight")
plt.close()
print("   saved H4_latency_distribution.png")

print(f"\nAll heuristic plots saved to: {OUT_DIR}")
print("  H1_forwarding_share.png      — who wins relay races (asymmetric 2-path)")
print("  H2_gini_coefficient.png      — load inequality across topologies and modes")
print("  H3_delivery_vs_load.png      — delivery rate under congestion")
print("  H4_latency_distribution.png  — latency cost of load-jitter balancing")
