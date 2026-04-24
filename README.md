# LoRa Mesh Network with Synchronized Sleep and Adaptive Forwarding

A multi-hop LoRa mesh networking stack for RAK4630 hardware, built around a synchronized deep-sleep duty cycle. Nodes spend most of their time asleep and wake together on a shared schedule, so battery life is measured in months rather than days. Forwarding decisions are made locally using RSSI-weighted jitter and an optional load-aware backpressure mechanism with NO routing tables, no always-on relays.

Developed as a directed study project at Northeastern University, Spring 2026.

---

## What it does

- Nodes synchronize wake timing around a periodic beacon, then go back to deep sleep
- Packets are flood-forwarded with duplicate suppression so whichever relay has the best link wins the jitter race and forwards first
- A neighbor RSSI table tracks link quality and biases forwarding toward stronger paths
- An optional load-aware jitter extension redistributes traffic away from congested relays
- Orphan recovery lets a node that loses sync request a beacon retransmit and rejoin automatically

The Python simulations let you run any of this without hardware -- useful for testing forwarding behavior at scales beyond a four-node bench setup.

---

## Hardware

- **RAK4630** WisBlock module (nRF52840 + SX1262)
- 4 nodes used in development; the protocol should generalize to more
- 915 MHz, SF12, 22 dBm, 125 kHz BW (US ISM band, FCC Part 15)
- Tested with a Bosch BME680 and InvenSense MPU-9250 on the sensor node

---

## Flashing the firmware

You need [PlatformIO](https://platformio.org/). The easiest way is the VS Code extension.

1. Open this folder in VS Code
2. Pick your build target from `platformio.ini` (e.g. `meshnode1_fullsleep_sync` for the sensor/beacon node, `meshnode2_fullsleep_sync` through `meshnode4_fullsleep_sync` for the relays)
3. Hit Upload

Each node ID is set at compile time. The only difference between node binaries is that flag.

---

## Running the simulations

No hardware needed. Requires Python 3.9+ and matplotlib.

```bash
pip install matplotlib networkx numpy
python simulation/mesh_sim.py
```

Other scripts in `simulation/`:

| Script | What it does |
|---|---|
| `sim_heuristic.py` | Compares RSSI-only vs load-aware jitter across topologies |
| `sim_analysis.py` | Five batch experiments, saves plots to `simulation/results/` |
| `sim_timing.py` | SF12 timing model, cold-start convergence, battery lifetime curves |
| `sim_paper_figures.py` | Generates the figures from the associated report |

Each script saves output to `simulation/results/` and runs headless (no display needed).

---

## Project structure

```
src/           -- main firmware (MeshNode.h is the core mesh stack)
include/       -- LoRa radio config (frequency, SF, TX power)
lib/           -- supporting libraries (async wakeup, power management, etc.)
simulation/    -- Python simulation and analysis scripts
platformio.ini -- build configurations for each node
```

---

Built on [RAKwireless WisBlock](https://docs.rakwireless.com/product-categories/wisblock/) and the [SX126x-Arduino](https://github.com/beegee-tokyo/SX126x-Arduino) radio library.
