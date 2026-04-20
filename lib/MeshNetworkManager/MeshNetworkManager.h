/**
 * @file MeshNetworkManager.h
 * @brief Enhanced mesh networking manager with multiple configuration modes
 * @version 2.0
 * @date 2025-09-27
 * @details
 * ENHANCED VERSION for RAK4630 with:
 * - Multiple operational configurations
 * - Better networking practices
 * - Power management integration
 * - Future-ready async wakeup support
 * - Improved error handling and diagnostics
 */

#ifndef MESH_NETWORK_MANAGER_H
#define MESH_NETWORK_MANAGER_H

#include <Arduino.h>
#include <SX126x-Arduino.h>
#include <LoRaWan-RAK4630.h>
#include "ProjectConfig.h"

//================================================================//
//                    MESH PACKET STRUCTURES                      //
//================================================================//

/**
 * @class MeshPacket
 * @brief Enhanced mesh networking packet with improved validation
 */
class MeshPacket {
public:
    // Header fields (5 fields as specified)
    uint16_t originalSenderId;  // 0-65535: Original sender of the packet
    uint16_t senderId;          // 0-65535: Current sender (changes during retransmission)
    uint16_t destinationId;     // 0-65535: Final destination (65535 = broadcast)
    uint8_t  networkId;         // 0-255: Network identifier
    uint16_t packetId;          // 0-65535: Sequential packet ID for duplicate detection
    
    // Packet metadata
    uint8_t  ttl;              // Time-to-live (hop count limit)
    uint8_t  packetType;       // Packet type identifier
    uint8_t  priority;         // Message priority (0-7)
    uint32_t timestamp;        // Packet creation timestamp
    
    // Data payload
    uint8_t  dataLength;       // Length of data payload
    uint8_t  data[64];         // Data payload (increased to 64 bytes)
    
    // Integrity and routing
    uint16_t checksum;         // Enhanced 16-bit checksum
    
    // Constants
    static const size_t MAX_DATA_SIZE = 64;
    static const uint8_t MAX_TTL = 10;
    static const uint16_t BROADCAST_ID = 65535;
    
    // Packet types
    enum PacketType {
        DATA_PACKET = 0,
        PING_PACKET = 1,
        ACK_PACKET = 2,
        RTR_PACKET = 3,
        BEACON_PACKET = 4,
        ROUTE_DISCOVER = 5,
        WAKEUP_PACKET = 6  // For future async wakeup
    };
    
    // Priority levels
    enum Priority {
        PRIORITY_LOW = 0,
        PRIORITY_NORMAL = 3,
        PRIORITY_HIGH = 6,
        PRIORITY_CRITICAL = 7
    };
    
    MeshPacket();
    MeshPacket(uint16_t origSender, uint16_t sender, uint16_t dest, 
               uint8_t netId, uint16_t pktId, PacketType type = DATA_PACKET,
               const uint8_t* payload = nullptr, uint8_t payloadLen = 0,
               Priority prio = PRIORITY_NORMAL);
    
    size_t getPacketSize() const;
    void serialize(uint8_t* buffer) const;
    bool deserialize(const uint8_t* buffer, size_t bufferSize);
    bool isValid() const;
    void decrementTTL();
    bool hasExpired() const;
    
private:
    uint16_t calculateChecksum() const;
    size_t getHeaderSize() const;
};

/**
 * @class RouteEntry
 * @brief Routing table entry for mesh networking
 */
class RouteEntry {
public:
    uint16_t destination;
    uint16_t nextHop;
    uint8_t hopCount;
    uint32_t lastSeen;
    int8_t rssi;
    
    RouteEntry(uint16_t dest, uint16_t next, uint8_t hops, int8_t signal = 0)
        : destination(dest), nextHop(next), hopCount(hops), lastSeen(millis()), rssi(signal) {}
};

//================================================================//
//                MESH NETWORK MANAGER CLASS                      //
//================================================================//

// Forward declarations for callback functions
void OnCadDone(bool channelActivityDetected);
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnRxTimeout(void);
void OnRxError(void);

