#ifndef MESH_PACKET_H
#define MESH_PACKET_H

#include <Arduino.h>

/**
 * @enum PacketType
 * @brief Defines the type of the packet.
 */
enum PacketType : uint8_t {
    COORDINATION_PACKET,    // Regular mesh coordination (no sensor data)
    SENSOR_PACKET,          // Contains sensor data payload
    BEACON_PACKET,          // Pending-traffic beacon
    EXPERIMENT_PACKET,      // Experiment data packet
    EXPERIMENT_ACK_PACKET,  // Experiment ACK packet
    SLEEP_CONTROL_PACKET,   // Master -> follower sleep timing command
    SLEEP_CONTROL_ACK_PACKET, // Follower -> master ACK for sleep timing command
    SLEEP_CONTROL_RTR_PACKET  // Follower -> master request-to-retransmit
};

/**
 * @class MeshPacket
 * @brief A serializable data structure for mesh network communication.
 * 
 * Two packet types are supported:
 * - COORDINATION_PACKET: Regular mesh routing (no payload)
 * - SENSOR_PACKET: Contains 21-byte sensor data payload
 */
class MeshPacket {
public:
    // --- Header Variables ---
    uint16_t originalSenderID;
    uint16_t senderID;
    uint16_t destinationID;
    uint16_t networkID;
    uint16_t packetID;
    PacketType packetType;

    // --- Payload (sensor data: 21 bytes) ---
    static const size_t MAX_PAYLOAD = 21;
    uint8_t payloadLen;
    uint8_t payload[MAX_PAYLOAD];

    // --- Footer ---
    uint8_t checksum;

    // Broadcast destination ID
    static const uint16_t BROADCAST_ID = 0xFFFF;

    // Byte offsets for serialized packet fields
    static const size_t OFFSET_ORIGINAL_SENDER_ID = 0;
    static const size_t OFFSET_SENDER_ID = OFFSET_ORIGINAL_SENDER_ID + sizeof(uint16_t);
    static const size_t OFFSET_DESTINATION_ID = OFFSET_SENDER_ID + sizeof(uint16_t);
    static const size_t OFFSET_NETWORK_ID = OFFSET_DESTINATION_ID + sizeof(uint16_t);
    static const size_t OFFSET_PACKET_ID = OFFSET_NETWORK_ID + sizeof(uint16_t);
    static const size_t OFFSET_PACKET_TYPE = OFFSET_PACKET_ID + sizeof(uint16_t);
    static const size_t OFFSET_PAYLOAD_LEN = OFFSET_PACKET_TYPE + sizeof(uint8_t);
    static const size_t PAYLOAD_OFFSET = OFFSET_PAYLOAD_LEN + sizeof(uint8_t);

    // Header/footer sizes
    static const size_t HEADER_SIZE = PAYLOAD_OFFSET;
    static const size_t MIN_PACKET_SIZE = HEADER_SIZE + sizeof(checksum);
    static const size_t MAX_PACKET_SIZE = HEADER_SIZE + MAX_PAYLOAD + sizeof(checksum);

    // Legacy alias (max size) for older code paths
    static const size_t PACKET_SIZE = MAX_PACKET_SIZE;

    /**
     * @brief Default constructor.
     */
    MeshPacket() : originalSenderID(0), senderID(0), destinationID(0), networkID(0), 
                   packetID(0), packetType(COORDINATION_PACKET), payloadLen(0), checksum(0) {
        memset(payload, 0, MAX_PAYLOAD);
    }

    /**
     * @brief Calculates the checksum over the packet's data.
     * @return The calculated checksum.
     */
    uint8_t calculateChecksum() const {
        uint8_t chk = 0;
        auto xor_bytes = [&](const void* data, size_t len) {
            const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(data);
            for (size_t i = 0; i < len; ++i) {
                chk ^= byte_ptr[i];
            }
        };

        xor_bytes(&originalSenderID, sizeof(originalSenderID));
        xor_bytes(&senderID, sizeof(senderID));
        xor_bytes(&destinationID, sizeof(destinationID));
        xor_bytes(&networkID, sizeof(networkID));
        xor_bytes(&packetID, sizeof(packetID));
        xor_bytes(&packetType, sizeof(packetType));
        xor_bytes(&payloadLen, sizeof(payloadLen));
        uint8_t effectiveLen = payloadLen > MAX_PAYLOAD ? MAX_PAYLOAD : payloadLen;
        xor_bytes(payload, effectiveLen);

        return chk;
    }

