/**
 * @file MeshNode.h
 * @brief Manages LoRa mesh networking logic for a single node.
 * @version 1.0
 * @details This class handles packet creation, reception, validation, and forwarding.
 * It supports optional wake scheduling with pending-traffic beacons and a TX queue.
 * The logic is implemented here.
 */

#ifndef MESH_NODE_H
#define MESH_NODE_H

#include <Arduino.h>
#include <SX126x-Arduino.h>
#include "MeshPacket.h"
#include "OLEDDisplay.h"
#include "ProjectConfig.h"

//==============================================================
// Compile-time configuration defaults (override via build_flags)
//==============================================================
#ifndef MESH_WAKE_CYCLE_MS
#define MESH_WAKE_CYCLE_MS 0
#endif

#ifndef MESH_WAKE_WINDOW_MS
#define MESH_WAKE_WINDOW_MS 0
#endif

#ifndef MESH_PENDING_LISTEN_MS
#define MESH_PENDING_LISTEN_MS 2000
#endif

#ifndef MESH_PENDING_BEACON_ENABLED
#define MESH_PENDING_BEACON_ENABLED 1
#endif

#ifndef MESH_TX_QUEUE_SIZE
#define MESH_TX_QUEUE_SIZE 8
#endif

#ifndef MESH_RX_DUTY_CYCLE_ENABLED
#define MESH_RX_DUTY_CYCLE_ENABLED 0
#endif

#ifndef MESH_RX_DUTY_CYCLE_RX_MS
#define MESH_RX_DUTY_CYCLE_RX_MS 64
#endif

#ifndef MESH_RX_DUTY_CYCLE_SLEEP_MS
#define MESH_RX_DUTY_CYCLE_SLEEP_MS 256
#endif

#ifndef MESH_TX_ONLY_IN_WAKE_WINDOW
#define MESH_TX_ONLY_IN_WAKE_WINDOW 0
#endif

#ifndef MESH_SYNC_BEACON_ENABLED
#define MESH_SYNC_BEACON_ENABLED 0
#endif

#ifndef MESH_SYNC_MASTER
#define MESH_SYNC_MASTER 0
#endif

#ifndef MESH_SYNC_WINDOW_MS
#define MESH_SYNC_WINDOW_MS 1200
#endif

#ifndef MESH_SYNC_BEACON_INTERVAL_MS
#define MESH_SYNC_BEACON_INTERVAL_MS 350
#endif

#ifndef MESH_SYNC_RESYNC_THRESHOLD_MS
#define MESH_SYNC_RESYNC_THRESHOLD_MS 120
#endif

#ifndef MESH_SYNC_COORD_BUFFER_MS
#define MESH_SYNC_COORD_BUFFER_MS 500
#endif

#ifndef MESH_STARTUP_SYNC_LISTEN_MS
#define MESH_STARTUP_SYNC_LISTEN_MS 0
#endif

#ifndef MESH_EVENT_LOGS
#define MESH_EVENT_LOGS 1
#endif

#ifndef MESH_PACKET_TRACE
#define MESH_PACKET_TRACE 0
#endif

#ifndef EXPERIMENT_ACK_TIMEOUT_MS
#define EXPERIMENT_ACK_TIMEOUT_MS 3000
#endif

#ifndef EXPERIMENT_REPORT_INTERVAL_MS
#define EXPERIMENT_REPORT_INTERVAL_MS 30000
#endif

#ifndef MESH_DYNAMIC_SLEEP_CONTROL
#define MESH_DYNAMIC_SLEEP_CONTROL 0
#endif

#ifndef MESH_SLEEP_CONTROL_RETRY_INTERVAL_MS
#define MESH_SLEEP_CONTROL_RETRY_INTERVAL_MS 1200
#endif

#ifndef MESH_SLEEP_CONTROL_ACK_TIMEOUT_MS
#define MESH_SLEEP_CONTROL_ACK_TIMEOUT_MS 2500
#endif

#ifndef MESH_SLEEP_CONTROL_MAX_RETRIES
#define MESH_SLEEP_CONTROL_MAX_RETRIES 8
#endif

#ifndef MESH_SLEEP_CONTROL_REFRESH_MS
#define MESH_SLEEP_CONTROL_REFRESH_MS 60000
#endif

#ifndef MESH_SLEEP_CONTROL_TARGET_ID
#ifdef DESTINATION_ID
#define MESH_SLEEP_CONTROL_TARGET_ID DESTINATION_ID
#else
#define MESH_SLEEP_CONTROL_TARGET_ID 0
#endif
#endif

#ifndef MESH_SLEEP_CONTROL_BROADCAST
#define MESH_SLEEP_CONTROL_BROADCAST 0
#endif

#ifndef MESH_SLEEP_CONTROL_FORWARD_ENABLED
#define MESH_SLEEP_CONTROL_FORWARD_ENABLED 1
#endif

#ifndef MESH_SLEEP_CONTROL_FORWARD_HOPS
#define MESH_SLEEP_CONTROL_FORWARD_HOPS 4
#endif

#ifndef MESH_SLEEP_CONTROL_FORWARD_JITTER_MS
#define MESH_SLEEP_CONTROL_FORWARD_JITTER_MS 180
#endif

#ifndef MESH_SENSOR_NODE_ID
#define MESH_SENSOR_NODE_ID 1
#endif

#ifndef MESH_FOLLOWER_CAD_ENABLED
#define MESH_FOLLOWER_CAD_ENABLED 0
#endif

#ifndef MESH_CAD_SYMBOLS
#define MESH_CAD_SYMBOLS LORA_CAD_04_SYMBOL
#endif

#ifndef MESH_CAD_DET_PEAK
#define MESH_CAD_DET_PEAK (LORA_SPREADING_FACTOR + 13)
#endif

#ifndef MESH_CAD_DET_MIN
#define MESH_CAD_DET_MIN 10
#endif

#ifndef MESH_CAD_MAX_RETRIES
#define MESH_CAD_MAX_RETRIES 4
#endif

#ifndef MESH_CAD_BACKOFF_MAX_MS
#define MESH_CAD_BACKOFF_MAX_MS 120
#endif

#ifndef MESH_CAD_BACKOFF_EXP_MAX_SHIFT
#define MESH_CAD_BACKOFF_EXP_MAX_SHIFT 4
#endif

#ifndef MESH_FORWARD_JITTER_MS
#define MESH_FORWARD_JITTER_MS 120
#endif

#ifndef MESH_FORWARD_MIN_INTERVAL_MS
#define MESH_FORWARD_MIN_INTERVAL_MS 80
#endif

#ifndef MESH_FORWARD_MAX_QUEUED_PER_DEST
#define MESH_FORWARD_MAX_QUEUED_PER_DEST 3
#endif

#ifndef MESH_DUP_HISTORY_SIZE
#define MESH_DUP_HISTORY_SIZE 32
#endif
#if MESH_DUP_HISTORY_SIZE < 1
#undef MESH_DUP_HISTORY_SIZE
#define MESH_DUP_HISTORY_SIZE 1
#endif

#ifndef MESH_SLEEP_CONTROL_RETRY_MAX_INTERVAL_MS
#define MESH_SLEEP_CONTROL_RETRY_MAX_INTERVAL_MS 12000
#endif

#ifndef MESH_SLEEP_CONTROL_RETRY_JITTER_MS
#define MESH_SLEEP_CONTROL_RETRY_JITTER_MS 250
#endif

// --- Scalability: Neighbor Table ---
#ifndef MESH_MAX_NEIGHBORS
#define MESH_MAX_NEIGHBORS 8
#endif

// --- Scalability: RSSI-weighted forward jitter ---
// Nodes with weaker RSSI add extra delay before forwarding; better-path nodes
// forward sooner and dup suppression silences the weaker-path nodes automatically.
#ifndef MESH_RSSI_JITTER_ENABLED
#define MESH_RSSI_JITTER_ENABLED 1
#endif
#ifndef MESH_RSSI_GOOD_THRESHOLD
#define MESH_RSSI_GOOD_THRESHOLD -80   // dBm: at or above this, no penalty
#endif
#ifndef MESH_RSSI_JITTER_SCALE_MS
#define MESH_RSSI_JITTER_SCALE_MS 3    // ms of extra jitter per dB below threshold
#endif
#ifndef MESH_RSSI_JITTER_MAX_PENALTY_MS
#define MESH_RSSI_JITTER_MAX_PENALTY_MS 200  // cap so very weak nodes still forward eventually
#endif

// --- Scalability: Load-weighted forward jitter ---
// Nodes with more packets already queued or a high recent forwarding rate wait longer before
// forwarding. Combined with RSSI jitter this reduces hotspot buildup at heavily-used relays.
#ifndef MESH_LOAD_JITTER_ENABLED
#define MESH_LOAD_JITTER_ENABLED 0
#endif
#ifndef MESH_LOAD_JITTER_QUEUE_SCALE_MS
#define MESH_LOAD_JITTER_QUEUE_SCALE_MS 25   // ms of extra jitter per packet in TX queue
#endif
#ifndef MESH_LOAD_JITTER_RATE_WINDOW_MS
#define MESH_LOAD_JITTER_RATE_WINDOW_MS 5000  // rolling window length for forwarding rate (ms)
#endif
#ifndef MESH_LOAD_JITTER_RATE_SCALE_MS
#define MESH_LOAD_JITTER_RATE_SCALE_MS 15    // ms of extra jitter per forward in the window
#endif
#ifndef MESH_LOAD_JITTER_MAX_PENALTY_MS
#define MESH_LOAD_JITTER_MAX_PENALTY_MS 300  // cap so overloaded nodes still forward eventually
#endif

// --- Scalability: Sync orphan detection & rebuild ---
// If no SLEEP_CONTROL is received for this long, enter orphan mode and search for a new sync source.
#ifndef MESH_SYNC_ORPHAN_THRESHOLD_MS
#define MESH_SYNC_ORPHAN_THRESHOLD_MS 120000  // 2 minutes
#endif
// Extra wake window duration while in orphan mode (listen longer to catch a new sync source).
#ifndef MESH_SYNC_ORPHAN_WAKE_EXTENSION_MS
#define MESH_SYNC_ORPHAN_WAKE_EXTENSION_MS 500
#endif
// How often to send a broadcast RTR while in orphan mode requesting sync retransmit.
#ifndef MESH_SYNC_ORPHAN_RTR_INTERVAL_MS
#define MESH_SYNC_ORPHAN_RTR_INTERVAL_MS 5000
#endif

#if MESH_EVENT_LOGS
#define MESH_LOG_EVENT(...) Serial.printf(__VA_ARGS__)
#define MESH_LOG_EVENT_LN(msg) Serial.println(msg)
#else
#define MESH_LOG_EVENT(...) do {} while (0)
#define MESH_LOG_EVENT_LN(msg) do {} while (0)
#endif

#if MESH_PACKET_TRACE
#define MESH_LOG_PACKET(...) Serial.printf(__VA_ARGS__)
#define MESH_LOG_PACKET_LN(msg) Serial.println(msg)
#else
#define MESH_LOG_PACKET(...) do {} while (0)
#define MESH_LOG_PACKET_LN(msg) do {} while (0)
#endif

// --- Forward declarations for global callback functions ---
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnRxTimeout(void);
void OnRxError(void);
void OnCadDone(bool channelActivityDetected);

/**
 * @class MeshNode
 * @brief Manages the state and logic for a node in the LoRa mesh network.
 */
