/**
 * @file MeshNetworkManager.cpp
 * @brief Implementation of MeshNetworkManager
 * @version 2.0
 * @date 2026-01-30
 */

#include "MeshNetworkManager.h"

// Global pointer for callbacks
MeshNetworkManager* g_MeshManager = nullptr;

//================================================================//
//                  MESHPACKET IMPLEMENTATION                     //
//================================================================//

MeshPacket::MeshPacket() 
    : originalSenderId(0), senderId(0), destinationId(0), networkId(0),
      packetId(0), ttl(MAX_TTL), packetType(DATA_PACKET), priority(PRIORITY_NORMAL),
      timestamp(0), dataLength(0), checksum(0) {
    memset(data, 0, sizeof(data));
}

MeshPacket::MeshPacket(uint16_t origSender, uint16_t sender, uint16_t dest, 
                       uint8_t netId, uint16_t pktId, PacketType type,
                       const uint8_t* payload, uint8_t payloadLen, Priority prio)
    : originalSenderId(origSender), senderId(sender), destinationId(dest),
      networkId(netId), packetId(pktId), ttl(MAX_TTL), packetType(type),
      priority(prio), timestamp(millis()), dataLength(0), checksum(0) {
    
    memset(data, 0, sizeof(data));
    if (payload && payloadLen > 0) {
        dataLength = min(payloadLen, (uint8_t)MAX_DATA_SIZE);
        memcpy(data, payload, dataLength);
    }
    checksum = calculateChecksum();
}

size_t MeshPacket::getPacketSize() const {
    return getHeaderSize() + dataLength;
}

size_t MeshPacket::getHeaderSize() const {
    return sizeof(originalSenderId) + sizeof(senderId) + sizeof(destinationId) +
           sizeof(networkId) + sizeof(packetId) + sizeof(ttl) + sizeof(packetType) +
           sizeof(priority) + sizeof(timestamp) + sizeof(dataLength) + sizeof(checksum);
}

void MeshPacket::serialize(uint8_t* buffer) const {
    if (!buffer) return;
    
    size_t offset = 0;
    memcpy(buffer + offset, &originalSenderId, sizeof(originalSenderId)); offset += sizeof(originalSenderId);
    memcpy(buffer + offset, &senderId, sizeof(senderId)); offset += sizeof(senderId);
    memcpy(buffer + offset, &destinationId, sizeof(destinationId)); offset += sizeof(destinationId);
    memcpy(buffer + offset, &networkId, sizeof(networkId)); offset += sizeof(networkId);
    memcpy(buffer + offset, &packetId, sizeof(packetId)); offset += sizeof(packetId);
    memcpy(buffer + offset, &ttl, sizeof(ttl)); offset += sizeof(ttl);
    memcpy(buffer + offset, &packetType, sizeof(packetType)); offset += sizeof(packetType);
    memcpy(buffer + offset, &priority, sizeof(priority)); offset += sizeof(priority);
    memcpy(buffer + offset, &timestamp, sizeof(timestamp)); offset += sizeof(timestamp);
    memcpy(buffer + offset, &dataLength, sizeof(dataLength)); offset += sizeof(dataLength);
    memcpy(buffer + offset, &checksum, sizeof(checksum)); offset += sizeof(checksum);
    
    if (dataLength > 0) {
        memcpy(buffer + offset, data, dataLength);
    }
}