/**
 * @class MeshNetworkManager
 * @brief Enhanced mesh networking manager with multiple operational modes
 */
class MeshNetworkManager {
public:
    /**
     * @enum NodeState_t
     * @brief Enhanced state machine for mesh node operations
     */
    enum NodeState_t {
        IDLE,
        CAD_IN_PROGRESS,
        READY_TO_SEND,
        WAITING_FOR_TX_DONE,
        PROCESSING_RECEIVED_PACKET,
        SLEEP_MODE,
        ROUTE_DISCOVERY,
        WAITING_FOR_ACK
    };
    
    /**
     * @enum ConfigType
     * @brief Configuration types for different operational modes
     */
    enum ConfigType {
        INTERACTIVE_CONFIG,  // User-controlled, full features
        TESTING_CONFIG,      // Always-on testing mode
        PRODUCTION_CONFIG    // Power-optimized production mode
    };
    
    volatile NodeState_t nodeState = IDLE;
    
    // Configuration structure
    struct NetworkConfig {
        uint32_t sleepCycleMs = 3600000;    // 1 hour default
        uint32_t wakeWindowMs = 120000;     // 2 minutes default
        uint8_t maxRetries = 3;
        uint32_t ackTimeoutMs = 5000;
        bool enableRouting = true;
        bool enableAckMode = false;
        bool enableBeacons = true;
        uint32_t beaconIntervalMs = 300000;  // 5 minutes
        int8_t txPower = LORA_TX_POWER_DBM;  // Max power
        uint32_t frequency = LORA_FREQUENCY_HZ;
    };
    
    // Statistics structure
    struct NetworkStats {
        uint32_t packetsTransmitted = 0;
        uint32_t packetsReceived = 0;
        uint32_t packetsForwarded = 0;
        uint32_t packetsDropped = 0;
        uint32_t duplicatesFiltered = 0;
        uint32_t routeDiscoveries = 0;
        int16_t lastRssi = 0;
        int8_t lastSnr = 0;
        uint32_t uptime = 0;
    };
    
private:
    // Core configuration
    uint16_t _deviceId;
    uint8_t _networkId;
    ConfigType _configType;
    NetworkConfig _config;
    NetworkStats _stats;
    
    // Packet management
    static const int PACKET_HISTORY_SIZE = 50;
    static const int MAX_PENDING_PACKETS = 5;
    static const int ROUTING_TABLE_SIZE = 20;
    
    uint16_t _packetCounter;
    uint16_t _packetHistory[PACKET_HISTORY_SIZE];
    int _historyIndex;
    
    // Pending transmission queue
    MeshPacket _pendingPackets[MAX_PENDING_PACKETS];
    bool _pendingSlotUsed[MAX_PENDING_PACKETS];
    int _currentPendingIndex;
    
    // Routing table
    RouteEntry* _routingTable[ROUTING_TABLE_SIZE];
    int _routingTableSize;
    
    // Timing and power management
    uint32_t _lastCycleStartTime;
    uint32_t _lastBeaconTime;
    uint32_t _startTime;
    
    // Radio management
    RadioEvents_t _radioEvents;
    bool _radioInitialized;
    
    // Async wakeup support (for future use)
    bool _asyncWakeupEnabled;
    uint32_t _nextAsyncWakeup;
    
public:
    MeshNetworkManager(uint16_t deviceId, uint8_t networkId);
    ~MeshNetworkManager();
    
    // Initialization and configuration
    bool begin(ConfigType configType);
    bool reconfigure(ConfigType configType);
    void setConfig(const NetworkConfig& config);
    NetworkConfig getConfig() const { return _config; }
    
    // Main execution
    void run();
    
    // Message sending
    bool sendMessage(uint16_t destId, const String& message, 
                    MeshPacket::Priority priority = MeshPacket::PRIORITY_NORMAL);
    bool sendMessage(uint16_t destId, const uint8_t* data, uint8_t len,
                    MeshPacket::Priority priority = MeshPacket::PRIORITY_NORMAL);
    bool sendBroadcast(const String& message);
    bool sendPing(uint16_t destId);
    bool sendPacket(const MeshPacket& packet);
    
