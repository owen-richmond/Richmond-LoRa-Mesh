# Project Memory

## Project Overview
RAK4630 (nRF52840 + SX1262) LoRa mesh network. Arduino + PlatformIO.
4 nodes: Node 1 = sensor/sync master, Nodes 2-4 = followers.

## Key Files
- src/MeshNode.h — entire mesh logic (5000+ lines, all inline in class)
- src/MeshPacket.h — packet format (8 packet types, XOR checksum, 21-byte max payload)
- src/NodeMetrics.h — metrics/congestion instrumentation (new, week 6)
- src/main.cpp — setup/loop, sensor TX, MCU idle sleep
- src/SensorManager.cpp — BME680 + MPU9250, 21-byte payload
- include/LoRaConfig.h — SF12, BW125, 868MHz, 22dBm, 68-symbol preamble
- platformio.ini — all build environments

## fullsleep_sync Build Architecture
- 10s wake cycle, 220ms nominal window, 900ms coord buffer → ~1251ms effective window
- effectiveWindow = MESH_WAKE_WINDOW_MS + LORA_PREAMBLE_MARGIN_MS + MESH_SYNC_COORD_BUFFER_MS
- MESH_TX_ONLY_IN_WAKE_WINDOW=1 → no TX outside window
- MCU sleeps via FreeRTOS delay() (RTC tickless idle) between windows
- Node 1 is MESH_SYNC_MASTER; distributes SLEEP_CONTROL_PACKET to followers on startup
- Followers wait awake until SLEEP_CONTROL received, then phase-align cycle

## Gas resistance fix (SensorManager.cpp)
- gas_scaled = gas_resistance / 100 (units: 100Ω steps)
- Correct display: gas_scaled / 10.0f = kΩ  (was /100.0f, off by 10x)

## Metrics Mode (MESH_METRICS_ENABLED=1)
- Build envs: sensor_metrics, meshnode2_metrics, meshnode3_metrics, meshnode4_metrics
- NodeMetrics::tick() called every loop; emits "METRIC key=val ..." lines every 60s
- grep "^METRIC" from serial for clean data extraction
- Overload score 0-100: queue depth(30) + fwd_drops(25) + cad_busy(20) + sc_retry(15) + tx_pressure(10)
- RSSI/SNR now stored in MeshNode._lastRxRssi/_lastRxSnr, exposed via getters

## User Preferences
- Don't touch core fullsleep_sync logic without being certain of no side effects
- All new modes behind compile-time flags so they can't affect production builds
