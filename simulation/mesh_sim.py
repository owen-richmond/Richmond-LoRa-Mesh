#!/usr/bin/env python3
"""
LoRa Mesh Network Simulator
============================
Interactive, animated visualization of mesh sync, RSSI-weighted forwarding,
orphan detection, and network congestion (collision model + duty cycle).

Usage:  python mesh_sim.py
Deps:   tkinter (stdlib)
"""

import sys, math, random
import tkinter as tk
import tkinter.ttk as ttk
from enum import Enum, auto
from typing import Optional, List, Dict


# ─── Colour palette ───────────────────────────────────────────────────────────
BG           = "#0d1117"
MASTER_CLR   = "#FFD700"
SYNCED_CLR   = "#00E5FF"
RELAY_CLR    = "#00FF88"
ORPHAN_CLR   = "#FF6B35"
COLLISION_CLR= "#FF2244"
DEAD_CLR     = "#2d3139"
EDGE_GOOD    = "#00E5FF"
EDGE_WEAK    = "#FF4444"
PANEL_BG     = "#161b22"
ACCENT       = "#58a6ff"
GRID_CLR     = "#1c2128"
WALL_CLR     = "#8b1a1a"

# Depth colors for hierarchy view (hop 0=master, 1, 2, 3, 4, 5+)
DEPTH_COLORS = ["#FFD700", "#00E5FF", "#00C8FF", "#7B68EE", "#FF69B4", "#FF4500"]

# ─── Physical layer constants (868 MHz LoRa SF12, 125 kHz BW) ────────────────
# These are grounded in real hardware specs and standard RF propagation theory.
LORA_TX_POWER_DBM  = 22.0    # RAK4630 max Tx power (dBm)
NOISE_FLOOR_DBM    = -117.0  # kTB(-174) + 10log10(125e3)(+51) + noise figure(+6) dBm
SNR_THRESHOLD_DB   = -20.0   # minimum decodable SNR for SF12 (Semtech AN1200.22)
PATH_LOSS_EXP      = 2.7     # path loss exponent (2.0=free space, 3.5=dense urban)
PL_1M_DB           = 91.2    # free-space path loss at 1 m, 868 MHz: 32.44+20log10(868)
SIM_METERS_PER_PX  = 500.0 / 230.0  # maps DEF_RANGE pixels to ~500 m real-world range
FAST_FADE_STD_DB   = 2.5     # std dev of per-link instantaneous fading (dB)

def _snr(rssi: float) -> float:
    """Received SNR given RSSI and the thermal noise floor."""
    return rssi - NOISE_FLOOR_DBM

def _per(snr: float) -> float:
    """Sigmoid packet error rate. PER=50% at SNR_THRESHOLD, steep transition either side."""
    margin = snr - SNR_THRESHOLD_DB   # positive = above threshold (good)
    return 1.0 / (1.0 + math.exp(margin * 1.5))


# ─── Default simulation parameters ───────────────────────────────────────────
DEF_NODES         = 40
DEF_RANGE         = 230
DEF_SYNC_INTERVAL = 80
DEF_ORPHAN_THRESH = 200
DEF_JITTER_SCALE  = 4
DEF_RSSI_THRESH   = -80
DEF_JITTER_MAX    = 30
DEF_RTR_INTERVAL  = 20
DEF_SHADOW_STD    = 6
DEF_PAYLOAD_BYTES = 20
DEF_DUTY_INTERVAL = 0        # 0 = disabled; >0 = min ticks between tx per node
FIELD_SIZE        = 1000

# Animation caps (keeps frame rate healthy under high load)
MAX_RINGS   = 60
MAX_PACKETS = 80


# ─── Node state ───────────────────────────────────────────────────────────────
class NodeState(Enum):
    MASTER = auto()
    SYNCED = auto()
    ORPHAN = auto()
    DEAD   = auto()


# ─── Simulation model ─────────────────────────────────────────────────────────
class SimNode:
    def __init__(self, nid: int, x: float, y: float, state=NodeState.SYNCED):
        self.id              = nid
        self.x               = x
        self.y               = y
        self.state           = state
        self.parent_id: Optional[int] = None
        self.last_sync_tick  = 0
        self.hop_count       = 0
        self.rssi_to_parent  = 0.0
        self.shadow_offset   = random.gauss(0, DEF_SHADOW_STD)
        self.pending_sync_at = -1
        self.pending_from    = -1
        self.orphan_since    = -1
        self.rtr_next_at     = -1
        self.last_tx_tick    = -9999   # for duty-cycle limiting
        self.forward_count   = 0       # total successful relays by this node

    def rssi_to(self, other: 'SimNode', lora_range: float) -> float:
        d = math.hypot(self.x - other.x, self.y - other.y)
        if d == 0:
            return LORA_TX_POWER_DBM
        if d > lora_range:          # hard UI cutoff so the range slider still works
            return -999.0
        d_m = max(1.0, d * SIM_METERS_PER_PX)
        path_loss = PL_1M_DB + 10.0 * PATH_LOSS_EXP * math.log10(d_m)
        rssi = LORA_TX_POWER_DBM - path_loss
        rssi += self.shadow_offset * 0.5 + other.shadow_offset * 0.5
        # Below receiver sensitivity: treat as unreachable
        sensitivity = NOISE_FLOOR_DBM + SNR_THRESHOLD_DB
        return rssi if rssi > sensitivity - 5.0 else -999.0

    def dist_to(self, other: 'SimNode') -> float:
        return math.hypot(self.x - other.x, self.y - other.y)