bool MeshPacket::deserialize(const uint8_t* buffer, size_t bufferSize) {
    if (!buffer || bufferSize < getHeaderSize()) return false;
    
    size_t offset = 0;
    memcpy(&originalSenderId, buffer + offset, sizeof(originalSenderId)); offset += sizeof(originalSenderId);
    memcpy(&senderId, buffer + offset, sizeof(senderId)); offset += sizeof(senderId);
    memcpy(&destinationId, buffer + offset, sizeof(destinationId)); offset += sizeof(destinationId);
    memcpy(&networkId, buffer + offset, sizeof(networkId)); offset += sizeof(networkId);
    memcpy(&packetId, buffer + offset, sizeof(packetId)); offset += sizeof(packetId);
    memcpy(&ttl, buffer + offset, sizeof(ttl)); offset += sizeof(ttl);
    memcpy(&packetType, buffer + offset, sizeof(packetType)); offset += sizeof(packetType);
    memcpy(&priority, buffer + offset, sizeof(priority)); offset += sizeof(priority);
    memcpy(&timestamp, buffer + offset, sizeof(timestamp)); offset += sizeof(timestamp);
    memcpy(&dataLength, buffer + offset, sizeof(dataLength)); offset += sizeof(dataLength);
    memcpy(&checksum, buffer + offset, sizeof(checksum)); offset += sizeof(checksum);
    
    if (dataLength > MAX_DATA_SIZE) return false;
    if (bufferSize < getHeaderSize() + dataLength) return false;
    
    if (dataLength > 0) {
        memcpy(data, buffer + offset, dataLength);
    }
    
    return isValid();
}

bool MeshPacket::isValid() const {
    return (checksum == calculateChecksum()) && (dataLength <= MAX_DATA_SIZE);
}

void MeshPacket::decrementTTL() {
    if (ttl > 0) ttl--;
}

bool MeshPacket::hasExpired() const {
    return (ttl == 0);
}

uint16_t MeshPacket::calculateChecksum() const {
    uint16_t sum = 0;
    sum += originalSenderId;
    sum += senderId;
    sum += destinationId;
    sum += networkId;
    sum += packetId;
    sum += ttl;
    sum += packetType;
    sum += priority;
    sum += (uint16_t)(timestamp & 0xFFFF);
    sum += (uint16_t)((timestamp >> 16) & 0xFFFF);
    sum += dataLength;
    
    for (int i = 0; i < dataLength; i++) {
        sum += data[i];
    }
    
    return sum;
}

//================================================================//
//            MESHNETWORKMANAGER IMPLEMENTATION                   //
//================================================================//

MeshNetworkManager::MeshNetworkManager(uint16_t deviceId, uint8_t networkId)
    : _deviceId(deviceId), _networkId(networkId), _configType(INTERACTIVE_CONFIG),
      _packetCounter(0), _historyIndex(0), _currentPendingIndex(0),
      _routingTableSize(0), _lastCycleStartTime(0), _lastBeaconTime(0),
      _radioInitialized(false), _asyncWakeupEnabled(false), _nextAsyncWakeup(0) {
    
    memset(_packetHistory, 0, sizeof(_packetHistory));
    memset(_pendingSlotUsed, 0, sizeof(_pendingSlotUsed));
    
    for (int i = 0; i < ROUTING_TABLE_SIZE; i++) {
        _routingTable[i] = nullptr;
    }
    
    g_MeshManager = this;
    _startTime = millis();
}

MeshNetworkManager::~MeshNetworkManager() {
    for (int i = 0; i < ROUTING_TABLE_SIZE; i++) {
        if (_routingTable[i]) {
            delete _routingTable[i];
            _routingTable[i] = nullptr;
        }
    }
    
    if (g_MeshManager == this) {
        g_MeshManager = nullptr;
    }
}

bool MeshNetworkManager::begin(ConfigType configType) {
    _configType = configType;
    
    // Apply configuration based on type
    switch (configType) {
        case INTERACTIVE_CONFIG:
            applyInteractiveConfig();
            break;
        case TESTING_CONFIG:
            applyTestingConfig();
            break;
        case PRODUCTION_CONFIG:
            applyProductionConfig();
            break;
    }
    
    // Initialize radio
    if (!initializeRadio()) {
        Serial.println("ERROR: Failed to initialize radio");
        return false;
    }
    
    _radioInitialized = true;
    _lastCycleStartTime = millis();
    _lastBeaconTime = millis();
    
    Serial.println("MeshNetworkManager initialized successfully");
    return true;
}

