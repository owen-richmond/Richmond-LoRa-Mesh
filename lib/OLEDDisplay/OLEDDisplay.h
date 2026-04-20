#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/**
 * @class OLEDDisplay
 * @brief Manages the 128x64 SSD1315 OLED display with I2C connection
 * @details Displays mesh network status, received messages, battery, and signal strength
 */
class OLEDDisplay {
public:
    /**
     * @brief Constructor for the OLED display
     */
    OLEDDisplay();

    /**
     * @brief Initialize the OLED display
     * @return true if initialization successful, false otherwise
     */
    bool begin();

    /**
     * @brief Show packet received with cute smiley face
     * @param packetID ID of the packet
     * @param senderID ID of the sending node
     * @param rssi Signal strength in dBm
     * @param snr Signal-to-noise ratio
     * @param temp Temperature in °C (0-99)
     * @param humidity Humidity in % (0-99)
     */
    void showPacketReceived(uint16_t packetID, uint16_t senderID, int16_t rssi, int8_t snr, uint8_t temp, uint8_t humidity);

    /**
     * @brief Show Bluetooth connected message
     */
    void showBLEConnected();

    /**
     * @brief Show Bluetooth disconnected message
     */
    void showBLEDisconnected();

    /**
     * @brief Show a packet-sent screen (sender nodes)
     */
    void showPacketSent(uint16_t packetID, uint16_t destinationID);
    void setSenderMode(bool isSender);
    
    /**
     * @brief Set sender node's own ID and current sensor data for SENDING screen
     */
    void setSenderNodeInfo(uint16_t nodeId, uint8_t temp, uint8_t accelMagnitude);
    
    /**
     * @brief Update the display (call from main loop)
     */
    void update();

private:
    Adafruit_SSD1306 display;
    static const int SCREEN_WIDTH = 128;
    static const int SCREEN_HEIGHT = 64;
    static const int OLED_ADDR = 0x3C;

    uint32_t lastUpdate = 0;
    uint32_t lastPacketTime = 0;
    uint32_t bleStatusTime = 0;
    bool smileyActive = false;
    bool bleStatusActive = false;
    bool bleConnected = false;
    bool isSenderMode = false; // shows 'SENDING' on normal screen for sender nodes
    bool sendActive = false;
    uint32_t lastSendTime = 0;

    // Current packet data
    uint16_t currentPacketID = 0;
    uint16_t currentSenderID = 0;
    int16_t currentRSSI = 0;
    int8_t currentSNR = 0;
    uint8_t currentTemp = 0;
    uint8_t currentHumidity = 0;
    // Current send data
    uint16_t currentSendPacketID = 0;
    uint16_t currentSendDestinationID = 0;

    // Sender node info (for displaying on SENDING screen)
    uint16_t _myNodeId = 0;
    uint8_t _currentAcceleration = 0;  // cached accel magnitude for SENDING screen

    // Battery caching
    static const uint8_t BATTERY_UPDATE_INTERVAL = 4; // read every 4th update
    uint8_t batteryUpdateCounter = BATTERY_UPDATE_INTERVAL - 1;
    uint8_t batteryPct = 0;

    // Read VBAT via ADC and return millivolts
    uint16_t readBatteryMV();
    // Get battery percentage with caching
    uint8_t getBatteryPercent();
    // Raw percentage calculation from mV (no caching)
    uint8_t readBatteryPercentageRaw();

    /**
     * @brief Draw a cute smiley face
     */
    void drawSmileyFace();

    // Pending event flags and data (set from ISR-safe handlers)
    volatile uint16_t pendingPacketID = 0;
    volatile uint16_t pendingSenderID = 0;
    volatile int16_t pendingRSSI = 0;
    volatile int8_t pendingSNR = 0;
    volatile uint8_t pendingTemp = 0;
    volatile uint8_t pendingHumidity = 0;
    volatile bool pendingSmileyEvent = false;

    volatile bool pendingBLEEvent = false;
    volatile bool pendingBLEConnectedState = false;

    volatile bool pendingSendEvent = false;
    volatile uint16_t pendingSendPacketID = 0;
    volatile uint16_t pendingSendDestinationID = 0;
};

// Thin wrappers so other modules (e.g., ble_uart) can notify OLED without needing the class type
void oled_show_ble_connected();
void oled_show_ble_disconnected();
void oled_show_packet_sent(uint16_t packetID, uint16_t destID);

// Provide extern declaration for the global pointer (main.cpp defines it)
extern OLEDDisplay* g_OLEDDisplay;
