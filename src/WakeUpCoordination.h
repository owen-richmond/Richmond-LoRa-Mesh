/**
 * @file WakeUpCoordination.h
 * @brief A unified, object-oriented class to manage LoRa node roles and cycles.
 * @version 3.0
 * @details This file provides a single `WakeUpCoordinator` class that can be
 * configured to act as a low-power, timed SENDER or a continuous RECEIVER.
 * This allows for a clean, minimal main sketch and a flexible network architecture.
 *
 * v3.0: Complete rewrite into a unified, role-based class.
 * - Integrated TimerPacket class for a single-file solution.
 * - Added SENDER and RECEIVER roles.
 * - begin() and run() methods now contain role-based logic.
 * - All necessary TX, RX, and CAD callbacks are now included.
 */

#ifndef WAKE_UP_COORDINATION_H
#define WAKE_UP_COORDINATION_H

#include <Arduino.h>
#include <SX126x-Arduino.h>
#include "ProjectConfig.h"

//================================================================//
//                  INTEGRATED TIMER PACKET CLASS                 //
//================================================================//

/**
 * @class TimerPacket
 * @brief A simple, serializable data structure to communicate timing and state.
 */
class TimerPacket {
public:
    uint32_t packetId;
    uint16_t messageInterval_s;
    uint16_t wakeWindow_s;
    uint8_t  checksum;

    static const size_t PACKET_SIZE = sizeof(packetId) + sizeof(messageInterval_s) + sizeof(wakeWindow_s) + sizeof(checksum);

    TimerPacket() : packetId(0), messageInterval_s(0), wakeWindow_s(0), checksum(0) {}

    TimerPacket(uint32_t id, uint16_t interval, uint16_t window)
        : packetId(id), messageInterval_s(interval), wakeWindow_s(window), checksum(0) {
        this->checksum = calculateChecksum();
    }

    void serialize(uint8_t* buffer) const {
        size_t offset = 0;
        memcpy(buffer + offset, &packetId, sizeof(packetId));
        offset += sizeof(packetId);
        memcpy(buffer + offset, &messageInterval_s, sizeof(messageInterval_s));
        offset += sizeof(messageInterval_s);
        memcpy(buffer + offset, &wakeWindow_s, sizeof(wakeWindow_s));
        offset += sizeof(wakeWindow_s);
        memcpy(buffer + offset, &checksum, sizeof(checksum));
    }

    bool deserialize(const uint8_t* buffer) {
        size_t offset = 0;
        memcpy(&packetId, buffer + offset, sizeof(packetId));
        offset += sizeof(packetId);
        memcpy(&messageInterval_s, buffer + offset, sizeof(messageInterval_s));
        offset += sizeof(messageInterval_s);
        memcpy(&wakeWindow_s, buffer + offset, sizeof(wakeWindow_s));
        offset += sizeof(wakeWindow_s);
        uint8_t receivedChecksum;
        memcpy(&receivedChecksum, buffer + offset, sizeof(receivedChecksum));
        if (receivedChecksum == calculateChecksum()) {
            this->checksum = receivedChecksum;
            return true;
        }
        return false;
    }
private:
    uint8_t calculateChecksum() const {
        uint8_t chk = 0;
        const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(this);
        for (size_t i = 0; i < (PACKET_SIZE - sizeof(checksum)); ++i) {
            chk ^= byte_ptr[i];
        }
        return chk;
    }
};


//================================================================//
//                UNIFIED WAKEUP COORDINATOR CLASS                //
//================================================================//

// --- Forward declarations for ALL possible global callback functions ---
void OnCadDone(bool channelActivityDetected);
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnRxTimeout(void);
void OnRxError(void);

/**
 * @class WakeUpCoordinator
 * @brief Manages the state and timing for a LoRa node, configurable as SENDER or RECEIVER.
 */
class WakeUpCoordinator
{
public:
    /**
     * @enum Role
     * @brief Defines the primary function of this LoRa node.
     */
    enum Role { SENDER, RECEIVER };