bool MeshNetworkManager::initializeRadio() {
    // Setup radio callbacks
    _radioEvents.CadDone = ::OnCadDone;
    _radioEvents.TxDone = ::OnTxDone;
    _radioEvents.TxTimeout = ::OnTxTimeout;
    _radioEvents.RxDone = ::OnRxDone;
    _radioEvents.RxTimeout = ::OnRxTimeout;
    _radioEvents.RxError = ::OnRxError;
    
    // Initialize radio hardware
    Radio.Init(&_radioEvents);
    
    configureRadioForMode();
    
    // Start receiving
    Radio.Rx(0);
    nodeState = IDLE;
    
    return true;
}

void MeshNetworkManager::configureRadioForMode() {
    Radio.SetChannel(_config.frequency);
    Radio.SetTxConfig(MODEM_LORA, _config.txPower, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODING_RATE,
                      LORA_LONG_PREAMBLE_SYMBOLS, LORA_FIX_LEN, LORA_CRC_ENABLED,
                      LORA_FREQ_HOP_ON, LORA_HOP_PERIOD, LORA_IQ_INVERTED_ENABLED,
                      LORA_TX_TIMEOUT_MS);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODING_RATE, 0, LORA_LONG_PREAMBLE_SYMBOLS,
                      LORA_SYMB_TIMEOUT, LORA_FIX_LEN, LORA_PAYLOAD_LEN,
                      LORA_CRC_ENABLED, LORA_FREQ_HOP_ON, LORA_HOP_PERIOD,
                      LORA_IQ_INVERTED_ENABLED, LORA_RX_CONTINUOUS);
}

void MeshNetworkManager::applyInteractiveConfig() {
    _config.sleepCycleMs = 3600000;
    _config.wakeWindowMs = 120000;
    _config.maxRetries = 3;
    _config.ackTimeoutMs = 5000;
    _config.enableRouting = true;
    _config.enableAckMode = false;
    _config.enableBeacons = true;
    _config.beaconIntervalMs = 300000;
    _config.txPower = 22;
    _config.frequency = LORA_FREQUENCY_HZ;
}

void MeshNetworkManager::applyTestingConfig() {
    _config.sleepCycleMs = 0;  // No sleep in testing
    _config.wakeWindowMs = 0;
    _config.maxRetries = 5;
    _config.ackTimeoutMs = 3000;
    _config.enableRouting = true;
    _config.enableAckMode = true;
    _config.enableBeacons = true;
    _config.beaconIntervalMs = 60000;  // More frequent beacons
    _config.txPower = 22;
    _config.frequency = LORA_FREQUENCY_HZ;
}

void MeshNetworkManager::applyProductionConfig() {
    _config.sleepCycleMs = 3600000;
    _config.wakeWindowMs = 60000;
    _config.maxRetries = 2;
    _config.ackTimeoutMs = 2000;
    _config.enableRouting = true;
    _config.enableAckMode = false;
    _config.enableBeacons = false;
    _config.beaconIntervalMs = 0;
    _config.txPower = 14;  // Lower power
    _config.frequency = LORA_FREQUENCY_HZ;
}

bool MeshNetworkManager::reconfigure(ConfigType configType) {
    _configType = configType;
    return begin(configType);
}

void MeshNetworkManager::setConfig(const NetworkConfig& config) {
    _config = config;
    if (_radioInitialized) {
        configureRadioForMode();
    }
}

void MeshNetworkManager::run() {
    updateStats();
    processTransmissionQueue();
    
    if (_config.enableBeacons) {
        handleBeacons();
    }
    
    cleanupOldRoutes();
}

bool MeshNetworkManager::sendMessage(uint16_t destId, const String& message, MeshPacket::Priority priority) {
    return sendMessage(destId, (const uint8_t*)message.c_str(), message.length(), priority);
}

bool MeshNetworkManager::sendMessage(uint16_t destId, const uint8_t* data, uint8_t len, MeshPacket::Priority priority) {
    MeshPacket packet(_deviceId, _deviceId, destId, _networkId, _packetCounter++,
                     MeshPacket::DATA_PACKET, data, len, priority);
    return sendPacket(packet);
}

bool MeshNetworkManager::sendBroadcast(const String& message) {
    return sendMessage(MeshPacket::BROADCAST_ID, message, MeshPacket::PRIORITY_NORMAL);
}