    // Power management
    bool canEnterSleep();
    void enterSleepMode();
    void wakeUp();
    void enableAsyncWakeup(bool enable);
    
    // Routing and network management
    bool addRoute(uint16_t destination, uint16_t nextHop, uint8_t hopCount, int8_t rssi = 0);
    RouteEntry* findRoute(uint16_t destination);
    void updateRouteMetrics(uint16_t destination, int8_t rssi);
    void cleanupOldRoutes();
    void triggerRouteDiscovery(uint16_t destination);
    
    // Network diagnostics
    NetworkStats getStats() const { return _stats; }
    void resetStats();
    String getStatusString();
    bool isHealthy();
    uint32_t getFreeMemory();
    
    // Callback handlers (public for global callback functions)
    void onCadDone(bool channelActivityDetected);
    void onTxDone();
    void onTxTimeout();
    void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
    void onRxTimeout();
    void onRxError();
    
private:
    // Configuration methods
    void applyInteractiveConfig();
    void applyTestingConfig();
    void applyProductionConfig();
    bool initializeRadio();
    void configureRadioForMode();
    
    // Packet processing
    void processReceivedPacket(const MeshPacket& packet, int16_t rssi, int8_t snr);
    void handleDataPacket(const MeshPacket& packet);
    void handlePingPacket(const MeshPacket& packet);
    void handleAckPacket(const MeshPacket& packet);
    void handleRtrPacket(const MeshPacket& packet);
    void handleBeaconPacket(const MeshPacket& packet);
    void handleRouteDiscoveryPacket(const MeshPacket& packet);
    void handleWakeupPacket(const MeshPacket& packet);
    
    // Packet management
    bool isPacketDuplicate(uint16_t packetId);
    void addToPacketHistory(uint16_t packetId);
    bool queuePacketForTransmission(const MeshPacket& packet);
    void processTransmissionQueue();
    void retransmitPacket(const MeshPacket& originalPacket);
    void sendAcknowledgment(const MeshPacket& originalPacket);
    void sendRtr(uint16_t originalSender, uint16_t failedPacketId);
    
    // Power and timing management
    void handleSleepCycles();
    void handleBeacons();
    bool shouldSendBeacon();
    void updateStats();
    
    // Routing management
    void processRouteDiscovery(uint16_t destination);
    void forwardPacket(const MeshPacket& packet);
    bool shouldForwardPacket(const MeshPacket& packet);
    void updateRoutingTable(uint16_t source, uint16_t lastHop, uint8_t hopCount, int8_t rssi);
    
    // Error handling
    void handleTransmissionError();
    void handleReceptionError();
    void logError(const String& error);
};

// Global pointer for callbacks
extern MeshNetworkManager* g_MeshManager;

//================================================================//
//                  GLOBAL CALLBACK IMPLEMENTATIONS               //
//================================================================//

inline void OnCadDone(bool channelActivityDetected) {
    if (g_MeshManager != nullptr) {
        g_MeshManager->onCadDone(channelActivityDetected);
    }
}

inline void OnTxDone(void) {
    if (g_MeshManager != nullptr) {
        g_MeshManager->onTxDone();
    }
}

inline void OnTxTimeout(void) {
    if (g_MeshManager != nullptr) {
        g_MeshManager->onTxTimeout();
    }
}

inline void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    if (g_MeshManager != nullptr) {
        g_MeshManager->onRxDone(payload, size, rssi, snr);
    }
}

inline void OnRxTimeout(void) {
    if (g_MeshManager != nullptr) {
        g_MeshManager->onRxTimeout();
    }
}

inline void OnRxError(void) {
    if (g_MeshManager != nullptr) {
        g_MeshManager->onRxError();
    }
}

#endif // MESH_NETWORK_MANAGER_H