class MeshNode
{
public:
    /**
     * @brief Constructor for the MeshNode.
     * @param deviceId The unique ID of this node.
     * @param networkId The ID of the network this node belongs to.
     */
    MeshNode(uint16_t deviceId, uint16_t networkId)
        : _deviceId(deviceId),
          _networkId(networkId),
          _lastProcessedPacketID(0),
          _packetCounter(0),
          _lastPayloadLen(0),
          _txQueueHead(0),
          _txQueueTail(0),
          _txQueueCount(0),
          _txInProgress(false),
          _rxRearmPending(false),
          _radioAwake(false),
          _radioListening(false),
          _cycleStartMs(0),
          _activeWakeCycleMs((uint32_t)MESH_WAKE_CYCLE_MS),
          _activeWakeWindowMs((uint32_t)MESH_WAKE_WINDOW_MS),
          _activeCoordBufferMs((uint16_t)MESH_SYNC_COORD_BUFFER_MS),
          _sleepScheduleActive((MESH_WAKE_CYCLE_MS > 0) && (MESH_WAKE_WINDOW_MS > 0)),
          _pendingListenUntilMs(0),
          _startupListenUntilMs(0),
          _syncCoordHoldUntilMs(0),
          _syncLocked(true),
          _sleepControlWaitingAck(false),
          _sleepControlResendRequested(false),
          _sleepControlSeq(0),
          _sleepControlLastAckSeq(0),
          _sleepControlLastTxMs(0),
          _sleepControlLastAckMs(0),
          _sleepControlRetries(0),
          _sleepControlLastAppliedSeq(0),
          _sleepControlLastForwardedSeq(0),
          _sleepControlForwardPending(false),
          _sleepControlForwardAtMs(0),
          _sleepControlLastSentValid(false),
          _sleepControlLastSentSeq(0),
          _cadInProgress(false),
          _cadResultPending(false),
          _cadChannelBusy(false),
          _cadTxPending(false),
          _cadBackoffUntilMs(0),
          _cadRetryCount(0),
          _beaconPending(false),
          _experimentPending(false),
          _experimentInFlight(false),
          _experimentNextId(1),
          _neighborCount(0),
          _syncOrphan(false),
          _syncLastReceivedMs(0),
          _syncBestParentId(0),
          _syncBestParentRssi(-32768),
          _syncOrphanRtrLastMs(0) {
        memset(_lastPayload, 0, sizeof(_lastPayload));
        memset(_txQueueNotBeforeMs, 0, sizeof(_txQueueNotBeforeMs));
        memset(_recentPacketHistory, 0, sizeof(_recentPacketHistory));
        memset(_neighborTable, 0, sizeof(_neighborTable));
    }

    /**
     * @brief Initializes the LoRa radio and sets up callbacks.
     */
    void begin()
    {
        MESH_LOG_EVENT("INFO: Node %u starting on network %u.\n", _deviceId, _networkId);

        uint32_t initResult = lora_rak4630_init(); // Initialize RAK-specific hardware
        if (initResult != 0) {
            Serial.printf("ERROR: lora_rak4630_init failed (%lu)\n", (unsigned long)initResult);
        }

        // Setup radio events
        _radioEvents.TxDone = OnTxDone;
        _radioEvents.TxTimeout = OnTxTimeout;
        _radioEvents.RxDone = OnRxDone;
        _radioEvents.RxTimeout = OnRxTimeout;
        _radioEvents.RxError = OnRxError;
        _radioEvents.CadDone = OnCadDone;

        Radio.Init(&_radioEvents);
        // Explicitly start from low-power radio state before runtime scheduler drives RX/TX.
        Radio.Sleep();
        Radio.SetChannel(LORA_FREQUENCY_HZ);
        // Configuration for TX (global LoRa settings)
        Radio.SetTxConfig(MODEM_LORA, LORA_TX_POWER_DBM, 0, LORA_BANDWIDTH,
                          LORA_SPREADING_FACTOR, LORA_CODING_RATE,
                          LORA_LONG_PREAMBLE_SYMBOLS, LORA_FIX_LEN, LORA_CRC_ENABLED,
                          LORA_FREQ_HOP_ON, LORA_HOP_PERIOD, LORA_IQ_INVERTED_ENABLED,
                          LORA_TX_TIMEOUT_MS);
        // Configuration for RX
        Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                          LORA_CODING_RATE, 0, LORA_LONG_PREAMBLE_SYMBOLS,
                          LORA_SYMB_TIMEOUT, LORA_FIX_LEN, LORA_PAYLOAD_LEN,
                          LORA_CRC_ENABLED, LORA_FREQ_HOP_ON, LORA_HOP_PERIOD,
                          LORA_IQ_INVERTED_ENABLED, LORA_RX_CONTINUOUS);
#if MESH_FOLLOWER_CAD_ENABLED && !defined(SENSOR_NODE)
        Radio.SetCadParams((uint8_t)MESH_CAD_SYMBOLS,
                           (uint8_t)MESH_CAD_DET_PEAK,
                           (uint8_t)MESH_CAD_DET_MIN,
                           (uint8_t)LORA_CAD_ONLY,
                           0);
#endif