    /**
     * @enum SenderState_t
     * @brief Defines the states for the SENDER's state machine.
     */
    enum SenderState_t {
        IDLE,
        CAD_IN_PROGRESS,
        READY_TO_SEND,
        WAITING_FOR_TX_DONE,
        CYCLE_COMPLETE
    };

    volatile SenderState_t senderState = IDLE; // Public state for SENDER role

    /**
     * @brief Constructor for the WakeUpCoordinator.
     * @param role The role this node will perform (SENDER or RECEIVER).
     */
    WakeUpCoordinator(Role role) : _role(role), _packetCounter(0) {}

    /**
     * @brief Initializes the node with specific timing parameters.
     * @param interval_ms For SENDER, the total duration of one cycle (sleep + wake).
     * @param wake_window_ms For SENDER, the maximum time to stay awake trying to send.
     */
    void begin(uint32_t interval_ms = 10000, uint32_t wake_window_ms = 3000)
    {
        _cycleInterval = interval_ms;
        _wakeWindow = wake_window_ms;

        Serial.printf("INFO: Coordinator starting in %s mode.\n", _role == SENDER ? "SENDER" : "RECEIVER");

        lora_rak4630_init(); // Initialize RAK-specific hardware

        // --- Role-Based Radio and Callback Setup ---
        if (_role == SENDER)
        {
            _radioEvents.CadDone = OnCadDone;
            _radioEvents.TxDone = OnTxDone;
            _radioEvents.TxTimeout = OnTxTimeout;
            Radio.Init(&_radioEvents);
            Radio.SetChannel(LORA_FREQUENCY_HZ);
            Radio.SetTxConfig(MODEM_LORA, LORA_TX_POWER_DBM, 0, LORA_BANDWIDTH,
                              LORA_SPREADING_FACTOR, LORA_CODING_RATE,
                              LORA_LONG_PREAMBLE_SYMBOLS, LORA_FIX_LEN, LORA_CRC_ENABLED,
                              LORA_FREQ_HOP_ON, LORA_HOP_PERIOD, LORA_IQ_INVERTED_ENABLED,
                              LORA_TX_TIMEOUT_MS);
            _lastCycleStartTime = millis(); // Start the timer for the first cycle
        }
        else // RECEIVER
        {
            _radioEvents.RxDone = OnRxDone;
            _radioEvents.RxTimeout = OnRxTimeout;
            _radioEvents.RxError = OnRxError;
            Radio.Init(&_radioEvents);
            Radio.SetChannel(LORA_FREQUENCY_HZ);
            Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                              LORA_CODING_RATE, 0, LORA_LONG_PREAMBLE_SYMBOLS,
                              LORA_SYMB_TIMEOUT, LORA_FIX_LEN, LORA_PAYLOAD_LEN,
                              LORA_CRC_ENABLED, LORA_FREQ_HOP_ON, LORA_HOP_PERIOD,
                              LORA_IQ_INVERTED_ENABLED, LORA_RX_CONTINUOUS);
            Radio.Rx(0); // Start listening continuously
            Serial.println("INFO: Receiver started and is now listening.");
        }
    }

    /**
     * @brief The main execution function. Call this repeatedly in your main loop().
     */
    void run()
    {
        // The run loop is only active for the SENDER role.
        // The RECEIVER role is entirely event-driven via callbacks.
        if (_role != SENDER) {
            return;
        }

        switch (senderState)
        {
        case IDLE:
            if (millis() - _lastCycleStartTime >= _cycleInterval)
            {
                _lastCycleStartTime = millis();
                Serial.printf("\n--- SENDER: Starting new cycle at %lu ms ---\n", _lastCycleStartTime);
                senderState = CAD_IN_PROGRESS;
                Radio.Standby();
                Radio.StartCad();
            }
            break;
        case CAD_IN_PROGRESS:
            if (millis() - _lastCycleStartTime > _wakeWindow)
            {
                Serial.println("WARN: SENDER: Wake window ended. No free channel. Sleeping.");
                Radio.Sleep();
                senderState = CYCLE_COMPLETE;
            }
            break;
        case READY_TO_SEND:
            {
                TimerPacket packet_to_send(_packetCounter, _cycleInterval / 1000, _wakeWindow / 1000);
                uint8_t buffer[TimerPacket::PACKET_SIZE];
                packet_to_send.serialize(buffer);

                // INTENTIONALLY CORRUPT THE PACKET for relay node test
                buffer[TimerPacket::PACKET_SIZE - 1] ^= 0xFF; // Invalidate checksum

                Serial.printf("INFO: SENDER: Sending intentionally INVALID Packet ID: %lu\n", packet_to_send.packetId);
                Radio.Send(buffer, TimerPacket::PACKET_SIZE);
                _packetCounter++;
                senderState = WAITING_FOR_TX_DONE;
            }
            break;
        case WAITING_FOR_TX_DONE:
            // Waiting for OnTxDone or OnTxTimeout callback
            break;
        case CYCLE_COMPLETE:
            senderState = IDLE;
            break;
        }
    }