class SimNetwork:
    def __init__(self):
        self.nodes: List[SimNode]   = []
        self.tick                   = 0
        self.lora_range             = DEF_RANGE
        self.sync_interval          = DEF_SYNC_INTERVAL
        self.orphan_threshold       = DEF_ORPHAN_THRESH
        self.jitter_scale           = DEF_JITTER_SCALE
        self.rssi_threshold         = DEF_RSSI_THRESH
        self.jitter_max             = DEF_JITTER_MAX
        self.rtr_interval           = DEF_RTR_INTERVAL
        self.payload_bytes          = DEF_PAYLOAD_BYTES
        self.duty_min_interval      = DEF_DUTY_INTERVAL
        self.bits_sent              = 0
        self.bits_received          = 0
        self.avg_throughput_kbps    = 0.0
        self.events: List[Dict]     = []
        self.rebuild_start_tick     = -1
        self.last_rebuild_ticks     = 0
        self.packets_sent           = 0
        # Congestion tracking
        self.total_tx_attempts      = 0
        self.total_collisions       = 0
        self.collisions_this_tick   = 0
        self.channel_busy_ticks     = 0
        self.total_phy_errors       = 0   # packets lost to noise/fading (not collisions)
        # Rolling window for live graphs
        self._collision_window: List[int] = []  # collisions per tick (last 50)
        self._load_window: List[float]    = []  # tx count per tick (last 50)
        # Walls: list of (x1,y1,x2,y2) in sim-space coords
        self.walls: List[tuple] = []
        # Set False in batch/headless runs to skip per-call fast fading (big speedup)
        self.fast_fade_enabled: bool = True

    def reset(self, n_nodes: int):
        self.nodes  = []
        self.tick   = 0
        self.events = []
        self.packets_sent = self.bits_sent = self.bits_received = 0
        self.avg_throughput_kbps = 0.0
        self.total_tx_attempts   = 0
        self.total_collisions    = 0
        self.collisions_this_tick = 0
        self.channel_busy_ticks  = 0
        self.total_phy_errors    = 0
        for n in self.nodes:
            n.forward_count = 0
        self.rebuild_start_tick  = -1
        self.total_tx_attempts   = 0
        self.total_collisions    = 0
        self.collisions_this_tick = 0
        self.channel_busy_ticks  = 0
        self._collision_window   = []
        self._load_window        = []
        master = SimNode(0, FIELD_SIZE / 2, FIELD_SIZE / 2, NodeState.MASTER)
        master.last_sync_tick = 0
        self.nodes.append(master)
        for i in range(1, n_nodes):
            n = SimNode(i, random.uniform(50, FIELD_SIZE - 50),
                           random.uniform(50, FIELD_SIZE - 50), NodeState.ORPHAN)
            n.orphan_since = 0
            self.nodes.append(n)
        self._trigger_master_broadcast()

    def add_nodes(self, count: int):
        start = len(self.nodes)
        for i in range(count):
            n = SimNode(start + i, random.uniform(50, FIELD_SIZE - 50),
                                   random.uniform(50, FIELD_SIZE - 50), NodeState.ORPHAN)
            n.orphan_since = self.tick
            self.nodes.append(n)

    def step(self):
        self.tick += 1
        self.events = []
        self._update_shadowing()
        self._process_pending_relays()
        self._check_orphans()
        self._process_rtr()
        if self.tick % self.sync_interval == 0:
            self._trigger_master_broadcast()
        self._estimate_throughput()

    # ── sync propagation ──────────────────────────────────────────────────────
    def _trigger_master_broadcast(self):
        master = self._master()
        if master is None:
            return
        master.last_sync_tick = self.tick
        self.events.append({'type': 'ring', 'x': master.x, 'y': master.y,
                            'color': MASTER_CLR, 'radius': self.lora_range})
        self.packets_sent += 1
        self._record_throughput(True)
        for n in self.nodes:
            if n.id == master.id or n.state == NodeState.DEAD:
                continue
            rssi = self.rssi_between(master, n)
            if rssi > -999:
                j = self._compute_jitter(rssi)
                if n.pending_sync_at < 0 or self.tick + j < n.pending_sync_at:
                    n.pending_sync_at = self.tick + j
                    n.pending_from    = master.id

    def _process_pending_relays(self):
        # Collect every node that wants to transmit this tick
        txers = [n for n in self.nodes
                 if n.state != NodeState.DEAD and 0 <= n.pending_sync_at <= self.tick]

        self.collisions_this_tick = 0
        tx_count = len(txers)
        self.total_tx_attempts += tx_count
        if tx_count > 0:
            self.channel_busy_ticks += 1

        # Collision detection: for each transmitter, count simultaneous
        # interferers within range.  More concurrent tx → higher drop prob.
        dropped = set()
        for n in txers:
            if self.duty_min_interval > 0:
                if self.tick - n.last_tx_tick < self.duty_min_interval:
                    n.pending_sync_at = -1
                    dropped.add(n.id)
                    continue
            interferers = sum(1 for m in txers
                              if m.id != n.id and n.dist_to(m) < self.lora_range)
            if interferers:
                # Capture effect: each extra interferer raises drop probability.
                # 1 interferer → 45%, 2 → 70%, 3+ → 88%
                p_drop = min(0.95, 1.0 - 0.55 ** interferers)
                if random.random() < p_drop:
                    self.collisions_this_tick += 1
                    self.total_collisions += 1
                    n.pending_sync_at = -1
                    dropped.add(n.id)
                    self.events.append({'type': 'collision', 'x': n.x, 'y': n.y})

        # Update rolling windows
        self._collision_window.append(self.collisions_this_tick)
        self._load_window.append(tx_count)
        if len(self._collision_window) > 50:
            self._collision_window = self._collision_window[-50:]
            self._load_window      = self._load_window[-50:]

        # Process surviving transmissions
        for n in txers:
            if n.id in dropped:
                continue
            sender = self._node(n.pending_from)
            if sender is None or sender.state == NodeState.DEAD:
                n.pending_sync_at = -1
                continue
            # Physical layer: SNR-based packet error rate (noise + fast fading)
            rssi_rx = self.rssi_between(sender, n)
            if random.random() < _per(_snr(rssi_rx)):
                self.total_phy_errors += 1
                n.pending_sync_at = -1
                self.events.append({'type': 'collision', 'x': n.x, 'y': n.y})
                continue
            n.last_tx_tick  = self.tick
            n.forward_count += 1
            rssi = (sender.rssi_to(n, self.lora_range)
                    if sender.id != 0 else n.rssi_to(sender, self.lora_range))
            if n.state == NodeState.ORPHAN or n.parent_id is None or rssi > n.rssi_to_parent:
                n.parent_id      = sender.id
                n.rssi_to_parent = rssi
                n.hop_count      = (sender.hop_count + 1) if sender.state != NodeState.MASTER else 1
            was_orphan        = n.state == NodeState.ORPHAN
            n.state           = NodeState.SYNCED
            n.last_sync_tick  = self.tick
            n.orphan_since    = -1
            n.rtr_next_at     = -1
            n.pending_sync_at = -1
            if was_orphan:
                self.events.append({'type': 'packet', 'from': n.pending_from,
                                    'to': n.id, 'color': SYNCED_CLR})
                if self.rebuild_start_tick >= 0 and self._orphan_count() == 0:
                    self.last_rebuild_ticks = self.tick - self.rebuild_start_tick
                    self.rebuild_start_tick = -1
            else:
                self.events.append({'type': 'packet', 'from': n.pending_from,
                                    'to': n.id, 'color': RELAY_CLR})
            self._record_throughput(True)
            self.events.append({'type': 'ring', 'x': n.x, 'y': n.y,
                                'color': SYNCED_CLR, 'radius': self.lora_range * 0.6})
            self.packets_sent += 1
            for nb in self.nodes:
                if nb.id == n.id or nb.state == NodeState.DEAD:
                    continue
                r = self.rssi_between(n, nb)
                if r > -999:
                    j = self._compute_jitter(r)
                    if nb.pending_sync_at < 0 or self.tick + j < nb.pending_sync_at:
                        nb.pending_sync_at = self.tick + j
                        nb.pending_from    = n.id

    def _check_orphans(self):
        for n in self.nodes:
            if n.state in (NodeState.MASTER, NodeState.DEAD, NodeState.ORPHAN):
                continue
            if self.tick - n.last_sync_tick > self.orphan_threshold:
                n.state        = NodeState.ORPHAN
                n.orphan_since = self.tick
                n.rtr_next_at  = self.tick + random.randint(2, 10)
                n.parent_id    = None
                if self.rebuild_start_tick < 0:
                    self.rebuild_start_tick = self.tick
                self.events.append({'type': 'ring', 'x': n.x, 'y': n.y,
                                    'color': ORPHAN_CLR, 'radius': 30})

    def _process_rtr(self):
        for n in self.nodes:
            if n.state != NodeState.ORPHAN:
                continue
            if n.rtr_next_at < 0 or self.tick < n.rtr_next_at:
                continue
            n.rtr_next_at = self.tick + self.rtr_interval
            self.events.append({'type': 'ring', 'x': n.x, 'y': n.y,
                                'color': ORPHAN_CLR, 'radius': self.lora_range * 0.4})
            self.packets_sent += 1
            best_rssi, best_relay = -999, None
            for nb in self.nodes:
                if nb.state not in (NodeState.MASTER, NodeState.SYNCED):
                    continue
                r = nb.rssi_to(n, self.lora_range)
                if r > best_rssi:
                    best_rssi, best_relay = r, nb
            if best_relay is not None:
                j = self._compute_jitter(best_rssi)
                if n.pending_sync_at < 0 or self.tick + j < n.pending_sync_at:
                    n.pending_sync_at = self.tick + j
                    n.pending_from    = best_relay.id

    def _update_shadowing(self):
        for n in self.nodes:
            n.shadow_offset += random.gauss(0, 0.65)
            n.shadow_offset  = max(-20, min(20, n.shadow_offset))

    def _record_throughput(self, successful: bool):
        self.bits_sent += self.payload_bytes * 8
        if successful:
            self.bits_received += self.payload_bytes * 8

    def _estimate_throughput(self):
        self.avg_throughput_kbps = self.bits_received / max(1, self.tick * 10)

    def _link_quality(self, rssi: float) -> float:
        lo = NOISE_FLOOR_DBM + SNR_THRESHOLD_DB   # sensitivity floor (~-137 dBm)
        hi = -80.0                                  # comfortably good signal
        return max(0.0, min(1.0, (rssi - lo) / (hi - lo))) if rssi > lo else 0.0

    def _wall_blocks(self, n, m) -> bool:
        """True if any wall segment crosses the line between nodes n and m."""
        for wx1, wy1, wx2, wy2 in self.walls:
            if _segments_intersect(n.x, n.y, m.x, m.y, wx1, wy1, wx2, wy2):
                return True
        return False

    def rssi_between(self, a, b) -> float:
        """RSSI from a to b: log-distance path loss + slow shadow + wall loss + fast fading."""
        rssi = a.rssi_to(b, self.lora_range)
        if rssi <= -999.0:
            return -999.0
        for wx1, wy1, wx2, wy2 in self.walls:
            if _segments_intersect(a.x, a.y, b.x, b.y, wx1, wy1, wx2, wy2):
                rssi -= 25.0
        # Per-call instantaneous fast fading on top of slow shadowing
        if self.fast_fade_enabled:
            rssi += random.gauss(0, FAST_FADE_STD_DB)
        return rssi

    def bfs_depths(self) -> dict:
        """BFS hop count from master through parent links. Returns {node_id: depth}."""
        from collections import deque
        master = self._master()
        if master is None:
            return {}
        depths  = {master.id: 0}
        visited = {master.id}
        queue   = deque([master])
        while queue:
            cur = queue.popleft()
            for n in self.nodes:
                if n.id in visited or n.state == NodeState.DEAD:
                    continue
                if n.parent_id == cur.id and n.state == NodeState.SYNCED:
                    depths[n.id] = depths[cur.id] + 1
                    visited.add(n.id)
                    queue.append(n)
        return depths

    def _chain_throughput(self) -> float:
        master = self._master()
        if master is None:
            return 0.0
        rates = []
        for n in self.nodes:
            if n.state != NodeState.SYNCED or n.parent_id is None:
                continue
            chain, cur = [], n
            while cur and cur.id != master.id and cur.parent_id is not None:
                p = self._node(cur.parent_id)
                if p is None:
                    break
                chain.append(self._link_quality(p.rssi_to(cur, self.lora_range)))
                cur = p
            if cur and cur.id == master.id and chain:
                rates.append(min(chain))
        return 50.0 * sum(rates) / len(rates) if rates else 0.0

    def _compute_jitter(self, rssi: float) -> int:
        base = random.randint(0, self.jitter_max)
        if rssi < self.rssi_threshold:
            return base + min(int((self.rssi_threshold - rssi) * self.jitter_scale), 60)
        return base

    def _master(self) -> Optional[SimNode]:
        return next((n for n in self.nodes if n.state == NodeState.MASTER), None)

    def _node(self, nid: int) -> Optional[SimNode]:
        return next((n for n in self.nodes if n.id == nid), None)

    def _orphan_count(self) -> int:
        return sum(1 for n in self.nodes if n.state == NodeState.ORPHAN)

    def kill_random(self, count: int = 1):
        cands = [n for n in self.nodes if n.state not in (NodeState.MASTER, NodeState.DEAD)]
        random.shuffle(cands)
        for n in cands[:count]:
            self._kill(n)

    def _kill(self, n: SimNode):
        if n.state == NodeState.MASTER:
            return
        n.state = NodeState.DEAD
        n.pending_sync_at = -1
        for nb in self.nodes:
            if nb.parent_id == n.id and nb.state == NodeState.SYNCED:
                nb.state        = NodeState.ORPHAN
                nb.orphan_since = self.tick
                nb.rtr_next_at  = self.tick + random.randint(2, 10)
                nb.parent_id    = None
                if self.rebuild_start_tick < 0:
                    self.rebuild_start_tick = self.tick

    def revive_all(self):
        for n in self.nodes:
            if n.state == NodeState.DEAD:
                n.state        = NodeState.ORPHAN
                n.orphan_since = self.tick
                n.rtr_next_at  = self.tick + random.randint(2, 10)

    def stats(self) -> Dict:
        total   = len(self.nodes)
        synced  = sum(1 for n in self.nodes if n.state in (NodeState.MASTER, NodeState.SYNCED))
        orphans = self._orphan_count()
        dead    = sum(1 for n in self.nodes if n.state == NodeState.DEAD)
        alive   = max(1, total - dead)   # nodes that can participate in the network
        hops    = [n.hop_count for n in self.nodes
                   if n.state == NodeState.SYNCED and n.hop_count > 0]
        coll_pct = (100 * self.total_collisions / max(1, self.total_tx_attempts))
        load_pct = (100 * self.channel_busy_ticks / max(1, self.tick))
        # Rolling collision rate (last 50 ticks, per-attempt)
        recent_coll = sum(self._collision_window[-50:])
        recent_att  = max(1, sum(self._load_window[-50:]))
        rolling_coll_pct = 100 * recent_coll / recent_att
        rolling_load_pct = (sum(self._load_window[-50:]) /
                            max(1, len(self._load_window[-50:]) * alive)) * 100
        # Sync % relative to alive nodes (so killing dead nodes doesn't distort recovery graph)
        sync_pct_alive = 100 * synced / alive
        # Per-node forwarding distribution: what share of all relays did the busiest node do?
        fwd_counts = [n.forward_count for n in self.nodes if n.state != NodeState.DEAD]
        total_fwd  = max(1, sum(fwd_counts))
        max_fwd    = max(fwd_counts) if fwd_counts else 0
        max_fwd_share = 100.0 * max_fwd / total_fwd
        total_losses  = self.total_collisions + self.total_phy_errors
        phy_error_pct = 100.0 * self.total_phy_errors / max(1, self.total_tx_attempts)
        return dict(
            total=total, synced=synced, orphans=orphans, dead=dead, alive=alive,
            sync_pct_alive=sync_pct_alive,
            avg_hops=sum(hops) / len(hops) if hops else 0.0,
            ticks=self.tick,
            rebuild_ticks=self.last_rebuild_ticks,
            packets=self.packets_sent,
            throughput_kbps=self.avg_throughput_kbps,
            chain_throughput_kbps=self._chain_throughput(),
            collision_pct=coll_pct,
            load_pct=load_pct,
            rolling_coll_pct=rolling_coll_pct,
            rolling_load_pct=min(100.0, rolling_load_pct),
            max_fwd_share=max_fwd_share,
            phy_error_pct=phy_error_pct,
            total_losses=total_losses,
        )