    /**
     * @brief Updates the packet's checksum.
     */
    void updateChecksum() {
        this->checksum = calculateChecksum();
    }

    /**
     * @brief Gets the serialized size of the packet (variable length).
     * @return Total serialized size in bytes.
     */
    size_t getSerializedSize() const {
        size_t payloadBytes = payloadLen > MAX_PAYLOAD ? MAX_PAYLOAD : payloadLen;
        return HEADER_SIZE + payloadBytes + sizeof(checksum);
    }

    /**
     * @brief Serializes the packet content into a byte buffer.
     * @param buffer The buffer to write the serialized data into.
     */
    void serialize(uint8_t* buffer) const {
        size_t offset = 0;
        memcpy(buffer + offset, &originalSenderID, sizeof(originalSenderID));
        offset += sizeof(originalSenderID);
        memcpy(buffer + offset, &senderID, sizeof(senderID));
        offset += sizeof(senderID);
        memcpy(buffer + offset, &destinationID, sizeof(destinationID));
        offset += sizeof(destinationID);
        memcpy(buffer + offset, &networkID, sizeof(networkID));
        offset += sizeof(networkID);
        memcpy(buffer + offset, &packetID, sizeof(packetID));
        offset += sizeof(packetID);
        memcpy(buffer + offset, &packetType, sizeof(packetType));
        offset += sizeof(packetType);
        memcpy(buffer + offset, &payloadLen, sizeof(payloadLen));
        offset += sizeof(payloadLen);
        uint8_t payloadBytes = payloadLen > MAX_PAYLOAD ? MAX_PAYLOAD : payloadLen;
        if (payloadBytes > 0) {
            memcpy(buffer + offset, payload, payloadBytes);
            offset += payloadBytes;
        }
        memcpy(buffer + offset, &checksum, sizeof(checksum));
    }

    /**
     * @brief Deserializes data from a byte buffer into the packet object.
     * @param buffer The buffer to read data from.
     * @return True if the checksum is valid, false otherwise.
     */
    bool deserialize(const uint8_t* buffer, size_t bufferSize) {
        if (bufferSize < MIN_PACKET_SIZE) {
            return false;
        }

        size_t offset = 0;
        memcpy(&originalSenderID, buffer + offset, sizeof(originalSenderID));
        offset += sizeof(originalSenderID);
        memcpy(&senderID, buffer + offset, sizeof(senderID));
        offset += sizeof(senderID);
        memcpy(&destinationID, buffer + offset, sizeof(destinationID));
        offset += sizeof(destinationID);
        memcpy(&networkID, buffer + offset, sizeof(networkID));
        offset += sizeof(networkID);
        memcpy(&packetID, buffer + offset, sizeof(packetID));
        offset += sizeof(packetID);
        memcpy(&packetType, buffer + offset, sizeof(packetType));
        offset += sizeof(packetType);
        memcpy(&payloadLen, buffer + offset, sizeof(payloadLen));
        offset += sizeof(payloadLen);
        if (payloadLen > MAX_PAYLOAD) {
            return false;
        }

        size_t expectedSize = HEADER_SIZE + payloadLen + sizeof(checksum);
        if (bufferSize < expectedSize) {
            return false;
        }

        memset(payload, 0, MAX_PAYLOAD);
        if (payloadLen > 0) {
            memcpy(payload, buffer + offset, payloadLen);
            offset += payloadLen;
        }
        
        uint8_t receivedChecksum;
        memcpy(&receivedChecksum, buffer + offset, sizeof(receivedChecksum));

        // Verify checksum
        if (receivedChecksum == calculateChecksum()) {
            this->checksum = receivedChecksum;
            return true;
        }
        return false;
    }
};

#endif // MESH_PACKET_H