bool MeshNetworkManager::sendPing(uint16_t destId) {
    MeshPacket packet(_deviceId, _deviceId, destId, _networkId, _packetCounter++,
                     MeshPacket::PING_PACKET, nullptr, 0, MeshPacket::PRIORITY_HIGH);
    return sendPacket(packet);
}

bool MeshNetworkManager::sendPacket(const MeshPacket& packet) {
    if (nodeState != IDLE && nodeState != READY_TO_SEND) {
        return queuePacketForTransmission(packet);
    }
    
    uint8_t buffer[256];
    packet.serialize(buffer);
    size_t packetSize = packet.getPacketSize();
    
    nodeState = WAITING_FOR_TX_DONE;
    Radio.Send(buffer, packetSize);
    
    return true;
}

bool MeshNetworkManager::queuePacketForTransmission(const MeshPacket& packet) {
    for (int i = 0; i < MAX_PENDING_PACKETS; i++) {
        if (!_pendingSlotUsed[i]) {
            _pendingPackets[i] = packet;
            _pendingSlotUsed[i] = true;
            return true;
        }
    }
    _stats.packetsDropped++;
    return false;
}

void MeshNetworkManager::processTransmissionQueue() {
    if (nodeState != IDLE && nodeState != READY_TO_SEND) return;
    
    for (int i = 0; i < MAX_PENDING_PACKETS; i++) {
        if (_pendingSlotUsed[i]) {
            sendPacket(_pendingPackets[i]);
            _pendingSlotUsed[i] = false;
            return;
        }
    }
}

bool MeshNetworkManager::canEnterSleep() {
    // Check if any pending packets
    for (int i = 0; i < MAX_PENDING_PACKETS; i++) {
        if (_pendingSlotUsed[i]) return false;
    }
    
    return (nodeState == IDLE);
}

void MeshNetworkManager::enterSleepMode() {
    if (canEnterSleep()) {
        Radio.Sleep();
        nodeState = SLEEP_MODE;
    }
}

void MeshNetworkManager::wakeUp() {
    if (nodeState == SLEEP_MODE) {
        Radio.Rx(0);
        nodeState = IDLE;
    }
}

void MeshNetworkManager::enableAsyncWakeup(bool enable) {
    _asyncWakeupEnabled = enable;
}

bool MeshNetworkManager::addRoute(uint16_t destination, uint16_t nextHop, uint8_t hopCount, int8_t rssi) {
    // Check if route already exists
    for (int i = 0; i < _routingTableSize; i++) {
        if (_routingTable[i] && _routingTable[i]->destination == destination) {
            // Update existing route if better
            if (hopCount < _routingTable[i]->hopCount) {
                _routingTable[i]->nextHop = nextHop;
                _routingTable[i]->hopCount = hopCount;
                _routingTable[i]->rssi = rssi;
                _routingTable[i]->lastSeen = millis();
            }
            return true;
        }
    }
    
    // Add new route
    if (_routingTableSize < ROUTING_TABLE_SIZE) {
        _routingTable[_routingTableSize++] = new RouteEntry(destination, nextHop, hopCount, rssi);
        return true;
    }
    
    return false;
}

RouteEntry* MeshNetworkManager::findRoute(uint16_t destination) {
    for (int i = 0; i < _routingTableSize; i++) {
        if (_routingTable[i] && _routingTable[i]->destination == destination) {
            return _routingTable[i];
        }
    }
    return nullptr;
}

void MeshNetworkManager::updateRouteMetrics(uint16_t destination, int8_t rssi) {
    RouteEntry* route = findRoute(destination);
    if (route) {
        route->rssi = rssi;
        route->lastSeen = millis();
    }
}