        _cycleStartMs = millis();
#if MESH_DYNAMIC_SLEEP_CONTROL && !MESH_SYNC_MASTER
        _sleepScheduleActive = false;
        _activeWakeCycleMs = 0;
        _activeWakeWindowMs = 0;
        _syncLocked = false;
        _startupListenUntilMs = 0;
        MESH_LOG_EVENT_LN("INFO: Awaiting sleep-control packet from sync master.");
#else
#if MESH_SYNC_BEACON_ENABLED && !MESH_SYNC_MASTER
        _syncLocked = false;
        _startupListenUntilMs = 0;
        if (wakeScheduleEnabled() && (MESH_STARTUP_SYNC_LISTEN_MS > 0)) {
            _startupListenUntilMs = millis() + (uint32_t)MESH_STARTUP_SYNC_LISTEN_MS;
            MESH_LOG_EVENT("INFO: Startup sync listen for %lu ms\n",
                           (unsigned long)MESH_STARTUP_SYNC_LISTEN_MS);
        }
#else
        _syncLocked = true;
        _startupListenUntilMs = 0;
#endif
#endif

#if MESH_DYNAMIC_SLEEP_CONTROL && MESH_SYNC_MASTER
        _sleepControlWaitingAck = false;
        _sleepControlResendRequested = false;
        _sleepControlSeq = 0;
        _sleepControlLastAckSeq = 0;
        _sleepControlLastTxMs = 0;
        _sleepControlLastAckMs = 0;
        _sleepControlRetries = 0;
#endif
        _sleepControlForwardPending = false;
        _sleepControlLastSentValid = false;
        _cadInProgress = false;
        _cadResultPending = false;
        _cadTxPending = false;
        // Start listening for packets
        startListening();
    }

    /**
     * @brief Starts the radio in continuous receive mode.
     * @param verbose Whether to print status messages (defaults to true). Use false when called from ISR.
     */
    void startListening(bool verbose = true) {
        if (verbose) {
            MESH_LOG_EVENT_LN("INFO: Node is now listening.");
        }
        _radioAwake = true;
        _radioListening = true;
        _rxRearmPending = false;
#if MESH_RX_DUTY_CYCLE_ENABLED
        Radio.SetRxDutyCycle(rxDutyTicksFromMs(MESH_RX_DUTY_CYCLE_RX_MS),
                             rxDutyTicksFromMs(MESH_RX_DUTY_CYCLE_SLEEP_MS));
#else
        Radio.Rx(0);
#endif
    }

    /**
     * @brief Sends a coordination packet (for routing/forwarding without sensor data).
     * @param destinationId The ID of the target node.
     */
    void sendCoordinationPacket(uint16_t destinationId) {
        MeshPacket packet;
        packet.originalSenderID = _deviceId;
        packet.senderID = _deviceId;
        packet.destinationID = destinationId;
        packet.networkID = _networkId;
        packet.packetID = _packetCounter++;
        packet.packetType = COORDINATION_PACKET;
        packet.payloadLen = 0;
        packet.updateChecksum();

        MESH_LOG_EVENT("INFO: Queueing COORDINATION Packet ID %u to Node %u\n", packet.packetID, destinationId);
        if (!enqueuePacket(packet)) {
            Serial.println("WARN: TX queue full; dropped coordination packet.");
        }
    }

    /**
     * @brief Sends a sensor packet with a compact sensor data payload.
     * @param destinationId The ID of the target node.
     * @param payload Sensor data buffer.
     * @param payloadLen Length of payload in bytes (<= MAX_PAYLOAD).
     */
    void sendSensorPacket(uint16_t destinationId, const uint8_t* payload, uint8_t payloadLen = MeshPacket::MAX_PAYLOAD) {
        if (!payload) return;

        MeshPacket packet;
        packet.originalSenderID = _deviceId;
        packet.senderID = _deviceId;
        packet.destinationID = destinationId;
        packet.networkID = _networkId;
        packet.packetID = _packetCounter++;
        packet.packetType = SENSOR_PACKET;
        packet.payloadLen = payloadLen > MeshPacket::MAX_PAYLOAD ? MeshPacket::MAX_PAYLOAD : payloadLen;
        if (packet.payloadLen > 0) {
            memcpy(packet.payload, payload, packet.payloadLen);
        }
        packet.updateChecksum();

        MESH_LOG_EVENT("INFO: Queueing SENSOR Packet ID %u to Node %u (%u bytes payload)\n",
                       packet.packetID, destinationId, packet.payloadLen);
        if (!enqueuePacket(packet)) {
            Serial.println("WARN: TX queue full; dropped sensor packet.");
        }
    }

    /**
     * @brief Queues an experiment packet to measure RTT and delivery.
     * @param destinationId The ID of the target node.
     * @return True if queued, false if already pending or queue full.
     */
    bool queueExperimentPacket(uint16_t destinationId) {
        if (_experimentPending || _experimentInFlight) {
            MESH_LOG_EVENT_LN("INFO: Experiment already pending/in-flight; skipping new experiment packet.");
            return false;
        }

        MeshPacket packet;
        packet.originalSenderID = _deviceId;
        packet.senderID = _deviceId;
        packet.destinationID = destinationId;
        packet.networkID = _networkId;
        packet.packetID = _packetCounter++;
        packet.packetType = EXPERIMENT_PACKET;

        uint16_t experimentId = _experimentNextId++;
        packet.payloadLen = sizeof(experimentId);
        memcpy(packet.payload, &experimentId, sizeof(experimentId));
        packet.updateChecksum();

        if (!enqueuePacket(packet)) {
            Serial.println("WARN: Experiment packet queue full; dropping.");
            return false;
        }

        _experimentPending = true;
        _experimentPendingId = experimentId;
        MESH_LOG_EVENT("INFO: Queued EXPERIMENT Packet ID %u (exp %u) to Node %u\n",
                       packet.packetID, experimentId, destinationId);
        return true;
    }

    /**
     * @brief Main runtime tick for wake scheduling, TX queue, and experiments.
     * Call this once per main loop iteration.
     */
    void run() {
        processReceivedQueue();
        handleExperimentTimeouts();
        processCadState();
        processSleepControlForward();
        checkSyncOrphan();

        updateWakeState();

        if (_radioAwake) {
            if (_rxRearmPending && !_txInProgress && !_cadInProgress) {
                startListening(false);
            }
            maybeSendSleepControl();
            maybeSendSyncBeacon();
            maybeRequestSyncRetransmit();
            processTxQueue();
        }

        maybeReportExperimentStats();
    }

    /**
     * @brief Handles a received packet.
     * @param payload The raw data from the radio.
     * @param size The size of the payload.
     * @param rssi The received signal strength indicator.
     * @param snr The signal-to-noise ratio.
     */
    void handleReceivedPacket(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
        _lastRxRssi = rssi;
        _lastRxSnr = snr;
#if MESH_PACKET_TRACE
        MESH_LOG_PACKET_LN("\n--- PACKET RECEIVED ---");
        MESH_LOG_PACKET("RSSI: %d dBm, SNR: %d, Size: %u bytes\n", rssi, snr, size);
#endif

        if (size < MeshPacket::MIN_PACKET_SIZE || size > MeshPacket::MAX_PACKET_SIZE) {
            Serial.printf("ERROR: Invalid packet size. Expected %u-%u, got %u.\n",
                          MeshPacket::MIN_PACKET_SIZE, MeshPacket::MAX_PACKET_SIZE, size);
            return; // Discard packet
        }

        MeshPacket packet;
        if (!packet.deserialize(payload, size)) {
            Serial.println("ERROR: Checksum INVALID. Packet discarded.");
            return;
        }
        _lastMeshRxMs = millis();
        updateNeighborTable(packet.senderID, rssi, snr);
#if MESH_PACKET_TRACE
        MESH_LOG_PACKET_LN("INFO: Checksum VALID.");
        MESH_LOG_PACKET("  - Original Sender: %u\n", packet.originalSenderID);
        MESH_LOG_PACKET("  - Sender: %u\n", packet.senderID);
        MESH_LOG_PACKET("  - Destination: %u (My ID: %u)\n", packet.destinationID, _deviceId);
        MESH_LOG_PACKET("  - Network: %u\n", packet.networkID);
        MESH_LOG_PACKET("  - Packet ID: %u\n", packet.packetID);
        MESH_LOG_PACKET("  - Packet Type: %s\n", packetTypeToString(packet.packetType));
#endif

        if (isRecentDuplicate(packet)) {
            _duplicateDropCount++;
            MESH_LOG_PACKET_LN("INFO: Duplicate packet signature. Ignoring.");
            return;
        }
        rememberPacketSignature(packet);

        if (packet.packetType == BEACON_PACKET) {
            handleBeaconPacket(packet);
            _lastProcessedPacketID = packet.packetID;
            return;
        }

        const bool isForMe = (packet.destinationID == _deviceId);
        const bool isBroadcast = (packet.destinationID == MeshPacket::BROADCAST_ID);

        if (!isForMe && !isBroadcast) {
            MESH_LOG_EVENT("INFO: Forwarding packet %u for node %u\n", packet.packetID, packet.destinationID);
            retransmitPacket(packet);
            _lastProcessedPacketID = packet.packetID;
            return;
        }

        if (packet.packetType == EXPERIMENT_PACKET) {
            if (isForMe) {
                handleExperimentPacket(packet);
            }
            _lastProcessedPacketID = packet.packetID;
            return;
        }

        if (packet.packetType == EXPERIMENT_ACK_PACKET) {
            if (isForMe) {
                handleExperimentAck(packet);
            } else if (!isBroadcast) {
                retransmitPacket(packet);
            }
            _lastProcessedPacketID = packet.packetID;
            return;
        }

        if (packet.packetType == SLEEP_CONTROL_PACKET) {
            if (isForMe || isBroadcast) {
                handleSleepControlPacket(packet);
            } else {
                retransmitPacket(packet);
            }
            _lastProcessedPacketID = packet.packetID;
            return;
        }

        if (packet.packetType == SLEEP_CONTROL_ACK_PACKET) {
            if (isForMe) {
                handleSleepControlAck(packet);
            } else if (!isBroadcast) {
                retransmitPacket(packet);
            }
            _lastProcessedPacketID = packet.packetID;
            return;
        }

        if (packet.packetType == SLEEP_CONTROL_RTR_PACKET) {
            if (isForMe || isBroadcast) {
                // Handle broadcast RTR: allows orphan nodes to solicit sync from any relay
                handleSleepControlRtr(packet);
            } else {
                retransmitPacket(packet);
            }
            _lastProcessedPacketID = packet.packetID;
            return;
        }

        MESH_LOG_PACKET_LN("INFO: Packet is for this node. Processing...");
        // Store payload for external access
        _lastPayloadLen = packet.payloadLen;
        if (packet.payloadLen > 0 && packet.payloadLen <= MeshPacket::MAX_PAYLOAD) {
            memcpy(_lastPayload, packet.payload, packet.payloadLen);
        }

        // Save original sender ID so UI code and BLE sender can use it
        _lastOriginalSenderID = packet.originalSenderID;
        _lastProcessedPacketID = packet.packetID;
    }

    /**
     * @brief Gets the device ID of the node.
     * @return The device ID.
     */
    uint16_t getDeviceId() const { return _deviceId; }

    /**
     * @brief Gets the network ID of the node.
     * @return The network ID.
     */
    uint16_t getNetworkId() const { return _networkId; }

    /**
     * @brief Gets the last received packet's payload for external processing.
     * @return Pointer to payload buffer.
     */
    const uint8_t* getLastPayload() const { return _lastPayload; }

    /**
     * @brief Gets the last received packet's payload length.
     * @return Length of payload.
     */
    uint8_t getLastPayloadLen() const { return _lastPayloadLen; }

    /**
     * @brief Clears the last received payload.
     */
    void clearLastPayload() { _lastPayloadLen = 0; memset(_lastPayload, 0, sizeof(_lastPayload)); }

    /**
     * @brief Gets the ID of the last successfully processed packet.
     * @return The last packet ID.
     */
    uint16_t getLastProcessedPacketID() const { return _lastProcessedPacketID; }

    // Returns the original sender ID of the last packet processed and delivered to user space
    uint16_t getLastOriginalSenderID() const { return _lastOriginalSenderID; }
    uint8_t getTxQueueDepth() const { return _txQueueCount; }
    bool hasRecentMeshRx(uint32_t maxAgeMs) const {
        if (_lastMeshRxMs == 0) {
            return false;
        }
        return (uint32_t)(millis() - _lastMeshRxMs) <= maxAgeMs;
    }
    uint32_t getDuplicateDropCount() const { return _duplicateDropCount; }
    uint32_t getForwardQueuedCount() const { return _forwardQueuedCount; }
    uint32_t getForwardDropCount() const { return _forwardDropCount; }
    uint32_t getCadBusyCount() const { return _cadBusyCount; }
    uint32_t getSleepControlRetryCount() const { return _sleepControlRetryTxCount; }
    int16_t getLastRxRssi() const { return _lastRxRssi; }
    int8_t  getLastRxSnr()  const { return _lastRxSnr; }
    uint8_t getNeighborCount() const { return _neighborCount; }
    bool    getSyncOrphan()    const { return _syncOrphan; }
    uint16_t getSyncBestParentId() const { return _syncBestParentId; }
    // Forwards enqueued within the current load-jitter rolling window
    uint16_t getForwardWindowCount() const { return _forwardWindowCount; }
    // 0-255 load score: combines queue depth and recent forwarding rate into a single number
    uint8_t getLoadScore() const {
        const uint32_t s = (uint32_t)_txQueueCount * 30U + (uint32_t)_forwardWindowCount * 5U;
        return (uint8_t)(s > 255U ? 255U : s);
    }

    /**
     * @brief Suggested idle sleep duration for MCU power saving.
     * @return Milliseconds the main loop can idle-sleep without missing scheduler work.
     */
    uint32_t getIdleSleepBudgetMs() const {
        if (_txInProgress || _rxRearmPending || _cadInProgress || _cadTxPending) {
            return 0;
        }

        if (sleepControlForcesAwake()) {
            return 0;
        }

#if MESH_TX_ONLY_IN_WAKE_WINDOW
        if (_txQueueCount > 0) {
            if (!wakeScheduleEnabled()) {
                return 0;
            }

            const uint32_t now = millis();
            const uint32_t cycleMs = _activeWakeCycleMs;
            uint32_t elapsed = now - _cycleStartMs;
            if (elapsed >= cycleMs) {
                elapsed %= cycleMs;
            }

            if (elapsed < effectiveWakeWindowMs()) {
                return 0;
            }
            return cycleMs - elapsed;
        }
#else
        if (_txQueueCount > 0) {
            return 0;
        }
#endif

        // If radio is awake/listening, keep loop latency low for packet processing.
        if (_radioAwake) {
            return 2;
        }

#ifdef TEST_MODE_AWAKE
        return 0;
#else
        const bool scheduleEnabled = wakeScheduleEnabled();
        if (!scheduleEnabled) {
            return 2;
        }

        const uint32_t now = millis();
        uint32_t elapsed = now - _cycleStartMs;
        const uint32_t cycleMs = _activeWakeCycleMs;
        if (elapsed >= cycleMs) {
            elapsed %= cycleMs;
        }

        const uint32_t wakeWindow = effectiveWakeWindowMs();
        if (elapsed < wakeWindow) {
            return 2;
        }

        return cycleMs - elapsed;
#endif
    }

    /**
     * @brief Legacy hook: processes the TX queue (kept for compatibility).
     */
    void processRetransmissions() {
        processTxQueue();
    }

    /**
     * @brief Queues a packet to be processed in the main loop (ISR-safe)
     * @param payload Raw packet bytes (variable length expected)
     * @param size Size of payload
     * @param rssi Received RSSI
     * @param snr Received SNR
     */
    void queueReceivedPacket(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
        if (size < MeshPacket::MIN_PACKET_SIZE || size > MeshPacket::MAX_PACKET_SIZE) return;

        noInterrupts();
        uint8_t head = _rxQueueHead;
        uint8_t tail = _rxQueueTail;
        uint8_t nextTail = (tail + 1) % RX_QUEUE_SIZE;
        if (nextTail == head) {
            _rxQueueDropCount++;
            interrupts();
            return;
        }

        memcpy(_rxQueue[tail], payload, size);
        _rxQueueSize[tail] = size;
        _rxQueueRssi[tail] = rssi;
        _rxQueueSnr[tail] = snr;
        _rxQueueTail = nextTail;
        interrupts();
    }

    /**
     * @brief Process queued received packets (call from main loop)
     */
    void processReceivedQueue() {
        // Drain the queue
        while (true) {
            noInterrupts();
            uint8_t head = _rxQueueHead;
            uint8_t tail = _rxQueueTail;
            if (head == tail) {
                interrupts();
                break;
            }
            // copy out queue entry
            uint8_t buffer[MeshPacket::MAX_PACKET_SIZE];
            uint16_t size = _rxQueueSize[head];
            memcpy(buffer, _rxQueue[head], size);
            int16_t rssi = _rxQueueRssi[head];
            int8_t snr = _rxQueueSnr[head];
            _rxQueueHead = (head + 1) % RX_QUEUE_SIZE;
            interrupts();

            // Process normally in main context (safe to use Serial and block)
            handleReceivedPacket(buffer, size, rssi, snr);
        }

        // Report queue drops with throttling to avoid console spam.
        uint16_t dropCount = 0;
        noInterrupts();
        dropCount = _rxQueueDropCount;
        interrupts();
        if (dropCount > _lastReportedRxQueueDropCount) {
            const uint32_t now = millis();
            if ((uint32_t)(now - _lastRxDropLogMs) >= RX_DROP_LOG_INTERVAL_MS) {
                Serial.printf("WARN: RX queue dropped %u packets so far\n", dropCount);
                _lastReportedRxQueueDropCount = dropCount;
                _lastRxDropLogMs = now;
            }
        }
    }

    // ISR-facing hooks for radio callbacks (keep lightweight)
    void onTxDoneISR() {
        _txInProgress = false;
        _radioListening = false;
        _rxRearmPending = true;
    }

    void onTxTimeoutISR() {
        _txInProgress = false;
        _radioListening = false;
        _rxRearmPending = true;
    }

    void onRxDoneISR() {
        _radioListening = false;
        _rxRearmPending = true;
    }

    void onRxTimeoutISR() {
        _radioListening = false;
        _rxRearmPending = true;
    }

    void onRxErrorISR() {
        _radioListening = false;
        _rxRearmPending = true;
    }

    void onCadDoneISR(bool channelActivityDetected) {
        _cadInProgress = false;
        _cadChannelBusy = channelActivityDetected;
        _cadResultPending = true;
        _radioListening = false;
        _rxRearmPending = false;
    }

