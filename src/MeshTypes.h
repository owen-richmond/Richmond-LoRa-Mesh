#ifndef MESH_TYPES_H
#define MESH_TYPES_H

#include <Arduino.h>

// Define node IDs for a 3-node network
#define NODE_ID_1 1
#define NODE_ID_2 2
#define NODE_ID_3 3

// Define a Network ID
#define NETWORK_ID 17

// Define packet types for control messages
enum PacketType : uint8_t
{
	DATA_PACKET,
	RTR_PACKET // Return to Sender (Negative Acknowledgement)
};

/**
 * @brief Structure for our mesh network packet.
 */
struct LoRaPacket
{
	uint16_t originalSenderId; // ID of the node that created the packet
	uint16_t senderId;		   // ID of the node that last sent/forwarded it
	uint16_t destinationId;	// ID of the intended final recipient
	uint8_t networkId;		   // ID of this LoRa network
	uint16_t packetId;		   // Unique and sequential ID for the packet
	PacketType packetType;	 // Type of packet (DATA or RTR)
	char data[200];			   // Payload data (size can be adjusted)
	uint16_t checksum;		   // Checksum for packet integrity
};

#endif // MESH_TYPES_H