void MeshNetworkManager::cleanupOldRoutes() {
    const uint32_t ROUTE_TIMEOUT = 600000;  // 10 minutes
    uint32_t now = millis();
    
    for (int i = 0; i < _routingTableSize; i++) {
        if (_routingTable[i] && (now - _routingTable[i]->lastSeen > ROUTE_TIMEOUT)) {
            delete _routingTable[i];
            _routingTable[i] = nullptr;
            
            // Compact the table
            for (int j = i; j < _routingTableSize - 1; j++) {
                _routingTable[j] = _routingTable[j + 1];
            }
            _routingTable[_routingTableSize - 1] = nullptr;
            _routingTableSize--;
            i--;
        }
    }
}

void MeshNetworkManager::triggerRouteDiscovery(uint16_t destination) {
    _stats.routeDiscoveries++;
    // Stub implementation
}

void MeshNetworkManager::resetStats() {
    memset(&_stats, 0, sizeof(_stats));
}

String MeshNetworkManager::getStatusString() {
    String status = "Device ID: " + String(_deviceId) + "\n";
    status += "Network ID: " + String(_networkId) + "\n";
    status += "State: " + String(nodeState) + "\n";
    status += "TX: " + String(_stats.packetsTransmitted) + " RX: " + String(_stats.packetsReceived) + "\n";
    status += "Routes: " + String(_routingTableSize) + "\n";
    return status;
}

bool MeshNetworkManager::isHealthy() {
    return _radioInitialized && (nodeState != SLEEP_MODE);
}

uint32_t MeshNetworkManager::getFreeMemory() {
    // Platform-specific implementation would go here
    return 0;
}

void MeshNetworkManager::updateStats() {
    _stats.uptime = millis() - _startTime;
}

void MeshNetworkManager::handleBeacons() {
    if (!_config.enableBeacons) return;
    
    uint32_t now = millis();
    if (now - _lastBeaconTime >= _config.beaconIntervalMs) {
        // Send beacon
        MeshPacket beacon(_deviceId, _deviceId, MeshPacket::BROADCAST_ID, _networkId,
                         _packetCounter++, MeshPacket::BEACON_PACKET);
        sendPacket(beacon);
        _lastBeaconTime = now;
    }
}

bool MeshNetworkManager::isPacketDuplicate(uint16_t packetId) {
    for (int i = 0; i < PACKET_HISTORY_SIZE; i++) {
        if (_packetHistory[i] == packetId) {
            return true;
        }
    }
    return false;
}

void MeshNetworkManager::addToPacketHistory(uint16_t packetId) {
    _packetHistory[_historyIndex] = packetId;
    _historyIndex = (_historyIndex + 1) % PACKET_HISTORY_SIZE;
}

// Radio callback implementations
void MeshNetworkManager::onCadDone(bool channelActivityDetected) {
    nodeState = channelActivityDetected ? IDLE : READY_TO_SEND;
}

void MeshNetworkManager::onTxDone() {
    _stats.packetsTransmitted++;
    nodeState = IDLE;
    Radio.Rx(0);
}

void MeshNetworkManager::onTxTimeout() {
    handleTransmissionError();
    nodeState = IDLE;
    Radio.Rx(0);
}

void MeshNetworkManager::onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    _stats.packetsReceived++;
    _stats.lastRssi = rssi;
    _stats.lastSnr = snr;
    
    MeshPacket packet;
    if (packet.deserialize(payload, size) && packet.isValid()) {
        if (!isPacketDuplicate(packet.packetId)) {
            addToPacketHistory(packet.packetId);
            processReceivedPacket(packet, rssi, snr);
        } else {
            _stats.duplicatesFiltered++;
        }
    }
    
    nodeState = IDLE;
}

void MeshNetworkManager::onRxTimeout() {
    nodeState = IDLE;
}

void MeshNetworkManager::onRxError() {
    handleReceptionError();
    nodeState = IDLE;
}

void MeshNetworkManager::processReceivedPacket(const MeshPacket& packet, int16_t rssi, int8_t snr) {
    // Update routing information
    updateRoutingTable(packet.originalSenderId, packet.senderId, 1, rssi);
    
    // Handle based on packet type
    switch (packet.packetType) {
        case MeshPacket::DATA_PACKET:
            handleDataPacket(packet);
            break;
        case MeshPacket::PING_PACKET:
            handlePingPacket(packet);
            break;
        case MeshPacket::ACK_PACKET:
            handleAckPacket(packet);
            break;
        case MeshPacket::BEACON_PACKET:
            handleBeaconPacket(packet);
            break;
        default:
            break;
    }
}