private:
    uint16_t _deviceId;
    uint16_t _networkId;
    uint16_t _lastProcessedPacketID;
    uint32_t _packetCounter;
    RadioEvents_t _radioEvents;
    
    // Store last received payload for external processing
    uint8_t _lastPayload[21];
    uint8_t _lastPayloadLen;
    uint16_t _lastOriginalSenderID = 0; // The origin of the last processed packet (original sender ID)
    uint32_t _lastMeshRxMs = 0;
    int16_t  _lastRxRssi = 0;
    int8_t   _lastRxSnr  = 0;

    // TX queue and radio state
    MeshPacket _txQueue[MESH_TX_QUEUE_SIZE];
    uint32_t _txQueueNotBeforeMs[MESH_TX_QUEUE_SIZE];
    uint8_t _txQueueHead;
    uint8_t _txQueueTail;
    uint8_t _txQueueCount;
    uint16_t _txQueueDropCount = 0;
    uint32_t _nextForwardTxAllowedMs = 0;
    volatile bool _txInProgress;
    volatile bool _rxRearmPending;
    bool _radioAwake;
    bool _radioListening;

    // Wake scheduling and pending-listen
    uint32_t _cycleStartMs;
    uint32_t _activeWakeCycleMs;
    uint32_t _activeWakeWindowMs;
    uint16_t _activeCoordBufferMs;
    bool _sleepScheduleActive;
    uint32_t _pendingListenUntilMs;
    uint32_t _startupListenUntilMs;
    uint32_t _syncCoordHoldUntilMs;
    bool _syncLocked;
    bool _sleepControlWaitingAck;
    bool _sleepControlResendRequested;
    uint16_t _sleepControlSeq;
    uint16_t _sleepControlLastAckSeq;
    uint32_t _sleepControlLastTxMs;
    uint32_t _sleepControlLastAckMs;
    uint8_t _sleepControlRetries;
    uint16_t _sleepControlLastAppliedSeq;
    uint16_t _sleepControlLastForwardedSeq;
    bool _sleepControlForwardPending;
    uint32_t _sleepControlForwardAtMs;
    MeshPacket _sleepControlForwardPacket;
    bool _sleepControlLastSentValid;
    uint16_t _sleepControlLastSentSeq;
    MeshPacket _sleepControlLastSentPacket;
    volatile bool _cadInProgress;
    volatile bool _cadResultPending;
    volatile bool _cadChannelBusy;
    bool _cadTxPending;
    uint32_t _cadBackoffUntilMs;
    uint8_t _cadRetryCount;
    MeshPacket _cadPacket;
    bool _beaconPending;
    uint32_t _lastSyncBeaconMs = 0;
    uint32_t _cadBusyCount = 0;
    uint32_t _sleepControlRetryTxCount = 0;
    uint32_t _duplicateDropCount = 0;
    uint32_t _forwardQueuedCount = 0;
    uint32_t _forwardDropCount = 0;
    uint32_t _forwardWindowStartMs = 0;   // start of current forwarding rate window
    uint16_t _forwardWindowCount   = 0;   // forwards enqueued in the current window

    // Experiment tracking
    bool _experimentPending;
    uint16_t _experimentPendingId = 0;
    bool _experimentInFlight;
    uint16_t _experimentLastId = 0;
    uint32_t _experimentLastSentMs = 0;
    uint16_t _experimentNextId;
    struct ExperimentStats {
        uint32_t sent = 0;
        uint32_t acked = 0;
        uint32_t timeouts = 0;
        uint32_t lastRttMs = 0;
        uint32_t minRttMs = UINT32_MAX;
        uint32_t maxRttMs = 0;
        uint64_t sumRttMs = 0;
        uint32_t lastReportMs = 0;
    } _experimentStats;

    // Receive queue for packets arriving in ISR
    static const uint8_t RX_QUEUE_SIZE = 16; // store a small burst (increased to reduce drops)
    volatile uint8_t _rxQueueHead = 0;
    volatile uint8_t _rxQueueTail = 0;
    uint8_t _rxQueue[RX_QUEUE_SIZE][MeshPacket::MAX_PACKET_SIZE];
    uint16_t _rxQueueSize[RX_QUEUE_SIZE] = {0};
    int16_t _rxQueueRssi[RX_QUEUE_SIZE];
    int8_t _rxQueueSnr[RX_QUEUE_SIZE];
    volatile uint16_t _rxQueueDropCount = 0; // packets dropped due to queue full
    uint16_t _lastReportedRxQueueDropCount = 0;
    uint32_t _lastRxDropLogMs = 0;
    static const uint32_t RX_DROP_LOG_INTERVAL_MS = 5000;
    static const uint8_t BEACON_SYNC_FLAG = 0x01;
    static const uint8_t BEACON_BASE_PAYLOAD_LEN = 1 + sizeof(uint16_t);
    static const uint8_t BEACON_SYNC_PAYLOAD_LEN =
        BEACON_BASE_PAYLOAD_LEN + 1 + sizeof(uint32_t) + sizeof(uint32_t);
    static const uint8_t SLEEP_CTRL_VERSION = 2;
    static const uint8_t SLEEP_CTRL_PAYLOAD_LEN =
        1 + 1 + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t);
    static const uint8_t SLEEP_CTRL_ACK_PAYLOAD_LEN = sizeof(uint16_t);
    static const uint8_t SLEEP_CTRL_RTR_PAYLOAD_LEN = sizeof(uint16_t) + sizeof(uint8_t);
    static const uint8_t SLEEP_CTRL_FLAG_FORWARD = 0x01;
    static const uint8_t SLEEP_CTRL_FLAG_ACK_REQUIRED = 0x02;
    static const uint8_t FORWARD_QUEUE_LIMIT_DISABLED = 0;

    struct PacketHistoryEntry {
        uint16_t originalSenderID = 0;
        uint16_t packetID = 0;
        uint8_t packetType = 0;
    };

    PacketHistoryEntry _recentPacketHistory[MESH_DUP_HISTORY_SIZE];
    uint8_t _recentPacketHistoryHead = 0;
    uint8_t _recentPacketHistoryCount = 0;

    // --- Neighbor Table (Scalability) ---
    // Tracks the RSSI/SNR of every unique sender heard directly over the radio.
    // Used to weight forward jitter (better link = forward sooner) and to log topology.
    struct NeighborEntry {
        uint16_t nodeId;
        int16_t  rssi;
        int8_t   snr;
        uint32_t lastHeardMs;
        bool     valid;
    };
    NeighborEntry _neighborTable[MESH_MAX_NEIGHBORS];
    uint8_t _neighborCount;

    // --- Sync Orphan State (Scalability) ---
    // If SLEEP_CONTROL is not received for MESH_SYNC_ORPHAN_THRESHOLD_MS, the node enters
    // orphan mode: it extends its wake window and broadcasts RTR to any relay in range.
    bool     _syncOrphan;
    uint32_t _syncLastReceivedMs;     // millis() when last valid SLEEP_CONTROL was received
    uint16_t _syncBestParentId;       // nodeId of the relay with the best RSSI for sync
    int16_t  _syncBestParentRssi;     // RSSI of the best sync parent seen
    uint32_t _syncOrphanRtrLastMs;    // millis() of last orphan RTR transmission

    // --- Scalability: Neighbor table update ---
    void updateNeighborTable(uint16_t nodeId, int16_t rssi, int8_t snr) {
        if (nodeId == _deviceId || nodeId == 0) return;  // Skip self and invalid

        const uint32_t now = millis();

        // Update existing entry if present
        for (uint8_t i = 0; i < _neighborCount; ++i) {
            if (_neighborTable[i].nodeId == nodeId) {
                _neighborTable[i].rssi = rssi;
                _neighborTable[i].snr  = snr;
                _neighborTable[i].lastHeardMs = now;
                return;
            }
        }

        // Add new entry if there's room
        if (_neighborCount < (uint8_t)MESH_MAX_NEIGHBORS) {
            _neighborTable[_neighborCount].nodeId      = nodeId;
            _neighborTable[_neighborCount].rssi        = rssi;
            _neighborTable[_neighborCount].snr         = snr;
            _neighborTable[_neighborCount].lastHeardMs = now;
            _neighborTable[_neighborCount].valid       = true;
            _neighborCount++;
            MESH_LOG_EVENT("INFO: New neighbor Node %u (RSSI=%d dBm SNR=%d)\n", nodeId, rssi, snr);
            return;
        }

        // Table full: evict oldest entry
        uint8_t oldestIdx = 0;
        uint32_t oldestAge = (uint32_t)(now - _neighborTable[0].lastHeardMs);
        for (uint8_t i = 1; i < (uint8_t)MESH_MAX_NEIGHBORS; ++i) {
            const uint32_t age = (uint32_t)(now - _neighborTable[i].lastHeardMs);
            if (age > oldestAge) {
                oldestAge = age;
                oldestIdx = i;
            }
        }
        MESH_LOG_EVENT("INFO: Neighbor table full; replacing Node %u with Node %u\n",
                       _neighborTable[oldestIdx].nodeId, nodeId);
        _neighborTable[oldestIdx].nodeId      = nodeId;
        _neighborTable[oldestIdx].rssi        = rssi;
        _neighborTable[oldestIdx].snr         = snr;
        _neighborTable[oldestIdx].lastHeardMs = now;
    }

    // --- Scalability: Orphan detection ---
    // Called every run() tick. If no SLEEP_CONTROL has arrived in MESH_SYNC_ORPHAN_THRESHOLD_MS,
    // declare the node an orphan so it searches for a new sync parent.
    void checkSyncOrphan() {
#if defined(MESH_SYNC_ORPHAN_THRESHOLD_MS) && (MESH_SYNC_ORPHAN_THRESHOLD_MS > 0) && \
    MESH_DYNAMIC_SLEEP_CONTROL && !MESH_SYNC_MASTER
        if (_syncOrphan) return;           // Already in orphan mode
        if (_syncLastReceivedMs == 0) return; // Never synced yet; wait for first sync
        const uint32_t now = millis();
        if ((uint32_t)(now - _syncLastReceivedMs) > (uint32_t)MESH_SYNC_ORPHAN_THRESHOLD_MS) {
            _syncOrphan = true;
            _syncOrphanRtrLastMs = 0;  // Allow an immediate RTR on next run()
            Serial.println("[MESH] Sync orphan detected -- searching for new sync parent");
        }
#endif
    }

    // --- Scalability: Orphan RTR broadcast ---
    // While orphaned, periodically send a broadcast RTR (sequence=0) so that any relay or
    // the sync master that hears it will retransmit the last cached SLEEP_CONTROL packet.
    void maybeRequestSyncRetransmit() {
#if defined(MESH_SYNC_ORPHAN_THRESHOLD_MS) && (MESH_SYNC_ORPHAN_THRESHOLD_MS > 0) && \
    MESH_DYNAMIC_SLEEP_CONTROL && !MESH_SYNC_MASTER
        if (!_syncOrphan) return;
        if (_txInProgress || _cadInProgress || _cadTxPending) return;
        const uint32_t now = millis();
        if (_syncOrphanRtrLastMs != 0 &&
            (uint32_t)(now - _syncOrphanRtrLastMs) < (uint32_t)MESH_SYNC_ORPHAN_RTR_INTERVAL_MS) {
            return;
        }
        _syncOrphanRtrLastMs = now;
        // Send RTR with sequence=0 to broadcast; any relay with _sleepControlLastSentValid
        // will respond by re-queuing the cached control packet.
        sendSleepControlRtr(MeshPacket::BROADCAST_ID, 0, 0);
        MESH_LOG_EVENT_LN("[MESH] Orphan: broadcast RTR sent, awaiting sync retransmit");
#endif
    }

    static uint32_t rxDutyTicksFromMs(uint32_t ms) {
        // SX126x RX duty cycle uses 15.625us steps (64 ticks per millisecond).
        uint64_t ticks = (uint64_t)ms * 64ULL;
        if (ticks > 0xFFFFFFULL) {
            ticks = 0xFFFFFFULL;
        }
        if (ticks == 0) {
            ticks = 1;
        }
        return (uint32_t)ticks;
    }

    bool isControlPacketType(PacketType packetType) const {
        return (packetType == BEACON_PACKET) ||
               (packetType == EXPERIMENT_ACK_PACKET) ||
               (packetType == SLEEP_CONTROL_PACKET) ||
               (packetType == SLEEP_CONTROL_ACK_PACKET) ||
               (packetType == SLEEP_CONTROL_RTR_PACKET);
    }

    bool isForwardedPacket(const MeshPacket& packet) const {
        return packet.originalSenderID != _deviceId;
    }

    bool needsForwardQueueDiscipline(const MeshPacket& packet) const {
        return isForwardedPacket(packet) && !isControlPacketType(packet.packetType);
    }

    uint32_t computeForwardJitterMs(const MeshPacket& packet, uint32_t now) const {
        const uint32_t maxJitter = (uint32_t)MESH_FORWARD_JITTER_MS;
        if (maxJitter == 0 || !needsForwardQueueDiscipline(packet)) {
            return 0;
        }

        // Base: pseudo-random jitter derived from node/packet IDs to spread simultaneous forwards
        uint32_t jitter = (uint32_t)(((_deviceId * 29U) +
                                      (packet.originalSenderID * 17U) +
                                      (packet.packetID * 13U) +
                                      (now & 0xFFU)) % (maxJitter + 1U));

        // RSSI penalty: nodes with weaker signal to the sender wait longer before forwarding.
        // Nodes with better signal forward sooner; dup suppression then silences the late ones.
        // This creates implicit gradient routing without a routing table.
#if defined(MESH_RSSI_JITTER_ENABLED) && MESH_RSSI_JITTER_ENABLED
        const int16_t threshold = (int16_t)MESH_RSSI_GOOD_THRESHOLD;
        if (_lastRxRssi < threshold) {
            const int16_t delta = threshold - _lastRxRssi;  // dB below threshold, always > 0
            uint32_t penalty = (uint32_t)((int32_t)delta * (int32_t)MESH_RSSI_JITTER_SCALE_MS);
            if (penalty > (uint32_t)MESH_RSSI_JITTER_MAX_PENALTY_MS) {
                penalty = (uint32_t)MESH_RSSI_JITTER_MAX_PENALTY_MS;
            }
            jitter += penalty;
        }
#endif
        // Load penalty: nodes with more queued packets or a high recent forwarding rate wait
        // longer, giving less-loaded neighbors a chance to forward first.
#if defined(MESH_LOAD_JITTER_ENABLED) && MESH_LOAD_JITTER_ENABLED
        {
            uint32_t loadPenalty = (uint32_t)_txQueueCount * (uint32_t)MESH_LOAD_JITTER_QUEUE_SCALE_MS;
            // Treat the window count as 0 if the window has already expired; avoids applying
            // stale load from a burst that ended several seconds ago.
            const uint16_t windowCount = (now - _forwardWindowStartMs < (uint32_t)MESH_LOAD_JITTER_RATE_WINDOW_MS)
                                         ? _forwardWindowCount : 0U;
            loadPenalty += (uint32_t)windowCount * (uint32_t)MESH_LOAD_JITTER_RATE_SCALE_MS;
            if (loadPenalty > (uint32_t)MESH_LOAD_JITTER_MAX_PENALTY_MS) {
                loadPenalty = (uint32_t)MESH_LOAD_JITTER_MAX_PENALTY_MS;
            }
            jitter += loadPenalty;
        }
#endif
        return jitter;
    }

    uint32_t computeInitialTxNotBeforeMs(const MeshPacket& packet, uint32_t now) const {
        return now + computeForwardJitterMs(packet, now);
    }

    bool isRecentDuplicate(const MeshPacket& packet) const {
        for (uint8_t i = 0; i < _recentPacketHistoryCount; ++i) {
            const PacketHistoryEntry& entry = _recentPacketHistory[i];
            if ((entry.originalSenderID == packet.originalSenderID) &&
                (entry.packetID == packet.packetID) &&
                (entry.packetType == (uint8_t)packet.packetType)) {
                return true;
            }
        }
        return false;
    }

    void rememberPacketSignature(const MeshPacket& packet) {
        if (MESH_DUP_HISTORY_SIZE == 0) {
            return;
        }

        _recentPacketHistory[_recentPacketHistoryHead].originalSenderID = packet.originalSenderID;
        _recentPacketHistory[_recentPacketHistoryHead].packetID = packet.packetID;
        _recentPacketHistory[_recentPacketHistoryHead].packetType = (uint8_t)packet.packetType;
        _recentPacketHistoryHead = (uint8_t)((_recentPacketHistoryHead + 1U) % (uint8_t)MESH_DUP_HISTORY_SIZE);
        if (_recentPacketHistoryCount < (uint8_t)MESH_DUP_HISTORY_SIZE) {
            _recentPacketHistoryCount++;
        }
    }

    uint32_t sleepControlRetryIntervalMs() const {
        uint32_t intervalMs = (uint32_t)MESH_SLEEP_CONTROL_RETRY_INTERVAL_MS;
        if (_sleepControlRetries > 1) {
            uint8_t backoffSteps = (uint8_t)(_sleepControlRetries - 1);
            for (uint8_t i = 0; i < backoffSteps; ++i) {
                if (intervalMs >= (uint32_t)MESH_SLEEP_CONTROL_RETRY_MAX_INTERVAL_MS) {
                    intervalMs = (uint32_t)MESH_SLEEP_CONTROL_RETRY_MAX_INTERVAL_MS;
                    break;
                }
                const uint32_t doubled = intervalMs << 1;
                if (doubled < intervalMs || doubled > (uint32_t)MESH_SLEEP_CONTROL_RETRY_MAX_INTERVAL_MS) {
                    intervalMs = (uint32_t)MESH_SLEEP_CONTROL_RETRY_MAX_INTERVAL_MS;
                    break;
                }
                intervalMs = doubled;
            }
        }

        const uint32_t jitterMax = (uint32_t)MESH_SLEEP_CONTROL_RETRY_JITTER_MS;
        if (jitterMax > 0) {
            const uint32_t jitter = (uint32_t)(((_deviceId * 31U) +
                                                (_sleepControlSeq * 11U) +
                                                (_sleepControlRetries * 7U) +
                                                (millis() & 0xFFU)) % (jitterMax + 1U));
            intervalMs += jitter;
        }

        return intervalMs;
    }

    /**
     * @brief Queue a packet for transmission.
     */
    bool enqueuePacket(const MeshPacket& packet) {
        if (needsForwardQueueDiscipline(packet) &&
            ((uint8_t)MESH_FORWARD_MAX_QUEUED_PER_DEST != FORWARD_QUEUE_LIMIT_DISABLED) &&
            (countQueuedForDest(packet.destinationID) >= (uint8_t)MESH_FORWARD_MAX_QUEUED_PER_DEST)) {
            _txQueueDropCount++;
            _forwardDropCount++;
            return false;
        }

        if (_txQueueCount >= MESH_TX_QUEUE_SIZE) {
            _txQueueDropCount++;
            if (needsForwardQueueDiscipline(packet)) {
                _forwardDropCount++;
            }
            return false;
        }

        const uint32_t now = millis();
        const bool wasEmpty = (_txQueueCount == 0);
        const uint8_t slot = _txQueueTail;
        _txQueue[slot] = packet;
        _txQueueNotBeforeMs[slot] = computeInitialTxNotBeforeMs(packet, now);
        _txQueueTail = (_txQueueTail + 1) % MESH_TX_QUEUE_SIZE;
        _txQueueCount++;
        if (needsForwardQueueDiscipline(packet)) {
            _forwardQueuedCount++;
#if defined(MESH_LOAD_JITTER_ENABLED) && MESH_LOAD_JITTER_ENABLED
            if (now - _forwardWindowStartMs >= (uint32_t)MESH_LOAD_JITTER_RATE_WINDOW_MS) {
                _forwardWindowStartMs = now;
                _forwardWindowCount   = 0;
            }
            _forwardWindowCount++;
#endif
        }

#if MESH_PENDING_BEACON_ENABLED
        if (wasEmpty) {
            _beaconPending = true;
        }
#else
        (void)wasEmpty;
#endif
        return true;
    }

    /**
     * @brief Dequeue a packet for transmission.
     */
    bool dequeuePacket(MeshPacket& packet) {
        if (_txQueueCount == 0) {
            return false;
        }
        const uint8_t slot = _txQueueHead;
        packet = _txQueue[slot];
        _txQueueNotBeforeMs[slot] = 0;
        _txQueueHead = (_txQueueHead + 1) % MESH_TX_QUEUE_SIZE;
        _txQueueCount--;
        if (_txQueueCount == 0) {
            _beaconPending = false;
        }
        return true;
    }

    /**
     * @brief Process pending TX (beacon first, then queued data).
     */
    void processTxQueue() {
        if (_txInProgress || _cadInProgress || _cadTxPending) return;

        if (_txQueueCount == 0) {
            _beaconPending = false;
            return;
        }

        const uint32_t now = millis();
        const uint8_t headSlot = _txQueueHead;
        const MeshPacket& headPacket = _txQueue[headSlot];

        if ((int32_t)(now - _txQueueNotBeforeMs[headSlot]) < 0) {
            return;
        }

        if (needsForwardQueueDiscipline(headPacket) &&
            (int32_t)(now - _nextForwardTxAllowedMs) < 0) {
            return;
        }

#if MESH_TX_ONLY_IN_WAKE_WINDOW
        if (wakeScheduleEnabled() && !isInWakeWindow(now)) {
            return;
        }
#endif

#if MESH_PENDING_BEACON_ENABLED
        if (_beaconPending) {
            if (isForwardedPacket(headPacket)) {
                _beaconPending = false;
            } else {
                sendPendingBeacon();
                _beaconPending = false;
                return;
            }
        }
#endif

        MeshPacket packet;
        if (dequeuePacket(packet)) {
            if (shouldCadGateTx(packet)) {
                startCadForPacket(packet);
            } else {
                sendPacketNow(packet);
            }
        }
    }

    bool shouldCadGateTx(const MeshPacket& packet) const {
#if MESH_FOLLOWER_CAD_ENABLED && !defined(SENSOR_NODE)
        const bool toSensor = (packet.destinationID == (uint16_t)MESH_SENSOR_NODE_ID);
        const bool controlFlow =
            (packet.packetType == SLEEP_CONTROL_ACK_PACKET) ||
            (packet.packetType == SLEEP_CONTROL_RTR_PACKET);
        return toSensor || controlFlow;
#else
        (void)packet;
        return false;
#endif
    }

    void startCadForPacket(const MeshPacket& packet) {
#if MESH_FOLLOWER_CAD_ENABLED && !defined(SENSOR_NODE)
        _cadPacket = packet;
        _cadTxPending = true;
        _cadRetryCount = 0;
        _cadBackoffUntilMs = millis();
        startCadAttempt();
#else
        sendPacketNow(packet);
#endif
    }

    void startCadAttempt() {
#if MESH_FOLLOWER_CAD_ENABLED && !defined(SENSOR_NODE)
        if (!_cadTxPending || _cadInProgress || _txInProgress) {
            return;
        }

        if (!_radioAwake) {
            wakeRadio();
        }

        _cadInProgress = true;
        _cadResultPending = false;
        _radioListening = false;
        _rxRearmPending = false;
        Radio.Standby();
        Radio.SetCadParams((uint8_t)MESH_CAD_SYMBOLS,
                           (uint8_t)MESH_CAD_DET_PEAK,
                           (uint8_t)MESH_CAD_DET_MIN,
                           (uint8_t)LORA_CAD_ONLY,
                           0);
        Radio.StartCad();
#endif
    }

    void processCadState() {
#if MESH_FOLLOWER_CAD_ENABLED && !defined(SENSOR_NODE)
        if (_cadTxPending && !_cadInProgress && !_txInProgress) {
            const uint32_t now = millis();
            if ((int32_t)(now - _cadBackoffUntilMs) >= 0) {
                startCadAttempt();
            }
        }

        if (!_cadResultPending) {
            return;
        }

        _cadResultPending = false;
        if (!_cadTxPending) {
            return;
        }

        if (!_cadChannelBusy) {
            MeshPacket pendingPacket = _cadPacket;
            _cadTxPending = false;
            _cadRetryCount = 0;
            sendPacketNow(pendingPacket);
            return;
        }

        if (_cadRetryCount >= (uint8_t)MESH_CAD_MAX_RETRIES) {
            MeshPacket pendingPacket = _cadPacket;
            _cadTxPending = false;
            _cadRetryCount = 0;
            MESH_LOG_EVENT_LN("WARN: CAD channel busy too long; sending packet without additional CAD.");
            sendPacketNow(pendingPacket);
            return;
        }

        _cadBusyCount++;
        _cadRetryCount++;
        const uint8_t expShift = (_cadRetryCount > (uint8_t)MESH_CAD_BACKOFF_EXP_MAX_SHIFT)
            ? (uint8_t)MESH_CAD_BACKOFF_EXP_MAX_SHIFT
            : _cadRetryCount;
        const uint32_t backoffRange = ((uint32_t)MESH_CAD_BACKOFF_MAX_MS) << expShift;
        const uint32_t backoff = backoffRange > 0
            ? (uint32_t)(((_deviceId * 37U) + (_cadRetryCount * 23U) + (millis() & 0x1FFU)) % (backoffRange + 1U))
            : 0;
        _cadBackoffUntilMs = millis() + backoff;
        _rxRearmPending = true;
#endif
    }

    void processSleepControlForward() {
#if MESH_DYNAMIC_SLEEP_CONTROL && MESH_SLEEP_CONTROL_FORWARD_ENABLED
        if (!_sleepControlForwardPending || _txInProgress || _cadInProgress || _cadTxPending) {
            return;
        }
        const uint32_t now = millis();
        if ((int32_t)(now - _sleepControlForwardAtMs) < 0) {
            return;
        }

        _sleepControlForwardPending = false;
        if (!enqueuePacket(_sleepControlForwardPacket)) {
            Serial.println("WARN: Sleep-control forward queue full; dropping control forward.");
        } else {
            MESH_LOG_EVENT("INFO: Forwarding SLEEP_CONTROL seq=%u\n", _sleepControlLastForwardedSeq);
        }
#endif
    }

    /**
     * @brief Count queued packets for a specific destination.
     */
    uint8_t countQueuedForDest(uint16_t dest) const {
        uint8_t count = 0;
        uint8_t index = _txQueueHead;
        for (uint8_t i = 0; i < _txQueueCount; ++i) {
            if (_txQueue[index].destinationID == dest) {
                count++;
            }
            index = (index + 1) % MESH_TX_QUEUE_SIZE;
        }
        return count;
    }

    /**
     * @brief Send a pending-traffic beacon before queued data.
     */
    void sendPendingBeacon() {
        if (_txQueueCount == 0) return;

        const uint16_t pendingDest = _txQueue[_txQueueHead].destinationID;
        const uint8_t pendingCount = countQueuedForDest(pendingDest);
        sendBeaconPacket(pendingCount, pendingDest);
    }

    void maybeSendSleepControl() {
#if MESH_DYNAMIC_SLEEP_CONTROL && MESH_SYNC_MASTER
        if (_txInProgress || _cadInProgress || _cadTxPending) {
            return;
        }
#if MESH_SLEEP_CONTROL_BROADCAST == 0
        if (MESH_SLEEP_CONTROL_TARGET_ID == 0) {
            return;
        }
#endif

        const uint32_t now = millis();

        if (_sleepControlWaitingAck) {
            const bool timedOut =
                (uint32_t)(now - _sleepControlLastTxMs) >= (uint32_t)MESH_SLEEP_CONTROL_ACK_TIMEOUT_MS;
            const uint32_t retryIntervalMs = sleepControlRetryIntervalMs();
            const bool retryIntervalElapsed =
                (uint32_t)(now - _sleepControlLastTxMs) >= retryIntervalMs;

            if (!_sleepControlResendRequested && !timedOut) {
                return;
            }
            if (!retryIntervalElapsed) {
                return;
            }
            if (!_sleepControlResendRequested &&
                _sleepControlRetries >= (uint8_t)MESH_SLEEP_CONTROL_MAX_RETRIES) {
                _sleepControlWaitingAck = false;
                _sleepControlResendRequested = false;
                _sleepControlRetries = 0;
                _sleepControlLastAckMs = now;  // Cooldown anchor before next refresh attempt.
                MESH_LOG_EVENT("WARN: Sleep-control seq=%u retries exhausted; backing off until refresh.\n",
                               _sleepControlSeq);
                return;
            }

            if (sendSleepControlPacket(_sleepControlSeq)) {
                _sleepControlLastTxMs = now;
                _sleepControlResendRequested = false;
                if (_sleepControlRetries < 255) {
                    _sleepControlRetries++;
                }
                _sleepControlRetryTxCount++;
                MESH_LOG_EVENT("INFO: Sleep-control retry seq=%u attempt=%u%s\n",
                               _sleepControlSeq,
                               (unsigned)_sleepControlRetries,
                               timedOut ? " (ack timeout)" : "");
            }
            return;
        }

        if (_sleepControlLastAckMs != 0) {
#if MESH_SLEEP_CONTROL_REFRESH_MS == 0
            return;
#else
            if ((uint32_t)(now - _sleepControlLastAckMs) < (uint32_t)MESH_SLEEP_CONTROL_REFRESH_MS) {
                return;
            }
#endif
        }

        _sleepControlSeq++;
        _sleepControlWaitingAck = true;
        _sleepControlResendRequested = false;
        _sleepControlRetries = 0;
        if (sendSleepControlPacket(_sleepControlSeq)) {
            _sleepControlLastTxMs = now;
            _sleepControlRetries = 1;
        }
#endif
    }

    bool sendSleepControlPacket(uint16_t sequence) {
#if MESH_DYNAMIC_SLEEP_CONTROL && MESH_SYNC_MASTER
        if (_txInProgress) {
            return false;
        }

        MeshPacket control;
        control.originalSenderID = _deviceId;
        control.senderID = _deviceId;
        control.destinationID = (MESH_SLEEP_CONTROL_BROADCAST != 0)
            ? MeshPacket::BROADCAST_ID
            : (uint16_t)MESH_SLEEP_CONTROL_TARGET_ID;
        control.networkID = _networkId;
        control.packetID = _packetCounter++;
        control.packetType = SLEEP_CONTROL_PACKET;
        control.payloadLen = SLEEP_CTRL_PAYLOAD_LEN;

        const uint8_t version = SLEEP_CTRL_VERSION;
        uint8_t flags = 0;
#if MESH_SLEEP_CONTROL_FORWARD_ENABLED
        flags |= SLEEP_CTRL_FLAG_FORWARD;
#endif
        flags |= SLEEP_CTRL_FLAG_ACK_REQUIRED;
        const uint32_t cycleMs = _activeWakeCycleMs;
        const uint32_t wakeWindowMs = _activeWakeWindowMs;
        const uint16_t coordBufferMs = _activeCoordBufferMs;
        uint8_t hopsRemaining = (uint8_t)MESH_SLEEP_CONTROL_FORWARD_HOPS;
#if MESH_SLEEP_CONTROL_FORWARD_ENABLED == 0
        hopsRemaining = 0;
#endif
        uint32_t phaseMs = 0;
        if (wakeScheduleEnabled()) {
            phaseMs = alignCycleBase(millis());
        }

        control.payload[0] = version;
        control.payload[1] = flags;
        memcpy(&control.payload[2], &sequence, sizeof(sequence));
        memcpy(&control.payload[4], &cycleMs, sizeof(cycleMs));
        memcpy(&control.payload[8], &wakeWindowMs, sizeof(wakeWindowMs));
        memcpy(&control.payload[12], &phaseMs, sizeof(phaseMs));
        memcpy(&control.payload[16], &coordBufferMs, sizeof(coordBufferMs));
        control.payload[18] = hopsRemaining;
        control.updateChecksum();

        if (control.destinationID == MeshPacket::BROADCAST_ID) {
            MESH_LOG_EVENT("INFO: Sending SLEEP_CONTROL seq=%u to broadcast (cycle=%lu ms, wake=%lu ms, phase=%lu ms, coord=%u ms, hops=%u)\n",
                           sequence,
                           (unsigned long)cycleMs,
                           (unsigned long)wakeWindowMs,
                           (unsigned long)phaseMs,
                           (unsigned)coordBufferMs,
                           (unsigned)hopsRemaining);
        } else {
            MESH_LOG_EVENT("INFO: Sending SLEEP_CONTROL seq=%u to Node %u (cycle=%lu ms, wake=%lu ms, phase=%lu ms, coord=%u ms, hops=%u)\n",
                           sequence,
                           (unsigned)control.destinationID,
                           (unsigned long)cycleMs,
                           (unsigned long)wakeWindowMs,
                           (unsigned long)phaseMs,
                           (unsigned)coordBufferMs,
                           (unsigned)hopsRemaining);
        }

        sendPacketNow(control);
        return true;
#else
        (void)sequence;
        return false;
#endif
    }

    /**
     * @brief Send sync beacons near cycle start to align schedule across nodes.
     */
    void maybeSendSyncBeacon() {
#if MESH_SYNC_BEACON_ENABLED && MESH_SYNC_MASTER
        if (!wakeScheduleEnabled() || _txInProgress || _txQueueCount > 0) {
            return;
        }

        const uint32_t now = millis();
        const uint32_t elapsed = alignCycleBase(now);
        if (elapsed >= effectiveSyncWindowMs()) {
            return;
        }

        if (_lastSyncBeaconMs != 0 &&
            (uint32_t)(now - _lastSyncBeaconMs) < (uint32_t)MESH_SYNC_BEACON_INTERVAL_MS) {
            return;
        }

        sendBeaconPacket(0, MeshPacket::BROADCAST_ID, true, elapsed);
#endif
    }

    /**
     * @brief Build and send a beacon packet.
     */
    void sendBeaconPacket(uint8_t pendingCount, uint16_t pendingDest, bool forceSync = false, uint32_t phaseMs = 0) {
        MeshPacket beacon;
        beacon.originalSenderID = _deviceId;
        beacon.senderID = _deviceId;
        beacon.destinationID = MeshPacket::BROADCAST_ID;
        beacon.networkID = _networkId;
        beacon.packetID = _packetCounter++;
        beacon.packetType = BEACON_PACKET;
        beacon.payload[0] = pendingCount;
        memcpy(&beacon.payload[1], &pendingDest, sizeof(pendingDest));
        beacon.payloadLen = BEACON_BASE_PAYLOAD_LEN;

#if MESH_SYNC_BEACON_ENABLED
        bool includeSync = forceSync;
#if MESH_SYNC_MASTER
        if (!includeSync && wakeScheduleEnabled()) {
            const uint32_t now = millis();
            const uint32_t elapsed = alignCycleBase(now);
            includeSync = elapsed < effectiveSyncWindowMs();
            if (includeSync) {
                phaseMs = elapsed;
            }
        }
#endif
        if (includeSync && (BEACON_SYNC_PAYLOAD_LEN <= MeshPacket::MAX_PAYLOAD)) {
            beacon.payload[BEACON_BASE_PAYLOAD_LEN] = BEACON_SYNC_FLAG;
            uint32_t cycleMs = _activeWakeCycleMs;
            memcpy(&beacon.payload[BEACON_BASE_PAYLOAD_LEN + 1], &cycleMs, sizeof(cycleMs));
            memcpy(&beacon.payload[BEACON_BASE_PAYLOAD_LEN + 1 + sizeof(cycleMs)], &phaseMs, sizeof(phaseMs));
            beacon.payloadLen = BEACON_SYNC_PAYLOAD_LEN;
            _lastSyncBeaconMs = millis();
            MESH_LOG_EVENT("INFO: Sending SYNC BEACON (phase=%lu ms, cycle=%lu ms)\n",
                           (unsigned long)phaseMs, (unsigned long)cycleMs);
        } else
#endif
        {
            MESH_LOG_EVENT("INFO: Sending BEACON (pending=%u dest=%u)\n", pendingCount, pendingDest);
        }

        beacon.updateChecksum();
        sendPacketNow(beacon);
    }

    /**
     * @brief Send a packet immediately through the radio.
     */
    void sendPacketNow(const MeshPacket& packet) {
        uint8_t buffer[MeshPacket::MAX_PACKET_SIZE];
        packet.serialize(buffer);
        size_t packetSize = packet.getSerializedSize();

        _txInProgress = true;
        _radioListening = false;
        _radioAwake = true;
        Radio.Send(buffer, packetSize);

        if (needsForwardQueueDiscipline(packet)) {
            _nextForwardTxAllowedMs = millis() + (uint32_t)MESH_FORWARD_MIN_INTERVAL_MS;
        }

        if (g_OLEDDisplay != nullptr) {
            g_OLEDDisplay->showPacketSent(packet.packetID, packet.destinationID);
        }

        if (packet.packetType == EXPERIMENT_PACKET) {
            uint16_t experimentId = 0;
            if (packet.payloadLen >= sizeof(experimentId)) {
                memcpy(&experimentId, packet.payload, sizeof(experimentId));
                _experimentPending = false;
                _experimentInFlight = true;
                _experimentLastId = experimentId;
                _experimentLastSentMs = millis();
                _experimentStats.sent++;
            }
        }

        if (packet.packetType == SLEEP_CONTROL_PACKET &&
            packet.payloadLen >= (uint8_t)(2 + sizeof(uint16_t))) {
            uint16_t sequence = 0;
            memcpy(&sequence, &packet.payload[2], sizeof(sequence));
            _sleepControlLastSentValid = true;
            _sleepControlLastSentSeq = sequence;
            _sleepControlLastSentPacket = packet;
        }
    }

    bool isPendingListenActive(uint32_t now) const {
        return (_pendingListenUntilMs != 0) && (static_cast<int32_t>(now - _pendingListenUntilMs) < 0);
    }

    bool isStartupSyncListenActive(uint32_t now) const {
        return (_startupListenUntilMs != 0) && (static_cast<int32_t>(now - _startupListenUntilMs) < 0);
    }

    bool isSyncCoordHoldActive(uint32_t now) const {
        return (_syncCoordHoldUntilMs != 0) && (static_cast<int32_t>(now - _syncCoordHoldUntilMs) < 0);
    }

    bool sleepControlForcesAwake() const {
#if MESH_DYNAMIC_SLEEP_CONTROL && MESH_SYNC_MASTER
        return _sleepControlWaitingAck;
#else
        return false;
#endif
    }

    bool wakeScheduleEnabled() const {
        return _sleepScheduleActive && (_activeWakeCycleMs > 0) && (_activeWakeWindowMs > 0);
    }

    bool isInWakeWindow(uint32_t now) const {
        if (!wakeScheduleEnabled()) {
            return true;
        }
        const uint32_t cycleMs = _activeWakeCycleMs;
        uint32_t elapsed = now - _cycleStartMs;
        if (elapsed >= cycleMs) {
            elapsed %= cycleMs;
        }
        return elapsed < effectiveWakeWindowMs();
    }

    uint32_t alignCycleBase(uint32_t now) {
        if (!wakeScheduleEnabled()) {
            (void)now;
            return 0;
        }
        const uint32_t cycleMs = _activeWakeCycleMs;
        uint32_t elapsed = now - _cycleStartMs;
        if (elapsed >= cycleMs) {
            _cycleStartMs = now - (elapsed % cycleMs);
            elapsed = now - _cycleStartMs;
        }
        return elapsed;
    }

    uint32_t effectiveWakeWindowMs() const {
        uint32_t window = _activeWakeWindowMs +
                          (uint32_t)LORA_PREAMBLE_MARGIN_MS +
                          (uint32_t)_activeCoordBufferMs;
        // In orphan mode, listen longer to catch a new sync source from any direction
#if defined(MESH_SYNC_ORPHAN_WAKE_EXTENSION_MS) && (MESH_SYNC_ORPHAN_WAKE_EXTENSION_MS > 0)
        if (_syncOrphan) {
            window += (uint32_t)MESH_SYNC_ORPHAN_WAKE_EXTENSION_MS;
        }
#endif
        if (_activeWakeCycleMs > 0 && window > _activeWakeCycleMs) {
            window = _activeWakeCycleMs;
        }
        return window;
    }

    uint32_t effectiveSyncWindowMs() const {
        uint32_t syncWindow = (uint32_t)MESH_SYNC_WINDOW_MS;
        uint32_t wakeWindow = effectiveWakeWindowMs();
        if (syncWindow > wakeWindow) {
            syncWindow = wakeWindow;
        }
        return syncWindow;
    }

    uint32_t sleepControlForwardDelayMs(uint16_t sequence) const {
        const uint32_t jitter = (uint32_t)MESH_SLEEP_CONTROL_FORWARD_JITTER_MS;
        if (jitter == 0) {
            return 0;
        }
        return (uint32_t)(((_deviceId * 29U) + (sequence * 11U)) % (jitter + 1U));
    }

    /**
     * @brief Update wake/sleep state based on schedule and pending traffic.
     */
    void updateWakeState() {
#ifdef TEST_MODE_AWAKE
        const bool shouldAwake = true;
#else
        const uint32_t now = millis();
        const bool scheduleEnabled = wakeScheduleEnabled();
        const bool pendingListen = isPendingListenActive(now);
        const bool startupSyncListen = isStartupSyncListenActive(now);
        const bool syncCoordHold = isSyncCoordHoldActive(now);
        const bool sleepControlAwake = sleepControlForcesAwake();
        bool shouldAwake = true;

        if (startupSyncListen) {
            shouldAwake = true;
        } else if (scheduleEnabled) {
            const bool inWakeWindow = isInWakeWindow(now);
#if MESH_TX_ONLY_IN_WAKE_WINDOW
            const bool txForcesAwake = _txInProgress || _cadInProgress || _cadTxPending;
#else
            const bool pendingTraffic = (_txQueueCount > 0) || _txInProgress || _cadInProgress || _cadTxPending;
            const bool txForcesAwake = pendingTraffic;
#endif
            shouldAwake = inWakeWindow || txForcesAwake || pendingListen || syncCoordHold || sleepControlAwake;
        } else if (sleepControlAwake) {
            shouldAwake = true;
        } else if (syncCoordHold) {
            shouldAwake = true;
        }
#endif

        if (shouldAwake) {
            if (!_radioAwake) {
                wakeRadio();
            }
        } else {
            if (_radioAwake && !_txInProgress) {
                sleepRadio();
            }
        }
    }

    void wakeRadio() {
        _radioAwake = true;
        if (!_txInProgress) {
            startListening(false);
        }
    }

    void sleepRadio() {
        _radioAwake = false;
        _radioListening = false;
        _rxRearmPending = false;
        Radio.Sleep();
    }

    /**
     * @brief Handle received beacon packets.
     */
    void handleBeaconPacket(const MeshPacket& packet) {
        if (packet.payloadLen < BEACON_BASE_PAYLOAD_LEN) {
            Serial.println("WARN: Beacon payload too short.");
            return;
        }

        uint8_t pendingCount = packet.payload[0];
        uint16_t pendingDest = 0;
        memcpy(&pendingDest, &packet.payload[1], sizeof(pendingDest));

        MESH_LOG_PACKET("INFO: Beacon from %u pending=%u dest=%u\n",
                        packet.senderID, pendingCount, pendingDest);

        if (pendingCount > 0 && pendingDest == _deviceId) {
            _pendingListenUntilMs = millis() + (uint32_t)MESH_PENDING_LISTEN_MS;
            MESH_LOG_EVENT("INFO: Pending traffic for me; extending listen for %lu ms\n",
                           (unsigned long)MESH_PENDING_LISTEN_MS);
        }

#if MESH_SYNC_BEACON_ENABLED
        uint8_t beaconFlags = 0;
        if (packet.payloadLen >= (BEACON_BASE_PAYLOAD_LEN + 1)) {
            beaconFlags = packet.payload[BEACON_BASE_PAYLOAD_LEN];
        }

        if ((beaconFlags & BEACON_SYNC_FLAG) != 0 &&
            packet.payloadLen >= BEACON_SYNC_PAYLOAD_LEN &&
            packet.senderID != _deviceId &&
            wakeScheduleEnabled()) {
            uint32_t senderCycleMs = 0;
            uint32_t senderPhaseMs = 0;
            memcpy(&senderCycleMs, &packet.payload[BEACON_BASE_PAYLOAD_LEN + 1], sizeof(senderCycleMs));
            memcpy(&senderPhaseMs,
                   &packet.payload[BEACON_BASE_PAYLOAD_LEN + 1 + sizeof(senderCycleMs)],
                   sizeof(senderPhaseMs));
            alignCycleFromBeacon(packet.senderID, senderCycleMs, senderPhaseMs);
        }
#endif
    }

    void alignCycleFromBeacon(uint16_t senderId, uint32_t senderCycleMs, uint32_t senderPhaseMs) {
        if (!wakeScheduleEnabled() || senderCycleMs == 0 || senderCycleMs != _activeWakeCycleMs || senderPhaseMs > senderCycleMs) {
            return;
        }

        if (!_syncLocked) {
            _syncLocked = true;
            _startupListenUntilMs = 0;
            MESH_LOG_EVENT("INFO: Sync lock acquired from Node %u\n", senderId);
        }

        const uint32_t now = millis();
        const uint32_t alignedStart = now - senderPhaseMs;
        const int32_t deltaMs = (int32_t)(alignedStart - _cycleStartMs);
        const int32_t absDeltaMs = (deltaMs >= 0) ? deltaMs : -deltaMs;

        if (absDeltaMs < (int32_t)MESH_SYNC_RESYNC_THRESHOLD_MS) {
            return;
        }

        _cycleStartMs = alignedStart;
        MESH_LOG_EVENT("INFO: Synced wake cycle to Node %u (adjust=%ld ms)\n",
                       senderId, (long)deltaMs);
    }

    void applySleepControl(uint32_t cycleMs,
                           uint32_t wakeWindowMs,
                           uint16_t coordBufferMs,
                           uint32_t phaseMs,
                           uint16_t sequence) {
        if (phaseMs >= cycleMs) {
            phaseMs %= cycleMs;
        }

        _activeWakeCycleMs = cycleMs;
        _activeWakeWindowMs = wakeWindowMs;
        _activeCoordBufferMs = coordBufferMs;
        _sleepScheduleActive = true;
        _syncLocked = true;
        _startupListenUntilMs = 0;
        _pendingListenUntilMs = 0;
        _cycleStartMs = millis() - phaseMs;
        _sleepControlLastAppliedSeq = sequence;
        _syncCoordHoldUntilMs = millis() + (uint32_t)coordBufferMs;
    }

    void sendSleepControlAck(uint16_t destinationId, uint16_t sequence) {
        MeshPacket ack;
        ack.originalSenderID = _deviceId;
        ack.senderID = _deviceId;
        ack.destinationID = destinationId;
        ack.networkID = _networkId;
        ack.packetID = _packetCounter++;
        ack.packetType = SLEEP_CONTROL_ACK_PACKET;
        ack.payloadLen = SLEEP_CTRL_ACK_PAYLOAD_LEN;
        memcpy(&ack.payload[0], &sequence, sizeof(sequence));
        ack.updateChecksum();
        enqueuePacket(ack);
    }

    void sendSleepControlRtr(uint16_t destinationId, uint16_t sequence, uint8_t reasonCode) {
        MeshPacket rtr;
        rtr.originalSenderID = _deviceId;
        rtr.senderID = _deviceId;
        rtr.destinationID = destinationId;
        rtr.networkID = _networkId;
        rtr.packetID = _packetCounter++;
        rtr.packetType = SLEEP_CONTROL_RTR_PACKET;
        rtr.payloadLen = SLEEP_CTRL_RTR_PAYLOAD_LEN;
        memcpy(&rtr.payload[0], &sequence, sizeof(sequence));
        rtr.payload[sizeof(sequence)] = reasonCode;
        rtr.updateChecksum();
        enqueuePacket(rtr);
    }

    void handleSleepControlPacket(const MeshPacket& packet) {
#if MESH_DYNAMIC_SLEEP_CONTROL
        if (packet.payloadLen < (1 + 1 + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t))) {
            Serial.println("WARN: Sleep-control payload too short.");
            sendSleepControlRtr(packet.senderID, 0, 1);
            return;
        }

        const uint8_t version = packet.payload[0];
        const uint8_t flags = packet.payload[1];
        uint16_t sequence = 0;
        uint32_t cycleMs = 0;
        uint32_t wakeWindowMs = 0;
        uint32_t phaseMs = 0;
        memcpy(&sequence, &packet.payload[2], sizeof(sequence));
        memcpy(&cycleMs, &packet.payload[4], sizeof(cycleMs));
        memcpy(&wakeWindowMs, &packet.payload[8], sizeof(wakeWindowMs));
        memcpy(&phaseMs, &packet.payload[12], sizeof(phaseMs));
        uint16_t coordBufferMs = (uint16_t)MESH_SYNC_COORD_BUFFER_MS;
        uint8_t hopsRemaining = 0;
        if (version >= 2 && packet.payloadLen >= SLEEP_CTRL_PAYLOAD_LEN) {
            memcpy(&coordBufferMs, &packet.payload[16], sizeof(coordBufferMs));
            hopsRemaining = packet.payload[18];
        }

        const bool knownVersion = (version == 1) || (version == SLEEP_CTRL_VERSION);
        if (!knownVersion || cycleMs == 0 || wakeWindowMs == 0 || wakeWindowMs > cycleMs) {
            MESH_LOG_EVENT("WARN: Invalid SLEEP_CONTROL from Node %u (ver=%u cycle=%lu wake=%lu)\n",
                           packet.senderID,
                           (unsigned)version,
                           (unsigned long)cycleMs,
                           (unsigned long)wakeWindowMs);
            sendSleepControlRtr(packet.senderID, sequence, 2);
            return;
        }

        const bool duplicateSequence = (_sleepControlLastAppliedSeq == sequence);
        if (!duplicateSequence) {
            applySleepControl(cycleMs, wakeWindowMs, coordBufferMs, phaseMs, sequence);
            MESH_LOG_EVENT("INFO: Applied SLEEP_CONTROL seq=%u (cycle=%lu ms, wake=%lu ms, coord=%u ms, phase=%lu ms)\n",
                           sequence,
                           (unsigned long)cycleMs,
                           (unsigned long)wakeWindowMs,
                           (unsigned)coordBufferMs,
                           (unsigned long)phaseMs);
        }

        // Track sync parent quality (both new and duplicate sequences confirm link health)
        _syncLastReceivedMs = millis();
        if (_syncBestParentId == 0 || _lastRxRssi > _syncBestParentRssi) {
            _syncBestParentId   = packet.originalSenderID;
            _syncBestParentRssi = _lastRxRssi;
        }
        if (_syncOrphan) {
            _syncOrphan = false;
            _syncOrphanRtrLastMs = 0;
            Serial.printf("[MESH] Sync restored from Node %u (RSSI=%d dBm) -- orphan mode cleared\n",
                          packet.originalSenderID, _lastRxRssi);
        }

        if ((flags & SLEEP_CTRL_FLAG_ACK_REQUIRED) != 0 || version == 1) {
            sendSleepControlAck(packet.senderID, sequence);
        }