private:
    Role _role;
    RadioEvents_t _radioEvents;
    uint32_t _cycleInterval;
    uint32_t _wakeWindow;
    uint32_t _lastCycleStartTime;
    uint32_t _packetCounter;
};

// --- Global Pointer for Callbacks ---
extern WakeUpCoordinator* g_Coordinator;

//================================================================//
//                  GLOBAL CALLBACK IMPLEMENTATIONS               //
//================================================================//

// --- SENDER Callbacks ---
inline void OnCadDone(bool channelActivityDetected) {
    if (g_Coordinator == nullptr) return;
    if (channelActivityDetected) {
        Radio.StartCad();
    } else {
        g_Coordinator->senderState = WakeUpCoordinator::READY_TO_SEND;
    }
}

inline void OnTxDone(void) {
    if (g_Coordinator == nullptr) return;
    Serial.println("INFO: SENDER: TX successful!");
    Radio.Sleep();
    g_Coordinator->senderState = WakeUpCoordinator::CYCLE_COMPLETE;
}

inline void OnTxTimeout(void) {
    if (g_Coordinator == nullptr) return;
    Serial.println("ERROR: SENDER: TX Timeout!");
    Radio.Sleep();
    g_Coordinator->senderState = WakeUpCoordinator::CYCLE_COMPLETE;
}

// --- RECEIVER Callbacks ---
inline void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    Serial.println("\n--- RECEIVER: Packet Received! ---");
    Serial.printf("RSSI: %d dBm, SNR: %d, Size: %d bytes\n", rssi, snr, size);

    // Validate packet size first.
    if (size != TimerPacket::PACKET_SIZE) {
        Serial.printf("ERROR: RECEIVER: Invalid packet size. Expected %u, got %u.\n", TimerPacket::PACKET_SIZE, size);
        Radio.Rx(0); // Go back to listening.
        return;
    }

    // Attempt to deserialize and validate checksum.
    TimerPacket received_packet;
    if (received_packet.deserialize(payload)) {
        Serial.println("INFO: RECEIVER: Checksum VALID. Packet details:");
        Serial.printf("  - Packet ID: %lu\n", received_packet.packetId);
        Serial.printf("  - Interval:  %u seconds\n", received_packet.messageInterval_s);
        Serial.printf("  - Wake Window: %u seconds\n", received_packet.wakeWindow_s);
    } else {
        Serial.println("ERROR: RECEIVER: Checksum INVALID. Packet discarded.");
    }

    Radio.Rx(0); // Go back to listening for the next packet.
}

inline void OnRxTimeout(void) {
    Serial.println("WARN: RECEIVER: Timeout!");
    Radio.Rx(0);
}

inline void OnRxError(void) {
    Serial.println("ERROR: RECEIVER: Error!");
    Radio.Rx(0);
}

#endif // WAKE_UP_COORDINATION_H