# ─── Headless fallback ────────────────────────────────────────────────────────
def _run_headless_simulation() -> None:
    net = SimNetwork()
    net.reset(DEF_NODES)
    for _ in range(400):
        net.step()
    st = net.stats()
    print(f"Ticks: {st['ticks']}  Synced: {st['synced']}/{st['total']}")
    print(f"Collisions: {st['collision_pct']:.1f}%  Load: {st['load_pct']:.1f}%")
    print(f"Throughput: {st['throughput_kbps']:.2f} kbps")


# ─── Colour helpers ───────────────────────────────────────────────────────────
def _lighten(h: str, f: float = 1.4) -> str:
    # Expand 3-digit shorthand (#rgb → #rrggbb)
    if len(h) == 4:
        h = '#' + h[1]*2 + h[2]*2 + h[3]*2
    return "#{:02x}{:02x}{:02x}".format(
        min(255, int(int(h[1:3], 16) * f)),
        min(255, int(int(h[3:5], 16) * f)),
        min(255, int(int(h[5:7], 16) * f)))


def _dim(h: str, f: float = 0.35) -> str:
    return _lighten(h, f)


def _lerp(c1: str, c2: str, t: float) -> str:
    r1,g1,b1 = int(c1[1:3],16), int(c1[3:5],16), int(c1[5:7],16)
    r2,g2,b2 = int(c2[1:3],16), int(c2[3:5],16), int(c2[5:7],16)
    return "#{:02x}{:02x}{:02x}".format(
        int(r1+t*(r2-r1)), int(g1+t*(g2-g1)), int(b1+t*(b2-b1)))


def _segments_intersect(ax, ay, bx, by, cx, cy, dx, dy) -> bool:
    """Return True if line segment AB intersects segment CD."""
    def _cross(ox, oy, px, py, qx, qy):
        return (px - ox) * (qy - oy) - (py - oy) * (qx - ox)
    d1 = _cross(cx, cy, dx, dy, ax, ay)
    d2 = _cross(cx, cy, dx, dy, bx, by)
    d3 = _cross(ax, ay, bx, by, cx, cy)
    d4 = _cross(ax, ay, bx, by, dx, dy)
    return (((d1 > 0 and d2 < 0) or (d1 < 0 and d2 > 0)) and
            ((d3 > 0 and d4 < 0) or (d3 < 0 and d4 > 0)))


# ─── Animation data objects ───────────────────────────────────────────────────
class SignalRing:
    def __init__(self, x, y, color: str, max_r: float):
        self.x, self.y = x, y
        self.color = color
        self.r     = 5.0
        self.max_r = max_r
        self.alpha = 1.0

    def step(self, speed: float) -> bool:
        self.r    += speed
        self.alpha = max(0.0, 1.0 - self.r / self.max_r)
        return self.r < self.max_r


class PacketDot:
    def __init__(self, x0, y0, x1, y1, color: str):
        self.x0, self.y0 = x0, y0
        self.x1, self.y1 = x1, y1
        self.color    = color
        self.progress = 0.0

    def step(self) -> bool:
        self.progress = min(1.0, self.progress + 0.07)
        return self.progress < 1.0