#if MESH_SLEEP_CONTROL_FORWARD_ENABLED
        const bool forwardAllowed = ((flags & SLEEP_CTRL_FLAG_FORWARD) != 0);
        if (forwardAllowed && hopsRemaining > 0 && !duplicateSequence &&
            packet.senderID != _deviceId && sequence != _sleepControlLastForwardedSeq) {
            _sleepControlLastForwardedSeq = sequence;
            _sleepControlForwardPending = true;
            _sleepControlForwardAtMs = millis() + sleepControlForwardDelayMs(sequence);
            _sleepControlForwardPacket = packet;
            _sleepControlForwardPacket.senderID = _deviceId;
            _sleepControlForwardPacket.destinationID = MeshPacket::BROADCAST_ID;
            _sleepControlForwardPacket.payload[18] = hopsRemaining - 1;
            _sleepControlForwardPacket.updateChecksum();
            _syncCoordHoldUntilMs = millis() + (uint32_t)coordBufferMs;
        }
#else
        (void)flags;
        (void)hopsRemaining;
#endif
#else
        (void)packet;
#endif
    }

    void handleSleepControlAck(const MeshPacket& packet) {
#if MESH_DYNAMIC_SLEEP_CONTROL && MESH_SYNC_MASTER
        if (packet.payloadLen < SLEEP_CTRL_ACK_PAYLOAD_LEN) {
            Serial.println("WARN: Sleep-control ACK payload too short.");
            return;
        }

        uint16_t sequence = 0;
        memcpy(&sequence, &packet.payload[0], sizeof(sequence));
        if (!_sleepControlWaitingAck) {
            return;
        }
        if (sequence != _sleepControlSeq) {
            MESH_LOG_EVENT("INFO: Ignoring SLEEP_CONTROL_ACK seq=%u (waiting seq=%u)\n",
                           sequence, _sleepControlSeq);
            return;
        }

        _sleepControlWaitingAck = false;
        _sleepControlResendRequested = false;
        _sleepControlRetries = 0;
        _sleepControlLastAckSeq = sequence;
        _sleepControlLastAckMs = millis();
        MESH_LOG_EVENT("INFO: Sleep-control ACK received from Node %u (seq=%u)\n",
                       packet.originalSenderID, sequence);
#else
        (void)packet;
#endif
    }

    void handleSleepControlRtr(const MeshPacket& packet) {
#if MESH_DYNAMIC_SLEEP_CONTROL
        if (packet.payloadLen < SLEEP_CTRL_RTR_PAYLOAD_LEN) {
            Serial.println("WARN: Sleep-control RTR payload too short.");
            return;
        }

        uint16_t sequence = 0;
        uint8_t reasonCode = 0;
        memcpy(&sequence, &packet.payload[0], sizeof(sequence));
        reasonCode = packet.payload[sizeof(sequence)];
        (void)reasonCode;

        if (_sleepControlLastSentValid &&
            (sequence == 0 || sequence == _sleepControlLastSentSeq)) {
            MeshPacket resend = _sleepControlLastSentPacket;
            if (resend.destinationID == _deviceId) {
                resend.destinationID = MeshPacket::BROADCAST_ID;
            }
            if (!enqueuePacket(resend)) {
                Serial.println("WARN: Sleep-control resend queue full; dropped RTR request.");
            } else {
                MESH_LOG_EVENT("WARN: Sleep-control RTR from Node %u (seq=%u reason=%u), resending cached control\n",
                               packet.originalSenderID, sequence, reasonCode);
            }
            return;
        }

#if MESH_SYNC_MASTER
        if (sequence != 0 && sequence != _sleepControlSeq) {
            MESH_LOG_EVENT("INFO: Ignoring SLEEP_CONTROL_RTR seq=%u (current seq=%u)\n",
                           sequence, _sleepControlSeq);
            return;
        }

        _sleepControlWaitingAck = true;
        _sleepControlResendRequested = true;
        MESH_LOG_EVENT("WARN: Sleep-control RTR from Node %u (seq=%u reason=%u)\n",
                       packet.originalSenderID, sequence, reasonCode);
#endif
#else
        (void)packet;
#endif
    }

    /**
     * @brief Handle experiment packets (respond with ACK).
     */
    void handleExperimentPacket(const MeshPacket& packet) {
        if (packet.payloadLen < sizeof(uint16_t)) {
            Serial.println("WARN: Experiment packet missing ID.");
            return;
        }

        uint16_t experimentId = 0;
        memcpy(&experimentId, packet.payload, sizeof(experimentId));

        MESH_LOG_EVENT("INFO: Experiment packet %u received from Node %u; sending ACK\n",
                       experimentId, packet.originalSenderID);

        MeshPacket ack;
        ack.originalSenderID = _deviceId;
        ack.senderID = _deviceId;
        ack.destinationID = packet.originalSenderID;
        ack.networkID = _networkId;
        ack.packetID = _packetCounter++;
        ack.packetType = EXPERIMENT_ACK_PACKET;
        ack.payloadLen = sizeof(experimentId);
        memcpy(ack.payload, &experimentId, sizeof(experimentId));
        ack.updateChecksum();

        enqueuePacket(ack);
    }

    /**
     * @brief Handle experiment ACK packets (measure RTT).
     */
    void handleExperimentAck(const MeshPacket& packet) {
        if (packet.payloadLen < sizeof(uint16_t)) {
            Serial.println("WARN: Experiment ACK missing ID.");
            return;
        }

        uint16_t experimentId = 0;
        memcpy(&experimentId, packet.payload, sizeof(experimentId));

        if (!_experimentInFlight || experimentId != _experimentLastId) {
            MESH_LOG_EVENT("INFO: Experiment ACK %u does not match in-flight packet.\n", experimentId);
            return;
        }

        uint32_t now = millis();
        uint32_t rtt = now - _experimentLastSentMs;
        _experimentInFlight = false;
        _experimentStats.acked++;
        _experimentStats.lastRttMs = rtt;
        if (rtt < _experimentStats.minRttMs) _experimentStats.minRttMs = rtt;
        if (rtt > _experimentStats.maxRttMs) _experimentStats.maxRttMs = rtt;
        _experimentStats.sumRttMs += rtt;

        MESH_LOG_EVENT("INFO: Experiment ACK %u received, RTT=%lu ms\n", experimentId, (unsigned long)rtt);
    }

    void handleExperimentTimeouts() {
        if (!_experimentInFlight) return;
        const uint32_t now = millis();
        if ((uint32_t)(now - _experimentLastSentMs) > (uint32_t)EXPERIMENT_ACK_TIMEOUT_MS) {
            Serial.printf("WARN: Experiment ACK timeout for %u\n", _experimentLastId);
            _experimentInFlight = false;
            _experimentStats.timeouts++;
        }
    }

    void maybeReportExperimentStats() {
        const uint32_t now = millis();
        if (_experimentStats.sent == 0 && _experimentStats.acked == 0 && _experimentStats.timeouts == 0) {
            return;
        }
        if ((uint32_t)(now - _experimentStats.lastReportMs) < (uint32_t)EXPERIMENT_REPORT_INTERVAL_MS) {
            return;
        }
        _experimentStats.lastReportMs = now;

        const uint32_t sent = _experimentStats.sent;
        const uint32_t acked = _experimentStats.acked;
        const uint32_t timeouts = _experimentStats.timeouts;
        const uint32_t successPct = sent > 0 ? (acked * 100UL / sent) : 0;
        const uint32_t avgRtt = acked > 0 ? (uint32_t)(_experimentStats.sumRttMs / acked) : 0;
        const uint32_t minRtt = (_experimentStats.minRttMs == UINT32_MAX) ? 0 : _experimentStats.minRttMs;

        Serial.printf("EXPERIMENT: sent=%lu acked=%lu timeouts=%lu success=%lu%% last=%lu ms avg=%lu ms min=%lu ms max=%lu ms\n",
                      (unsigned long)sent,
                      (unsigned long)acked,
                      (unsigned long)timeouts,
                      (unsigned long)successPct,
                      (unsigned long)_experimentStats.lastRttMs,
                      (unsigned long)avgRtt,
                      (unsigned long)minRtt,
                      (unsigned long)_experimentStats.maxRttMs);
    }

    const char* packetTypeToString(PacketType type) const {
        switch (type) {
            case COORDINATION_PACKET: return "COORDINATION";
            case SENSOR_PACKET: return "SENSOR";
            case BEACON_PACKET: return "BEACON";
            case EXPERIMENT_PACKET: return "EXPERIMENT";
            case EXPERIMENT_ACK_PACKET: return "EXPERIMENT_ACK";
            case SLEEP_CONTROL_PACKET: return "SLEEP_CONTROL";
            case SLEEP_CONTROL_ACK_PACKET: return "SLEEP_CONTROL_ACK";
            case SLEEP_CONTROL_RTR_PACKET: return "SLEEP_CONTROL_RTR";
            default: return "UNKNOWN";
        }
    }

    /**
     * @brief Queues a packet for retransmission (non-blocking)
     * @param packet The packet to retransmit.
     */
    void retransmitPacket(MeshPacket& packet) {
        packet.senderID = _deviceId; // Update sender to this node's ID
        packet.updateChecksum();

        if (enqueuePacket(packet)) {
            MESH_LOG_EVENT("INFO: Queued retransmit for Packet ID %u on behalf of Node %u\n",
                           packet.packetID, packet.originalSenderID);
        } else {
            Serial.printf("WARN: Retransmit queue full; dropped Packet ID %u\n", packet.packetID);
        }
    }

    /**
     * @brief Sends a Request-to-Retransmit packet for a failed packet.
     * @param destinationId The node that sent the bad packet.
     * @param failedPacketId The ID of the packet that failed validation.
     */
    void sendRTR(uint16_t destinationId, uint16_t failedPacketId) {
        MeshPacket rtrPacket;
        rtrPacket.originalSenderID = _deviceId;
        rtrPacket.senderID = _deviceId;
        rtrPacket.destinationID = destinationId;
        rtrPacket.networkID = _networkId;
        rtrPacket.packetID = failedPacketId; // Reference the failed packet
        rtrPacket.packetType = COORDINATION_PACKET;
        rtrPacket.payloadLen = 0;
        rtrPacket.updateChecksum();

        MESH_LOG_EVENT("INFO: Queueing RTR for Packet ID %u to Node %u\n", failedPacketId, destinationId);
        enqueuePacket(rtrPacket);
    }
};