void MeshNetworkManager::handleDataPacket(const MeshPacket& packet) {
    if (packet.destinationId == _deviceId || packet.destinationId == MeshPacket::BROADCAST_ID) {
        // This packet is for us
        Serial.print("MSG from ");
        Serial.print(packet.originalSenderId);
        Serial.print(": ");
        Serial.println((char*)packet.data);
    } else if (_config.enableRouting && !packet.hasExpired()) {
        // Forward packet
        forwardPacket(packet);
    }
}

void MeshNetworkManager::handlePingPacket(const MeshPacket& packet) {
    if (packet.destinationId == _deviceId) {
        Serial.println("PING from " + String(packet.originalSenderId));
        // Send ACK
        MeshPacket ack(_deviceId, _deviceId, packet.originalSenderId, _networkId,
                      _packetCounter++, MeshPacket::ACK_PACKET);
        sendPacket(ack);
    }
}

void MeshNetworkManager::handleAckPacket(const MeshPacket& packet) {
    Serial.println("ACK from " + String(packet.originalSenderId));
}

void MeshNetworkManager::handleRtrPacket(const MeshPacket& packet) {
    // Ready-to-receive handling
}

void MeshNetworkManager::handleBeaconPacket(const MeshPacket& packet) {
    Serial.println("BEACON from " + String(packet.originalSenderId));
}

void MeshNetworkManager::handleRouteDiscoveryPacket(const MeshPacket& packet) {
    // Route discovery handling
}

void MeshNetworkManager::handleWakeupPacket(const MeshPacket& packet) {
    // Wakeup packet handling
}

void MeshNetworkManager::forwardPacket(const MeshPacket& originalPacket) {
    if (shouldForwardPacket(originalPacket)) {
        MeshPacket forwarded = originalPacket;
        forwarded.senderId = _deviceId;
        forwarded.decrementTTL();
        
        if (!forwarded.hasExpired()) {
            sendPacket(forwarded);
            _stats.packetsForwarded++;
        }
    }
}

bool MeshNetworkManager::shouldForwardPacket(const MeshPacket& packet) {
    return _config.enableRouting && 
           packet.destinationId != _deviceId &&
           !packet.hasExpired();
}

void MeshNetworkManager::updateRoutingTable(uint16_t source, uint16_t lastHop, uint8_t hopCount, int8_t rssi) {
    addRoute(source, lastHop, hopCount, rssi);
}

void MeshNetworkManager::retransmitPacket(const MeshPacket& originalPacket) {
    sendPacket(originalPacket);
}

void MeshNetworkManager::sendAcknowledgment(const MeshPacket& originalPacket) {
    MeshPacket ack(_deviceId, _deviceId, originalPacket.originalSenderId, _networkId,
                  _packetCounter++, MeshPacket::ACK_PACKET);
    sendPacket(ack);
}

void MeshNetworkManager::sendRtr(uint16_t originalSender, uint16_t failedPacketId) {
    MeshPacket rtr(_deviceId, _deviceId, originalSender, _networkId,
                  _packetCounter++, MeshPacket::RTR_PACKET);
    sendPacket(rtr);
}

void MeshNetworkManager::handleSleepCycles() {
    // Sleep cycle management
}

bool MeshNetworkManager::shouldSendBeacon() {
    return _config.enableBeacons && 
           (millis() - _lastBeaconTime >= _config.beaconIntervalMs);
}

void MeshNetworkManager::processRouteDiscovery(uint16_t destination) {
    // Route discovery process
}

void MeshNetworkManager::handleTransmissionError() {
    _stats.packetsDropped++;
    logError("Transmission error");
}

void MeshNetworkManager::handleReceptionError() {
    _stats.packetsDropped++;
    logError("Reception error");
}

void MeshNetworkManager::logError(const String& error) {
    Serial.println("ERROR: " + error);
}