# ─── Network canvas ───────────────────────────────────────────────────────────
class NetworkCanvas:
    def __init__(self, master_widget, network: SimNetwork):
        self.net   = network
        self.rings: List[SignalRing]  = []
        self.packets: List[PacketDot] = []
        self.show_ranges  = True
        self.show_parents = True
        self.show_packets = True
        self.show_rings   = True
        self.show_labels  = False
        self.show_rssi    = False
        self.selected_id  = -1
        self.hover_id     = -1
        self._pulse_phase = 0.0
        self.on_node_click = None
        # Wall drawing mode
        self.wall_mode     = False
        self._wall_start   = None   # (sim_x, sim_y) of drag start

        # Tiered rendering state
        self._frame      = 0
        self._topo_dirty = True
        self._grid_w     = 0
        self._grid_h     = 0
        # Edge pair cache — computed once when topology changes (positions are static).
        # Avoids O(N²) distance checks every redraw.
        self._edge_pairs: list = []   # [(node_a, node_b), ...]
        # Cached scale/offset — updated once per redraw to avoid thousands of
        # winfo_width()/winfo_height() OS calls per frame.
        self._s  = 0.5
        self._ox = 0.0
        self._oy = 0.0

        self.canvas = tk.Canvas(master_widget, bg=BG, highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.canvas.bind("<Button-1>",        self._on_click)
        self.canvas.bind("<Motion>",          self._on_motion)
        self.canvas.bind("<ButtonRelease-1>", self._on_release)
        self.canvas.bind("<Configure>",       lambda *_: self._mark_grid_dirty())

    def _mark_grid_dirty(self):
        self._grid_w = 0

    def mark_topo_dirty(self):
        self._topo_dirty = True
        self._rebuild_edge_cache()

    def _rebuild_edge_cache(self):
        """Recompute in-range node pairs. Positions are static so this only
        needs to run when nodes are added/killed or walls change."""
        pairs = []
        nodes = self.net.nodes
        r_sq  = self.net.lora_range ** 2
        for i, n in enumerate(nodes):
            if n.state == NodeState.DEAD:
                continue
            for m in nodes[i + 1:]:
                if m.state == NodeState.DEAD:
                    continue
                dx, dy = n.x - m.x, n.y - m.y
                if dx * dx + dy * dy < r_sq:
                    blocked = self.net._wall_blocks(n, m)
                    pairs.append((n, m, blocked))
        # Sort shortest-first so the cap keeps the most local/interesting edges
        pairs.sort(key=lambda p: (p[0].x - p[1].x)**2 + (p[0].y - p[1].y)**2)
        self._edge_pairs = pairs

    # ── coordinate helpers (use cached _s / _ox / _oy set in redraw) ─────────
    def _update_scale_cache(self):
        w = self.canvas.winfo_width()
        h = self.canvas.winfo_height()
        if w > 10 and h > 10:
            self._s  = min(w, h) / FIELD_SIZE
            self._ox = (w - FIELD_SIZE * self._s) / 2
            self._oy = (h - FIELD_SIZE * self._s) / 2

    def _sx(self, x): return x * self._s + self._ox
    def _sy(self, y): return y * self._s + self._oy

    def _to_sim(self, px, py):
        return (px - self._ox) / self._s, (py - self._oy) / self._s

    def _node_at(self, px, py) -> int:
        # Recompute scale for click events (outside redraw cycle)
        self._update_scale_cache()
        sx, sy = self._to_sim(px, py)
        for n in self.net.nodes:
            if math.hypot(n.x - sx, n.y - sy) < 13 / self._s:
                return n.id
        return -1

    # ── animation ─────────────────────────────────────────────────────────────
    def ingest_events(self):
        for ev in self.net.events:
            if ev['type'] == 'ring' and self.show_rings and len(self.rings) < MAX_RINGS:
                self.rings.append(SignalRing(ev['x'], ev['y'], ev['color'],
                                             ev.get('radius', 200)))
            elif ev['type'] == 'collision' and self.show_rings and len(self.rings) < MAX_RINGS:
                self.rings.append(SignalRing(ev['x'], ev['y'], COLLISION_CLR, 40))
            elif ev['type'] == 'packet' and self.show_packets and len(self.packets) < MAX_PACKETS:
                src = self.net._node(ev['from'])
                dst = self.net._node(ev['to'])
                if src and dst:
                    self.packets.append(PacketDot(src.x, src.y, dst.x, dst.y, ev['color']))

    def advance_animations(self):
        self._update_scale_cache()
        speed = max(2.0, self._s * FIELD_SIZE / 200)
        self.rings   = [r for r in self.rings   if r.step(speed)]
        self.packets = [p for p in self.packets if p.step()]
        self._pulse_phase = (self._pulse_phase + 0.08) % (2 * math.pi)

    # ── tiered redraw ─────────────────────────────────────────────────────────
    def redraw(self):
        c = self.canvas
        # Update cached scale/offset ONCE — all _sx/_sy calls below use these.
        self._update_scale_cache()
        s = self._s
        self._frame += 1
        many_nodes = len(self.net.nodes) > 80

        # Grid: only on resize
        w = c.winfo_width()
        h = c.winfo_height()
        if w != self._grid_w or h != self._grid_h:
            c.delete("grid")
            self._draw_grid(s)
            self._grid_w, self._grid_h = w, h
            self._topo_dirty = True

        # Static topology: every 10 frames or when dirty
        if self._topo_dirty or self._frame % 20 == 0:
            c.delete("static")
            if self.show_ranges and not many_nodes:
                self._draw_ranges(s)
            self._draw_edges()
            self._draw_walls()
            if self.show_parents:
                self._draw_parent_arrows(s)
            self._topo_dirty = False

        # Dynamic: every frame
        c.delete("dynamic")
        if self.show_rings:
            self._draw_rings(s)
        if self.show_packets:
            self._draw_packets()
        self._draw_nodes(s, many_nodes)

    # ── layer draw methods ────────────────────────────────────────────────────
    def _draw_grid(self, s):
        c = self.canvas
        ox, oy = self._ox, self._oy
        fs = FIELD_SIZE * s
        for i in range(0, FIELD_SIZE + 1, 100):
            v = i * s
            c.create_line(ox+v, oy, ox+v, oy+fs, fill=GRID_CLR, width=1, tags=("grid",))
            c.create_line(ox, oy+v, ox+fs, oy+v, fill=GRID_CLR, width=1, tags=("grid",))

    def _draw_ranges(self, s):
        c = self.canvas
        for n in self.net.nodes:
            if n.state == NodeState.DEAD:
                continue
            cx, cy = self._sx(n.x), self._sy(n.y)
            r = self.net.lora_range * s
            c.create_oval(cx-r, cy-r, cx+r, cy+r,
                          outline=_dim(self._node_color(n), 0.4),
                          width=1, dash=(4, 6), fill="", tags=("static",))

    _MAX_EDGES = 150

    def _draw_walls(self):
        c = self.canvas
        for x1, y1, x2, y2 in self.net.walls:
            c.create_line(self._sx(x1), self._sy(y1),
                          self._sx(x2), self._sy(y2),
                          fill=WALL_CLR, width=5, capstyle=tk.ROUND,
                          tags=("static",))

    def _draw_edges(self):
        c   = self.canvas
        lim = self.net.lora_range
        for n, m, blocked in self._edge_pairs[:self._MAX_EDGES]:
            if blocked:
                c.create_line(self._sx(n.x), self._sy(n.y),
                              self._sx(m.x), self._sy(m.y),
                              fill="#3a3f47", dash=(3, 6), width=1,
                              tags=("static",))
                continue
            rssi = n.rssi_to(m, lim)
            t    = max(0.0, min(1.0, (rssi + 120) / 80))
            clr  = _dim(_lerp(EDGE_WEAK, EDGE_GOOD, t), 0.3 + 0.5 * t)
            c.create_line(self._sx(n.x), self._sy(n.y),
                          self._sx(m.x), self._sy(m.y),
                          fill=clr, width=1, tags=("static",))
            if self.show_rssi:
                c.create_text((self._sx(n.x) + self._sx(m.x)) / 2,
                              (self._sy(n.y) + self._sy(m.y)) / 2,
                              text=f"{rssi:.0f}", fill=clr,
                              font=("Courier", 7), tags=("static",))

    def _draw_parent_arrows(self, s):
        c = self.canvas
        for n in self.net.nodes:
            if n.parent_id is None or n.state in (NodeState.DEAD, NodeState.ORPHAN):
                continue
            par = self.net._node(n.parent_id)
            if par is None:
                continue
            dx, dy = par.x - n.x, par.y - n.y
            length = max(math.hypot(dx, dy), 0.001)
            ux, uy = dx / length, dy / length
            inset  = 12 / s
            c.create_line(self._sx(n.x + ux*inset),   self._sy(n.y + uy*inset),
                          self._sx(par.x - ux*inset),  self._sy(par.y - uy*inset),
                          fill=_dim(ACCENT, 0.65), width=1,
                          arrow=tk.LAST, arrowshape=(7, 9, 3),
                          tags=("static",))

    def _draw_rings(self, s):
        c = self.canvas
        for ring in self.rings:
            cx, cy = self._sx(ring.x), self._sy(ring.y)
            r   = ring.r * s
            clr = _dim(ring.color, 0.15 + 0.85 * ring.alpha)
            w   = max(1, int(1.5 * ring.alpha + 0.5))
            c.create_oval(cx-r, cy-r, cx+r, cy+r,
                          outline=clr, width=w, fill="", tags=("dynamic",))

    def _draw_packets(self):
        c = self.canvas
        for pk in self.packets:
            t  = pk.progress
            px = self._sx(pk.x0 + t * (pk.x1 - pk.x0))
            py = self._sy(pk.y0 + t * (pk.y1 - pk.y0))
            fade = max(0.0, min(1.0, 1.0 - abs(t - 0.5) * 1.6))
            r    = 4
            c.create_oval(px-r, py-r, px+r, py+r,
                          fill=_dim(pk.color, 0.3 + 0.7 * fade),
                          outline="", tags=("dynamic",))

    def _draw_nodes(self, s, many_nodes: bool):
        c = self.canvas
        for n in self.net.nodes:
            cx, cy = self._sx(n.x), self._sy(n.y)
            is_sel = (n.id == self.selected_id)
            is_hov = (n.id == self.hover_id)

            if n.state == NodeState.DEAD:
                sz = 6.0
                c.create_line(cx-sz, cy-sz, cx+sz, cy+sz, fill="#3d4450", width=2, tags=("dynamic",))
                c.create_line(cx+sz, cy-sz, cx-sz, cy+sz, fill="#3d4450", width=2, tags=("dynamic",))
                continue

            clr    = self._node_color(n)
            base_r = self._node_radius(n, s)
            pulse  = (1.0 + 0.16 * math.sin(self._pulse_phase + n.id * 0.7 +
                      (1.5 if n.state == NodeState.ORPHAN else 0)))
            draw_r = base_r * pulse if n.state in (NodeState.MASTER, NodeState.ORPHAN) else base_r

            # Glow (skip when many nodes for performance)
            if not many_nodes or is_sel or is_hov:
                glow_r   = draw_r * (2.8 if is_sel or is_hov else 1.9)
                glow_clr = _dim(clr, 0.22 if n.state != NodeState.MASTER else 0.32)
                c.create_oval(cx-glow_r, cy-glow_r, cx+glow_r, cy+glow_r,
                              fill=glow_clr, outline="", tags=("dynamic",))

            c.create_oval(cx-draw_r, cy-draw_r, cx+draw_r, cy+draw_r,
                          fill=clr, outline=_lighten(clr, 1.3), width=1,
                          tags=("dynamic",))

            if is_sel:
                sel_r = draw_r + 4
                c.create_oval(cx-sel_r, cy-sel_r, cx+sel_r, cy+sel_r,
                              outline="white", width=2, dash=(4, 3), fill="",
                              tags=("dynamic",))

            if n.state == NodeState.MASTER:
                self._draw_star(cx, cy, draw_r * 0.55)

            if n.state == NodeState.SYNCED and n.hop_count > 0:
                fsz = max(5, int(5 * s / 0.35))
                c.create_text(cx, cy, text=str(n.hop_count), fill="white",
                              font=("Courier", fsz, "bold"), tags=("dynamic",))

            if self.show_labels:
                fsz = max(6, int(6 * s / 0.35))
                c.create_text(cx + draw_r + 2, cy - draw_r + 3,
                              text=f"N{n.id}", fill=_lighten(clr, 1.5),
                              font=("Courier", fsz), anchor="w", tags=("dynamic",))

    def _draw_star(self, cx, cy, r):
        pts = []
        for i in range(10):
            a  = math.pi / 2 + i * math.pi / 5
            rr = r if i % 2 == 0 else r * 0.42
            pts += [cx + rr * math.cos(a), cy - rr * math.sin(a)]
        self.canvas.create_polygon(pts, fill="white", outline="", tags=("dynamic",))

    @staticmethod
    def _node_color(n: SimNode) -> str:
        return {NodeState.MASTER: MASTER_CLR, NodeState.SYNCED: SYNCED_CLR,
                NodeState.ORPHAN: ORPHAN_CLR, NodeState.DEAD:   DEAD_CLR}[n.state]

    @staticmethod
    def _node_radius(n: SimNode, s: float) -> float:
        return {NodeState.MASTER: 10, NodeState.SYNCED: 6,
                NodeState.ORPHAN: 8,  NodeState.DEAD:   5}[n.state] * max(0.5, s / 0.35)

    def _on_click(self, event):
        if self.wall_mode:
            self._update_scale_cache()
            self._wall_start = self._to_sim(event.x, event.y)
            return
        nid = self._node_at(event.x, event.y)
        self.selected_id = nid
        if self.on_node_click and nid >= 0:
            self.on_node_click(nid)

    def _on_motion(self, event):
        if self.wall_mode:
            if self._wall_start is not None:
                # Draw live preview line (tagged so it's cleaned up each frame)
                self.canvas.delete("wall_preview")
                self._update_scale_cache()
                x2, y2 = self._to_sim(event.x, event.y)
                x1, y1 = self._wall_start
                self.canvas.create_line(
                    self._sx(x1), self._sy(y1),
                    self._sx(x2), self._sy(y2),
                    fill=WALL_CLR, width=5, dash=(6, 4),
                    capstyle=tk.ROUND, tags=("wall_preview",))
            return
        self.hover_id = self._node_at(event.x, event.y)

    def _on_release(self, event):
        if self.wall_mode and self._wall_start is not None:
            self._update_scale_cache()
            x2, y2 = self._to_sim(event.x, event.y)
            x1, y1 = self._wall_start
            self._wall_start = None
            self.canvas.delete("wall_preview")
            if math.hypot(x2 - x1, y2 - y1) > 15:   # min length
                self.net.walls.append((x1, y1, x2, y2))
                self.mark_topo_dirty()


# ─── Hierarchy canvas ─────────────────────────────────────────────────────────
class HierarchyCanvas:
    """Snapshot view of the sync tree coloured by hop depth from the master.
    Intended to be shown in a separate tab while the simulation is paused."""

    def __init__(self, master_widget, network: SimNetwork):
        self.net = network
        self._s = 0.5; self._ox = 0.0; self._oy = 0.0
        self.canvas = tk.Canvas(master_widget, bg=BG, highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        # Redraw whenever the canvas is resized or first mapped — this is what
        # makes the tab work on first open regardless of layout timing.
        self.canvas.bind("<Configure>", lambda _: self.redraw())

    def _update_scale(self):
        w = self.canvas.winfo_width()
        h = self.canvas.winfo_height()
        if w > 10 and h > 10:
            self._s  = min(w, h) / FIELD_SIZE
            self._ox = (w - FIELD_SIZE * self._s) / 2
            self._oy = (h - FIELD_SIZE * self._s) / 2

    def _sx(self, x): return x * self._s + self._ox
    def _sy(self, y): return y * self._s + self._oy

    def redraw(self):
        c = self.canvas
        c.delete("all")
        self._update_scale()
        s = self._s

        # Grid
        ox, oy = self._ox, self._oy
        fs = FIELD_SIZE * s
        for i in range(0, FIELD_SIZE + 1, 100):
            v = i * s
            c.create_line(ox+v, oy, ox+v, oy+fs, fill=GRID_CLR, width=1)
            c.create_line(ox, oy+v, ox+fs, oy+v, fill=GRID_CLR, width=1)

        depths = self.net.bfs_depths()

        # Walls
        for x1, y1, x2, y2 in self.net.walls:
            c.create_line(self._sx(x1), self._sy(y1),
                          self._sx(x2), self._sy(y2),
                          fill=WALL_CLR, width=5, capstyle=tk.ROUND)

        # Parent tree edges coloured by child depth
        for n in self.net.nodes:
            if n.parent_id is None or n.state in (NodeState.DEAD, NodeState.ORPHAN):
                continue
            par = self.net._node(n.parent_id)
            if par is None:
                continue
            d   = depths.get(n.id, -1)
            clr = DEPTH_COLORS[min(d, len(DEPTH_COLORS) - 1)] if d >= 0 else "#555555"
            c.create_line(self._sx(n.x), self._sy(n.y),
                          self._sx(par.x), self._sy(par.y),
                          fill=_dim(clr, 0.55), width=1)

        # Nodes
        for n in self.net.nodes:
            cx, cy = self._sx(n.x), self._sy(n.y)
            if n.state == NodeState.DEAD:
                clr, r = "#2d3139", 5
            elif n.state == NodeState.ORPHAN:
                clr, r = ORPHAN_CLR, 7
            else:
                d   = depths.get(n.id, -1)
                clr = DEPTH_COLORS[min(d, len(DEPTH_COLORS) - 1)] if d >= 0 else "#888888"
                r   = 10 if n.state == NodeState.MASTER else 7
            c.create_oval(cx - r, cy - r, cx + r, cy + r,
                          fill=clr, outline=_lighten(clr, 1.3), width=1)
            if n.state != NodeState.DEAD:
                label = "M" if n.state == NodeState.MASTER else str(depths.get(n.id, "?"))
                c.create_text(cx, cy, text=label,
                              fill="#000000" if n.state == NodeState.MASTER else "#ffffff",
                              font=("Courier", 7, "bold"))

        # Legend
        self._draw_legend(c, depths)

    def _draw_legend(self, c, depths):
        depth_counts: Dict[int, int] = {}
        for n in self.net.nodes:
            if n.id in depths:
                d = depths[n.id]
                depth_counts[d] = depth_counts.get(d, 0) + 1
        orphans = sum(1 for n in self.net.nodes if n.state == NodeState.ORPHAN)
        dead    = sum(1 for n in self.net.nodes if n.state == NodeState.DEAD)

        rows = sorted(depth_counts.keys())
        total_rows = len(rows) + (1 if orphans else 0) + (1 if dead else 0) + 1
        box_h = 16 * total_rows + 14
        x, y  = 10, 10
        c.create_rectangle(x, y, x + 148, y + box_h,
                           fill="#0d1117", outline="#30363d")
        y += 8
        c.create_text(x + 74, y, text="SYNC HIERARCHY",
                      fill="#8b949e", font=("Courier", 8, "bold"), anchor="n")
        y += 16
        for d in rows:
            clr   = DEPTH_COLORS[min(d, len(DEPTH_COLORS) - 1)]
            label = "Master (depth 0)" if d == 0 else f"Depth {d}"
            cnt   = depth_counts[d]
            c.create_rectangle(x + 8, y - 5, x + 20, y + 7, fill=clr, outline="")
            c.create_text(x + 26, y, text=f"{label}: {cnt}",
                          fill=clr, font=("Courier", 8), anchor="w")
            y += 16
        if orphans:
            c.create_rectangle(x + 8, y - 5, x + 20, y + 7, fill=ORPHAN_CLR, outline="")
            c.create_text(x + 26, y, text=f"Orphaned: {orphans}",
                          fill=ORPHAN_CLR, font=("Courier", 8), anchor="w")
            y += 16
        if dead:
            c.create_rectangle(x + 8, y - 5, x + 20, y + 7,
                               fill="#2d3139", outline="#555")
            c.create_text(x + 26, y, text=f"Dead: {dead}",
                          fill="#555", font=("Courier", 8), anchor="w")


# ─── Live bottom graphs ───────────────────────────────────────────────────────
class LiveGraph:
    """Scrolling line graph with fill, gridlines, and current-value label."""
    def __init__(self, master, label: str, color: str, max_val: float = 100):
        self.label   = label
        self.color   = color
        self.max_val = max_val
        self.data    = []
        self.canvas  = tk.Canvas(master, bg=BG, highlightthickness=0)
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=1)

    def push(self, val: float):
        self.data.append(val)
        if len(self.data) > 200:
            self.data = self.data[-200:]
        self._redraw()

    def _redraw(self):
        c = self.canvas
        c.delete("all")
        w = c.winfo_width()
        h = c.winfo_height()
        if w < 10 or h < 10 or not self.data:
            return

        # Faint horizontal grid lines at 25 / 50 / 75 %
        for pct in (25, 50, 75):
            y = h - (pct / 100.0) * (h - 18) - 4
            c.create_line(0, y, w, y, fill=GRID_CLR, width=1)

        # Data area
        pts = [(w * i / max(1, len(self.data) - 1),
                h - (v / max(self.max_val, 1e-3)) * (h - 18) - 4)
               for i, v in enumerate(self.data)]
        if len(pts) >= 2:
            poly = []
            for x, y in pts:
                poly += [x, y]
            poly += [pts[-1][0], h, pts[0][0], h]
            c.create_polygon(poly, fill=_dim(self.color, 0.18), outline="")
            lpts = []
            for x, y in pts:
                lpts += [x, y]
            c.create_line(lpts, fill=self.color, width=1)

        # Label + current value
        last = self.data[-1]
        c.create_text(w - 4, 9, text=f"{self.label}: {last:.1f}",
                      fill=_lighten(self.color, 1.2), font=("Courier", 8), anchor="e")
        # Peak marker
        peak = max(self.data)
        c.create_text(4, 9, text=f"pk {peak:.1f}",
                      fill=_dim(self.color, 0.7), font=("Courier", 7), anchor="w")


class BottomGraphs:
    HEIGHT = 88

    def __init__(self, master):
        tk.Frame(master, bg="#30363d", height=1).pack(fill=tk.X)
        row = tk.Frame(master, bg=BG)
        row.pack(fill=tk.BOTH, expand=True)
        self.g_sync = LiveGraph(row, "Sync %",         SYNCED_CLR,  100)
        self.g_coll = LiveGraph(row, "Collision %",    COLLISION_CLR, 100)
        self.g_load = LiveGraph(row, "Channel Load %", MASTER_CLR,  100)
        # Dividers between graphs
        tk.Frame(row, bg="#30363d", width=1).place(relx=0.333, rely=0, relheight=1)
        tk.Frame(row, bg="#30363d", width=1).place(relx=0.666, rely=0, relheight=1)

    def push(self, sync_pct: float, coll_pct: float, load_pct: float):
        self.g_sync.push(sync_pct)
        self.g_coll.push(coll_pct)
        self.g_load.push(load_pct)


# ─── Sparkline (side panel) ───────────────────────────────────────────────────
class SparkLine:
    def __init__(self, master, label: str, color: str, max_val: float = 100):
        self.label, self.color, self.max_val = label, color, max_val
        self.data   = []
        self.canvas = tk.Canvas(master, bg=BG, height=40, highlightthickness=0)
        self.canvas.pack(fill=tk.X, padx=2, pady=1)

    def push(self, val: float):
        self.data.append(val)
        if len(self.data) > 120:
            self.data = self.data[-120:]
        self._redraw()

    def _redraw(self):
        c = self.canvas
        c.delete("all")
        if not self.data:
            return
        w, h = c.winfo_width(), c.winfo_height()
        if w < 4 or h < 4:
            return
        pts = [(w * i / max(1, len(self.data) - 1),
                h - (v / max(self.max_val, 1e-3)) * (h - 4) - 2)
               for i, v in enumerate(self.data)]
        if len(pts) >= 2:
            poly = []
            for x, y in pts:
                poly += [x, y]
            poly += [pts[-1][0], h, pts[0][0], h]
            c.create_polygon(poly, fill=_dim(self.color, 0.2), outline="")
            lpts = []
            for x, y in pts:
                lpts += [x, y]
            c.create_line(lpts, fill=self.color, width=1)
        last = self.data[-1]
        c.create_text(3, 9, text=f"{self.label}: {last:.1f}",
                      fill=_lighten(self.color, 1.3), font=("Courier", 8), anchor="w")


# ─── Control panel ────────────────────────────────────────────────────────────
class ControlPanel:
    def __init__(self, master_widget, network: SimNetwork):
        self.net              = network
        self.selected_node_id = -1
        self._paused          = False
        self._speed           = 5
        self.on_reset            = None
        self.on_kill             = None
        self.on_revive           = None
        self.on_add              = None
        self.on_node_action      = None
        self.on_wall_mode_changed = None
        self.on_clear_walls      = None

        self.frame = tk.Frame(master_widget, bg=PANEL_BG, width=285)
        self.frame.pack(side=tk.RIGHT, fill=tk.Y)
        self.frame.pack_propagate(False)
        self._build_ui()

    def _build_ui(self):
        f = self.frame
        tk.Label(f, text="LoRa Mesh Simulator", bg=PANEL_BG, fg="#f0f6fc",
                 font=("Helvetica", 13, "bold"), pady=6).pack(fill=tk.X, padx=6)
        tk.Frame(f, bg="#30363d", height=1).pack(fill=tk.X)

        scroll_outer = tk.Frame(f, bg=PANEL_BG)
        scroll_outer.pack(fill=tk.BOTH, expand=True)
        self._sc = tk.Canvas(scroll_outer, bg=PANEL_BG, highlightthickness=0)
        sb = tk.Scrollbar(scroll_outer, orient="vertical", command=self._sc.yview)
        self._sc.configure(yscrollcommand=sb.set)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        self._sc.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.inner = tk.Frame(self._sc, bg=PANEL_BG)
        win = self._sc.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>",
                        lambda *_: self._sc.configure(scrollregion=self._sc.bbox("all")))
        self._sc.bind("<Configure>",
                      lambda e: self._sc.itemconfig(win, width=e.width))
        self._sc.bind("<Enter>", lambda *_: self._sc.bind_all("<MouseWheel>", self._mwheel))
        self._sc.bind("<Leave>", lambda *_: self._sc.unbind_all("<MouseWheel>"))
        self._build_inner()

    def _mwheel(self, event):
        self._sc.yview_scroll(int(-1 * (event.delta / 120)), "units")

    def _build_inner(self):
        p = self.inner

        self._section(p, "Network Parameters")
        self._sl_nodes      = self._slider(p, "Nodes",             5,   200, DEF_NODES,         None)
        self._sl_range      = self._slider(p, "LoRa Range",        50,  500, DEF_RANGE,         "lora_range")
        self._sl_sync_int   = self._slider(p, "Sync Interval",     10,  300, DEF_SYNC_INTERVAL, "sync_interval")
        self._sl_orphan_thr = self._slider(p, "Orphan Threshold",  20,  600, DEF_ORPHAN_THRESH, "orphan_threshold")
        self._sl_jitter_sc  = self._slider(p, "RSSI Jitter Scale", 0,   15,  DEF_JITTER_SCALE,  "jitter_scale")
        self._sl_jitter_max = self._slider(p, "Jitter Max",        0,   80,  DEF_JITTER_MAX,    "jitter_max")
        self._sl_rtr_int    = self._slider(p, "RTR Interval",      5,   60,  DEF_RTR_INTERVAL,  "rtr_interval")

        self._section(p, "Congestion")
        self._sl_duty       = self._slider(p, "Duty Cycle (ticks)", 0, 500, DEF_DUTY_INTERVAL, "duty_min_interval")

        self._section(p, "Simulation")
        speed_row = tk.Frame(p, bg=PANEL_BG)
        speed_row.pack(fill=tk.X, padx=6, pady=2)
        tk.Label(speed_row, text="Speed:", bg=PANEL_BG, fg="#8b949e",
                 font=("Helvetica", 10)).pack(side=tk.LEFT)
        self._speed_var = tk.IntVar(value=5)
        tk.Scale(speed_row, from_=1, to=20, orient=tk.HORIZONTAL, variable=self._speed_var,
                 bg=PANEL_BG, fg="#8b949e", troughcolor="#30363d",
                 highlightthickness=0, bd=0, showvalue=True,
                 command=lambda v: setattr(self, '_speed', int(float(v)))
                 ).pack(side=tk.LEFT, fill=tk.X, expand=True)

        row = tk.Frame(p, bg=PANEL_BG)
        row.pack(fill=tk.X, padx=6, pady=2)
        self._btn_pause = self._btn(row, "Pause", "#8b949e", self._on_pause)
        self._btn(row, "Reset", ACCENT, self._on_reset).pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._btn_pause.pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)

        row2 = tk.Frame(p, bg=PANEL_BG)
        row2.pack(fill=tk.X, padx=6, pady=2)
        self._btn(row2, "Kill 1", ORPHAN_CLR, lambda: self.on_kill and self.on_kill(1)
                  ).pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._btn(row2, "Kill 5", "#FF3333",  lambda: self.on_kill and self.on_kill(5)
                  ).pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._btn(p, "Revive All", SYNCED_CLR,
                  lambda: self.on_revive and self.on_revive()
                  ).pack(fill=tk.X, padx=8, pady=2)
        row3 = tk.Frame(p, bg=PANEL_BG)
        row3.pack(fill=tk.X, padx=6, pady=2)
        self._btn(row3, "+ Add 5",  RELAY_CLR, lambda: self.on_add and self.on_add(5)
                  ).pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._btn(row3, "+ Add 20", RELAY_CLR, lambda: self.on_add and self.on_add(20)
                  ).pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._btn(p, "Stress Test (Kill 30%)", "#FF3333", self._on_stress
                  ).pack(fill=tk.X, padx=8, pady=2)

        self._section(p, "Selected Node")
        self._node_info = tk.Label(p, text="Click a node to inspect",
                                    bg=PANEL_BG, fg="#8b949e",
                                    font=("Courier", 10), justify=tk.LEFT,
                                    anchor="w", wraplength=250)
        self._node_info.pack(fill=tk.X, padx=8, pady=2)
        row_n = tk.Frame(p, bg=PANEL_BG)
        row_n.pack(fill=tk.X, padx=6, pady=2)
        self._btn(row_n, "Kill",   ORPHAN_CLR,
                  lambda: self.on_node_action and self.on_node_action("kill", self.selected_node_id)
                  ).pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._btn(row_n, "Revive", SYNCED_CLR,
                  lambda: self.on_node_action and self.on_node_action("revive", self.selected_node_id)
                  ).pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)

        self._section(p, "Obstacles")
        row_w = tk.Frame(p, bg=PANEL_BG)
        row_w.pack(fill=tk.X, padx=6, pady=2)
        self._wall_var = tk.BooleanVar(value=False)
        self._btn_wall = tk.Checkbutton(
            row_w, text="Draw Walls", variable=self._wall_var,
            bg=PANEL_BG, fg="#8b949e", selectcolor="#8b1a1a",
            activebackground=PANEL_BG, activeforeground=WALL_CLR,
            font=("Courier", 10), indicatoron=False, relief=tk.FLAT,
            command=self._on_wall_toggle)
        self._btn_wall.pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._btn(row_w, "Clear Walls", "#555",
                  lambda: self.on_clear_walls and self.on_clear_walls()
                  ).pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)

        self._section(p, "Visual Options")
        self._chk_ranges  = self._chk(p, "Show range circles",    True)
        self._chk_parents = self._chk(p, "Show parent arrows",    True)
        self._chk_packets = self._chk(p, "Show packet animation", True)
        self._chk_rings   = self._chk(p, "Show signal rings",     True)
        self._chk_labels  = self._chk(p, "Show node IDs",         False)
        self._chk_rssi    = self._chk(p, "Show RSSI on edges",    False)

        self._section(p, "Live Stats")
        self._lbl_synced  = self._stat(p, "Synced:   ?/?",         SYNCED_CLR)
        self._lbl_orphans = self._stat(p, "Orphaned: ?",           ORPHAN_CLR)
        self._lbl_dead    = self._stat(p, "Dead:     ?",           "#8b949e")
        self._lbl_hops    = self._stat(p, "Avg hops: ?",           MASTER_CLR)
        self._lbl_rebuild = self._stat(p, "Rebuild:  -",           ACCENT)
        self._lbl_thr     = self._stat(p, "Throughput: ? kbps",    RELAY_CLR)
        self._lbl_chain   = self._stat(p, "Chain thr:  ? kbps",    ACCENT)
        self._lbl_coll    = self._stat(p, "Collisions: ?%",        COLLISION_CLR)
        self._lbl_load    = self._stat(p, "Chan load:  ?%",        MASTER_CLR)
        self._lbl_ticks   = self._stat(p, "Ticks:    0",           "#8b949e")
        self._lbl_pkts    = self._stat(p, "Pkts sent: 0",          "#8b949e")

        self._section(p, "History")
        self.spark_sync   = SparkLine(p, "Sync%",    SYNCED_CLR,    100)
        self.spark_orphan = SparkLine(p, "Orphan%",  ORPHAN_CLR,    100)
        self.spark_coll   = SparkLine(p, "Coll%",    COLLISION_CLR, 100)

        self._section(p, "Legend")
        for clr, txt in ((MASTER_CLR,    "* Sync Master"),
                          (SYNCED_CLR,   "o Synced follower"),
                          (RELAY_CLR,    "o Actively relaying"),
                          (ORPHAN_CLR,   "o Orphan (searching)"),
                          (COLLISION_CLR,"x Collision event"),
                          ("#4a5568",    "x Dead node")):
            row = tk.Frame(p, bg=PANEL_BG)
            row.pack(fill=tk.X, padx=8, pady=1)
            tk.Label(row, text="■", bg=PANEL_BG, fg=clr,
                     font=("Helvetica", 12)).pack(side=tk.LEFT)
            tk.Label(row, text=txt, bg=PANEL_BG, fg="#8b949e",
                     font=("Helvetica", 10)).pack(side=tk.LEFT, padx=4)
        tk.Frame(p, bg=PANEL_BG, height=10).pack()

    # ── factories ─────────────────────────────────────────────────────────────
    def _section(self, parent, title: str):
        tk.Frame(parent, bg="#30363d", height=1).pack(fill=tk.X, pady=(8, 0))
        tk.Label(parent, text=title, bg=PANEL_BG, fg="#8b949e",
                 font=("Helvetica", 10, "bold"), anchor="w", padx=6
                 ).pack(fill=tk.X, pady=(2, 2))

    def _slider(self, parent, label, lo, hi, val, net_attr):
        row = tk.Frame(parent, bg=PANEL_BG)
        row.pack(fill=tk.X, padx=6, pady=1)
        tk.Label(row, text=label, bg=PANEL_BG, fg="#8b949e",
                 font=("Helvetica", 10), width=16, anchor="w").pack(side=tk.LEFT)
        var     = tk.IntVar(value=val)
        val_lbl = tk.Label(row, text=str(val), bg=PANEL_BG, fg="#f0f6fc",
                           font=("Courier", 10), width=4)
        val_lbl.pack(side=tk.RIGHT)
        def _cb(v, attr=net_attr, vl=val_lbl):
            iv = int(float(v)); vl.configure(text=str(iv))
            if attr: setattr(self.net, attr, iv)
        tk.Scale(row, from_=lo, to=hi, orient=tk.HORIZONTAL, variable=var,
                 bg=PANEL_BG, fg="#8b949e", troughcolor="#30363d",
                 highlightthickness=0, bd=0, showvalue=False,
                 command=_cb).pack(side=tk.LEFT, fill=tk.X, expand=True)
        return var

    def _chk(self, parent, label, default):
        var = tk.BooleanVar(value=default)
        tk.Checkbutton(parent, text=label, variable=var,
                       bg=PANEL_BG, fg="#8b949e",
                       selectcolor=PANEL_BG, activebackground=PANEL_BG,
                       font=("Helvetica", 10)).pack(anchor="w", padx=8, pady=1)
        return var

    def _btn(self, parent, text, color, cmd):
        return tk.Button(parent, text=text, command=cmd,
                         bg=PANEL_BG, fg=color,
                         activebackground="#21262d", activeforeground=color,
                         relief=tk.FLAT, bd=0,
                         highlightbackground="#30363d", highlightthickness=1,
                         font=("Helvetica", 10), cursor="hand2")

    def _stat(self, parent, text, color):
        lbl = tk.Label(parent, text=text, bg=PANEL_BG, fg=color,
                       font=("Courier", 10), anchor="w", padx=8)
        lbl.pack(fill=tk.X)
        return lbl

    # ── handlers ──────────────────────────────────────────────────────────────
    def _on_reset(self):
        if self.on_reset: self.on_reset(self._sl_nodes.get())

    def _on_pause(self):
        self._paused = not self._paused
        self._btn_pause.configure(text="Resume" if self._paused else "Pause")

    def _on_stress(self):
        count = max(1, len(self.net.nodes) * 3 // 10)
        if self.on_kill: self.on_kill(count)

    def _on_wall_toggle(self):
        enabled = self._wall_var.get()
        self._btn_wall.config(fg=WALL_CLR if enabled else "#8b949e")
        if self.on_wall_mode_changed:
            self.on_wall_mode_changed(enabled)

    @property
    def paused(self) -> bool: return self._paused

    @paused.setter
    def paused(self, value: bool):
        self._paused = value
        self._btn_pause.configure(text="Resume" if value else "Pause")
    @property
    def speed(self)  -> int:  return self._speed

    # ── updates ───────────────────────────────────────────────────────────────
    def update_stats(self, st: dict):
        total  = max(1, st['total'])
        synced = st['synced']
        alive  = st['alive']
        pct    = st['sync_pct_alive']   # % of alive nodes that are synced
        self._lbl_synced.configure(text=f"Synced:   {synced}/{alive}  ({pct:.0f}% of alive)")
        self._lbl_orphans.configure(text=f"Orphaned: {st['orphans']}")
        self._lbl_dead.configure(text=f"Dead:     {st['dead']}  (total: {total})")
        self._lbl_hops.configure(text=f"Avg hops: {st['avg_hops']:.1f}")
        rb = st['rebuild_ticks']
        self._lbl_rebuild.configure(text=f"Rebuild:  {rb} ticks" if rb else "Rebuild:  -")
        self._lbl_thr.configure(text=f"Throughput: {st['throughput_kbps']:.1f} kbps")
        self._lbl_chain.configure(text=f"Chain thr:  {st['chain_throughput_kbps']:.1f} kbps")
        self._lbl_coll.configure(text=f"Collisions: {st['collision_pct']:.1f}%  "
                                       f"(last {st['rolling_coll_pct']:.0f}%)")
        self._lbl_load.configure(text=f"Chan load:  {st['load_pct']:.1f}%")
        self._lbl_ticks.configure(text=f"Ticks:    {st['ticks']}")
        self._lbl_pkts.configure(text=f"Pkts sent: {st['packets']}")
        self.spark_sync.push(pct)
        self.spark_orphan.push(100 * st['orphans'] / total)
        self.spark_coll.push(st['rolling_coll_pct'])

    def update_selected_node(self, net: SimNetwork, nid: int):
        self.selected_node_id = nid
        if nid < 0:
            self._node_info.configure(text="Click a node to inspect")
            return
        n = net._node(nid)
        if n is None: return
        par  = f"N{n.parent_id}" if n.parent_id is not None else "-"
        rssi = f"{n.rssi_to_parent:.0f} dBm" if n.parent_id is not None else "-"
        duty = (f"tx cooldown: {max(0, net.duty_min_interval - (net.tick - n.last_tx_tick))} tk"
                if net.duty_min_interval > 0 else "duty: off")
        snr_str = (f"{n.rssi_to_parent - NOISE_FLOOR_DBM:.1f} dB"
                   if n.parent_id is not None else "-")
        self._node_info.configure(
            text=f"Node {nid}  [{n.state.name}]\n"
                 f"Parent: {par}   RSSI: {rssi}   SNR: {snr_str}\n"
                 f"Hops: {n.hop_count}   Last sync: {n.last_sync_tick}\n"
                 f"Fwd count: {n.forward_count}   {duty}"
        )

    def visual_flags(self) -> dict:
        return dict(ranges=self._chk_ranges.get(), parents=self._chk_parents.get(),
                    packets=self._chk_packets.get(), rings=self._chk_rings.get(),
                    labels=self._chk_labels.get(),  rssi=self._chk_rssi.get())


# ─── Main application ─────────────────────────────────────────────────────────
class MainApp:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("LoRa Mesh Simulator — Spring 2025 Directed Study")
        self.root.geometry("1200x760")
        self.root.configure(bg=BG)
        self.root.minsize(900, 580)

        self.net = SimNetwork()
        main = tk.Frame(self.root, bg=BG)
        main.pack(fill=tk.BOTH, expand=True)

        # Right panel
        self.panel = ControlPanel(main, self.net)

        # Left: tabbed view (Network | Hierarchy)
        left = tk.Frame(main, bg=BG)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        style = ttk.Style()
        style.theme_use('clam')
        style.configure('Mesh.TNotebook',     background=BG,        borderwidth=0)
        style.configure('Mesh.TNotebook.Tab', background="#161b22", foreground="#8b949e",
                        padding=[12, 4], font=("Courier", 9))
        style.map('Mesh.TNotebook.Tab',
                  background=[('selected', '#21262d')],
                  foreground=[('selected', '#c9d1d9')])

        self._notebook = ttk.Notebook(left, style='Mesh.TNotebook')
        self._notebook.pack(fill=tk.BOTH, expand=True)

        # Tab 1: Network
        tab_net = tk.Frame(self._notebook, bg=BG)
        self._notebook.add(tab_net, text=" Network ")

        net_frame = tk.Frame(tab_net, bg=BG)
        net_frame.pack(fill=tk.BOTH, expand=True)
        self.canvas = NetworkCanvas(net_frame, self.net)
        self.canvas.on_node_click = self._on_node_clicked

        graphs_frame = tk.Frame(tab_net, bg=BG, height=BottomGraphs.HEIGHT)
        graphs_frame.pack(fill=tk.X, side=tk.BOTTOM)
        graphs_frame.pack_propagate(False)
        self.graphs = BottomGraphs(graphs_frame)

        # Tab 2: Hierarchy
        tab_hier = tk.Frame(self._notebook, bg=BG)
        self._notebook.add(tab_hier, text=" Hierarchy ")
        self.hier_canvas = HierarchyCanvas(tab_hier, self.net)

        self._notebook.bind("<<NotebookTabChanged>>", self._on_tab_changed)
        self._auto_paused = False

        # Status bar
        self._status = tk.Label(self.root, text="Initialising...",
                                 bg="#161b22", fg="#8b949e",
                                 font=("Courier", 10), anchor="w", padx=6)
        self._status.pack(fill=tk.X, side=tk.BOTTOM)

        # Wire callbacks
        self.panel.on_reset            = self._on_reset
        self.panel.on_kill             = self._on_kill
        self.panel.on_revive           = self._on_revive
        self.panel.on_add              = self._on_add
        self.panel.on_node_action      = self._on_node_action
        self.panel.on_wall_mode_changed = self._on_wall_mode_changed
        self.panel.on_clear_walls      = self._on_clear_walls

        self._frame = 0
        self.net.reset(DEF_NODES)
        self.root.after(120, self._tick)

    def _on_reset(self, n):
        self.net.reset(n)
        self.canvas.rings.clear()
        self.canvas.packets.clear()
        self.canvas.mark_topo_dirty()

    def _on_kill(self, count):
        self.net.kill_random(count)
        self.canvas.mark_topo_dirty()

    def _on_revive(self):
        self.net.revive_all()
        self.canvas.mark_topo_dirty()

    def _on_add(self, count):
        self.net.add_nodes(count)
        self.canvas.mark_topo_dirty()

    def _on_node_clicked(self, nid: int):
        self.panel.update_selected_node(self.net, nid)
        self.canvas.selected_id = nid

    def _on_node_action(self, action: str, nid: int):
        n = self.net._node(nid)
        if n is None: return
        if action == "kill":
            self.net._kill(n)
        elif action == "revive" and n.state == NodeState.DEAD:
            n.state, n.orphan_since = NodeState.ORPHAN, self.net.tick
            n.rtr_next_at = self.net.tick + 3
        self.canvas.mark_topo_dirty()
        self.panel.update_selected_node(self.net, nid)

    def _on_wall_mode_changed(self, enabled: bool):
        self.canvas.wall_mode = enabled
        self.canvas.canvas.config(cursor="crosshair" if enabled else "")
        if enabled:
            self.canvas.canvas.bind("<Escape>", lambda _: self._exit_wall_mode())
        else:
            self.canvas.canvas.unbind("<Escape>")
            self.canvas._wall_start = None
            self.canvas.canvas.delete("wall_preview")

    def _exit_wall_mode(self):
        self.panel._wall_var.set(False)
        self.panel._on_wall_toggle()   # keeps button label + callback in sync

    def _on_clear_walls(self):
        self.net.walls.clear()
        self.canvas.mark_topo_dirty()

    def _on_tab_changed(self, *_):
        tab = self._notebook.index(self._notebook.select())
        if tab == 1:   # Hierarchy tab
            if not self.panel.paused:
                self.panel.paused = True
                self._auto_paused = True
            self.root.update_idletasks()   # flush layout so canvas has real size
            self.hier_canvas.redraw()
        else:          # Network tab
            if self._auto_paused and self.panel.paused:
                self.panel.paused = False
            self._auto_paused = False

    def _tick(self):
        if not self.panel.paused:
            for _ in range(self.panel.speed):
                self.net.step()
            self.canvas.ingest_events()

        flags = self.panel.visual_flags()
        self.canvas.show_ranges  = flags['ranges']
        self.canvas.show_parents = flags['parents']
        self.canvas.show_packets = flags['packets']
        self.canvas.show_rings   = flags['rings']
        self.canvas.show_labels  = flags['labels']
        self.canvas.show_rssi    = flags['rssi']

        self.canvas.advance_animations()
        self.canvas.redraw()

        self._frame += 1
        if self._frame % 6 == 0:
            st = self.net.stats()
            self.panel.update_stats(st)
            self.panel.update_selected_node(self.net, self.canvas.selected_id)
            self.graphs.push(st['sync_pct_alive'],
                             st['rolling_coll_pct'],
                             st['rolling_load_pct'])
            self._update_status(st)

        self.root.after(33, self._tick)

    def _update_status(self, st: dict):
        pct   = st['sync_pct_alive']
        self._status.configure(
            text=(f"Tick {st['ticks']}  |  "
                  f"Synced {st['synced']}/{st['alive']} ({pct:.0f}%)  |  "
                  f"Orphaned {st['orphans']}  |  Dead {st['dead']}  |  "
                  f"Collisions {st['collision_pct']:.1f}%  |  "
                  f"PHY err {st['phy_error_pct']:.1f}%  |  "
                  f"Chan load {st['load_pct']:.1f}%  |  "
                  f"Top relay {st['max_fwd_share']:.0f}%  |  "
                  f"Pkts {st['packets']}  |  "
                  f"Thr {st['throughput_kbps']:.1f} kbps"))

    def run(self):
        self.root.mainloop()


# ─── Entry point ──────────────────────────────────────────────────────────────
def main():
    MainApp().run()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == '--headless':
        _run_headless_simulation()
    else:
        main()