// --- Global Pointer for Callbacks ---
extern MeshNode* g_MeshNode;
extern OLEDDisplay* g_OLEDDisplay;

//================================================================//
//                  GLOBAL CALLBACK IMPLEMENTATIONS               //
//================================================================//

inline void OnTxDone(void) {
    if (g_MeshNode == nullptr) return;
    // Don't print from ISR context; return to main loop to do verbose logging
    g_MeshNode->onTxDoneISR();
}

inline void OnTxTimeout(void) {
    if (g_MeshNode == nullptr) return;
    // Don't print from ISR context
    g_MeshNode->onTxTimeoutISR();
}

inline uint16_t readUint16LE(const uint8_t* ptr) {
    uint16_t v;
    memcpy(&v, ptr, sizeof(uint16_t));
    return v;
}

inline void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    if (g_MeshNode == nullptr) return;

    // Queue packet for processing in main loop (non-blocking)
    g_MeshNode->queueReceivedPacket(payload, size, rssi, snr);
    g_MeshNode->onRxDoneISR();

#if OLED_ENABLED
    // Show on OLED if connected (just queue display event - minimal)
    if (size >= MeshPacket::MIN_PACKET_SIZE) {
        uint16_t packetID = readUint16LE(&payload[MeshPacket::OFFSET_PACKET_ID]);
        uint16_t senderID = readUint16LE(&payload[MeshPacket::OFFSET_SENDER_ID]);
        uint8_t payloadLen = payload[MeshPacket::OFFSET_PAYLOAD_LEN];
        uint8_t temp = 0, humidity = 0;
        if (payloadLen >= 4) {
            int16_t temp_scaled;
            uint16_t hum_scaled;
            memcpy(&temp_scaled, &payload[MeshPacket::PAYLOAD_OFFSET + 1], 2);
            memcpy(&hum_scaled, &payload[MeshPacket::PAYLOAD_OFFSET + 3], 2);
            temp = (uint8_t)(temp_scaled / 100);
            humidity = (uint8_t)(hum_scaled / 100);
        }
        // Minimal call to OLED (ISR-safe wrapper sets pending vars only)
        if (g_OLEDDisplay != nullptr) {
            g_OLEDDisplay->showPacketReceived(packetID, senderID, rssi, snr, temp, humidity);
        }
    }
#endif

}

inline void OnRxTimeout(void) {
    if (g_MeshNode == nullptr) return;
    g_MeshNode->onRxTimeoutISR();
}

inline void OnRxError(void) {
    if (g_MeshNode == nullptr) return;
    g_MeshNode->onRxErrorISR();
}

inline void OnCadDone(bool channelActivityDetected) {
    if (g_MeshNode == nullptr) return;
    g_MeshNode->onCadDoneISR(channelActivityDetected);
}

#endif // MESH_NODE_H
