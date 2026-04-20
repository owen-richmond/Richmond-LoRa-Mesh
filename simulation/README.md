# LoRa Mesh Network Simulator

This simulator demonstrates the dynamic parent-selection and network-rebuild behavior implemented in the LoRa mesh network.

## Features

- Dynamic RSSI shadowing with realistic signal attenuation
- Throughput estimation and link quality metrics
- Multi-hop sync propagation (ripple wave from sync master)
- Sync-orphan detection and network rebuild via RTR
- RSSI-weighted forward jitter (better links forward sooner)

## Usage

### Method 1: VS Code Play Button
Simply click the play button in VS Code - it will automatically use the virtual environment.

### Method 2: Terminal Launcher (Recommended)
```bash
./run_mesh_sim.sh
```
This launcher script automatically uses the virtual environment Python and works regardless of your PATH configuration.

### Method 3: Direct Execution
```bash
source .venv/bin/activate
python simulation/mesh_sim.py
```

## Modes

- **GUI Mode** (default): Interactive visualization with real-time network animation
- **Headless Mode**: Terminal-based simulation with statistics output (use `--headless` flag)

```bash
./run_mesh_sim.sh --headless  # Headless mode via launcher
```

## Dependencies

The virtual environment should have:
- PyQt5 (for GUI mode)
- numpy (for calculations)

Install with:
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install PyQt5 numpy
```