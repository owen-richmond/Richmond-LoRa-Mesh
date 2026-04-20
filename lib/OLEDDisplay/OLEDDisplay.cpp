#include "OLEDDisplay.h"

OLEDDisplay::OLEDDisplay()
    : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1) {
    // Member defaults are initialized in the class declaration in OLEDDisplay.h
}

bool OLEDDisplay::begin() {
    Serial.println("OLED: Initializing SSD1315 display...");
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED: Failed to initialize!");
        return false;
    }
    
    Serial.println("OLED: Display initialized successfully!");
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.clearDisplay();
    
    // Splash screen with node info
    display.setTextSize(2);
    display.setCursor(20, 10);
    display.println("LoRa");
    display.setCursor(10, 28);
    display.println("Mesh Node");
    
    display.setTextSize(1);
    display.setCursor(15, 50);
    display.println("Ready to receive!");
    
    display.display();
    delay(1500);
    
    return true;
}

// Quick helper to read battery voltage in millivolts
uint16_t OLEDDisplay::readBatteryMV() {
    // Default ADC resolution for the RAK4630 variant
    const uint32_t adcResolution = 14; // 14-bit
    const uint32_t adcMax = (1u << adcResolution) - 1u;

    // Use A0 as battery sense by default (WB_A0)
    uint32_t raw = (uint32_t)analogRead(A0);
    float vref = 3.3f; // VDD reference

    // Measured voltage (V) at the analog input pin
    float measuredV = ((float)raw / (float)adcMax) * vref;

    // Assume board uses a 2:1 divider for VBAT to ADC
    const float divider = 2.0f;
    float batteryV = measuredV * divider;

    return (uint16_t)(batteryV * 1000.0f + 0.5f);
}

// Return battery percentage using cached reads.
uint8_t OLEDDisplay::getBatteryPercent() {
    batteryUpdateCounter++;
    if (batteryUpdateCounter >= BATTERY_UPDATE_INTERVAL) {
        batteryUpdateCounter = 0;
        uint16_t mv = readBatteryMV();
        // map 3000 mV -> 0% and 4200 mV -> 100%
        int pct = (int)((mv - 3000u) * 100 / (4200u - 3000u));
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        batteryPct = (uint8_t)pct;
    }
    return batteryPct;
}

// Helper: convert mV to percentage (approximate), 3300=0%, 4200=100%
uint8_t OLEDDisplay::readBatteryPercentageRaw() {
    uint16_t mv = readBatteryMV();
    // Simple linear mapping 3300 -> 0%, 4200 -> 100%
    const uint16_t minMv = 3300;
    const uint16_t maxMv = 4200;
    if (mv <= minMv) return 0;
    if (mv >= maxMv) return 100;
    return (uint8_t)((mv - minMv) * 100 / (maxMv - minMv));
}

void OLEDDisplay::showPacketReceived(uint16_t packetID, uint16_t senderID, int16_t rssi, int8_t snr, uint8_t temp, uint8_t humidity) {
    // Do minimal work in-case this is called from an ISR: just set pending vars
    pendingPacketID = packetID;
    pendingSenderID = senderID;
    pendingRSSI = rssi;
    pendingSNR = snr;
    pendingTemp = temp;
    pendingHumidity = humidity;
    pendingSmileyEvent = true;
}

void OLEDDisplay::setSenderMode(bool isSender) {
    isSenderMode = isSender;
}

void OLEDDisplay::setSenderNodeInfo(uint16_t nodeId, uint8_t temp, uint8_t accelMagnitude) {
    _myNodeId = nodeId;
    currentTemp = temp;
    _currentAcceleration = accelMagnitude;
}

void OLEDDisplay::showPacketSent(uint16_t packetID, uint16_t destinationID) {
    pendingSendPacketID = packetID;
    pendingSendDestinationID = destinationID;
    pendingSendEvent = true;
    // Mirror minimal packet details so the smiley overlay shows source + packetID on send
    pendingPacketID = packetID;
    pendingSenderID = _myNodeId;
    // Show smiley for senders as well (flash the overlay)
    pendingSmileyEvent = true;
}

void OLEDDisplay::showBLEConnected() {
    // ISR-safe: set pending BLE event only
    pendingBLEEvent = true;
    pendingBLEConnectedState = true;
}

void OLEDDisplay::showBLEDisconnected() {
    // ISR-safe: set pending BLE event only
    pendingBLEEvent = true;
    pendingBLEConnectedState = false;
}

void OLEDDisplay::update() {
    uint32_t now = millis();

    // Copy any pending events set by ISR-safe functions (ensure minimal critical section)
    if (pendingSmileyEvent) {
        noInterrupts();
        pendingSmileyEvent = false;
        currentPacketID = pendingPacketID;
        currentSenderID = pendingSenderID;
        currentRSSI = pendingRSSI;
        currentSNR = pendingSNR;
        currentTemp = pendingTemp;
        currentHumidity = pendingHumidity;
        smileyActive = true;
        lastPacketTime = now; // timestamp in main loop
        interrupts();
    }

    if (pendingBLEEvent) {
        noInterrupts();
        pendingBLEEvent = false;
        bleConnected = pendingBLEConnectedState;
        bleStatusActive = true;
        bleStatusTime = now; // timestamp in main loop
        interrupts();
    }

    // Sync pending send event (sender nodes should show SENDING state)
    if (pendingSendEvent) {
        noInterrupts();
        pendingSendEvent = false;
        // copy details to current pointer fields
        currentSendPacketID = pendingSendPacketID;
        currentSendDestinationID = pendingSendDestinationID;
        sendActive = true;
        lastSendTime = now;
        interrupts();
    }

    // Dynamic throttle: faster during active events
    uint32_t throttleMs = (smileyActive || bleStatusActive || sendActive) ? 100 : 500;

    // Update display on throttle or when events triggered
    if (now - lastUpdate < throttleMs && !smileyActive && !bleStatusActive) return;

    lastUpdate = now;
    
    // Clear and prepare display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false); // avoid auto-wrapping
    
    // Battery percentage (cached) - draw at top-right aligned with LISTENING y
    uint8_t battPct = getBatteryPercent();
    int battX = SCREEN_WIDTH - 60; // leave room for BATT:100% without wrapping (9 chars * 6px)
    int battY = 5; // slightly lower to align with LISTENING
    display.setCursor(battX, battY);
    display.printf("BATT:%3u%%", (unsigned int)battPct);

    // Priority 1: Show BLE status (3 seconds)
    if (bleStatusActive && (now - bleStatusTime < 3000)) {
        // Single border around the edges of screen
        display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

        // Battery on top-right (same spot)
        display.setCursor(battX, battY);
        display.printf("BATT:%3u%%", (unsigned int)battPct);

        // Title left/top
        display.setTextSize(1);
        display.setCursor(8, 6);
        display.println("Bluetooth");

        // Centered status message - slightly larger (size 2)
        display.setTextSize(2);
        const char *msg = bleConnected ? "Connected" : "Lost";
        int msgWidth = 6 * strlen(msg) * 2; // 6px per char, size 2
        int msgX = (SCREEN_WIDTH - msgWidth) / 2;
        display.setCursor(msgX, 28);
        display.println(msg);
    }
    // Priority 2: Show smiley face (2 seconds after packet) - show for both sender and receiver nodes
    else if (((smileyActive && (now - lastPacketTime < 2000)) || (sendActive && (now - lastSendTime < 2000)))) {
        // Draw cute smiley face (smaller)
        drawSmileyFace();
        
        // Show sender/destination below smiley (font size 1)
        display.setTextSize(1);
        if (sendActive && (now - lastSendTime < 2000)) {
            display.setCursor(5, 48);
            display.printf("To Node: %u", currentSendDestinationID);
            display.setCursor(5, 57);
            display.printf("Packet ID: %u", currentSendPacketID);
        } else {
            display.setCursor(5, 48);
            display.printf("Node: %u", currentSenderID);
            display.setCursor(5, 57);
            display.printf("RSSI: %d dBm", currentRSSI);
        }
    } 
    // Priority 3: Normal status display
    else {
        smileyActive = false;
        bleStatusActive = false;
        if (sendActive && (now - lastSendTime < 3000)) {
            // show simple 'PACKET SENT' feedback at left
            display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
            display.setTextSize(1);
            display.setCursor(8, 5);
            display.println("SENDING");

            // Battery on top-right
            display.setCursor(battX, battY);
            display.printf("BATT:%3u%%", (unsigned int)battPct);

            // Show Send info: from node, current temp, current acceleration
            display.setTextSize(1);
            display.setCursor(5, 22);
            display.printf("From Node: %u", _myNodeId);
            display.setCursor(5, 34);
            display.printf("Temp: %u C", currentTemp);
            display.setCursor(5, 46);
            display.printf("Accel: %u m/s2", (uint8_t)_currentAcceleration);
        } else {
            sendActive = false;
            // Status display - cleaner layout for listening
            display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
            display.setTextSize(1);
            display.setCursor(8, 5);
            if (isSenderMode) display.println("SENDING"); else display.println("LISTENING");

            // Battery on top-right
            display.setCursor(battX, battY);
            display.printf("BATT:%3u%%", (unsigned int)battPct);

            // Divider line
            display.drawLine(0, 15, 128, 15, SSD1306_WHITE);

            // Last packet info - size 1 for better spacing
            display.setTextSize(1);
            if (isSenderMode) {
                // For sender nodes, show self info instead of last-received details
                display.setCursor(5, 22);
                display.printf("From Node: %u", _myNodeId);
                display.setCursor(5, 34);
                display.printf("Temp: %u C", currentTemp);
                display.setCursor(5, 46);
                display.printf("Accel: %u m/s2", (uint8_t)_currentAcceleration);
            } else {
                display.setCursor(5, 22);
                display.printf("From Node: %u", currentSenderID);
                display.setCursor(5, 34);
                display.printf("RSSI: %d dBm", currentRSSI);
                display.setCursor(5, 46);
                display.printf("Packet ID: %u", currentPacketID);
            }
        }
    }
    
    // Send to display with timeout protection
    uint32_t displayStart = millis();
    display.display();
    uint32_t displayTime = millis() - displayStart;
    
    // Check if display call took too long (I2C hang indicator)
    if (displayTime > 50) {
        Serial.printf("WARN: OLED display() took %lu ms\n", displayTime);
    }
}

void OLEDDisplay::drawSmileyFace() {
    // ASCII-style smiley face ":)" centered at (44, 22) approximately
    const int cx = 44;
    const int cy = 22;
    // No white background - user requested only ":)" text

    // Use text size 2 so it fits inside the face region
    const int textSize = 4;
    display.setTextSize(textSize);
    // Draw the smiley text in white (on black background)
    display.setTextColor(SSD1306_WHITE);

    // Center the ":)" inside the face region with 5px left offset
    const int charWidth = 6 * textSize; // 6px per char times text size
    const int textWidth = charWidth * 2; // two characters ":)"
    const int textHeight = 8 * textSize; // char height
    int textX = cx - (textWidth / 2) - 5;  // shift left 5px
    int textY = cy - (textHeight / 2);

    display.setCursor(textX, textY);
    display.print(":)");

    // Reset text to white and size to 1 for subsequent drawing
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setTextSize(1);
}

// Wrapper functions for BLE module
void oled_show_ble_connected() {
    extern OLEDDisplay* g_OLEDDisplay;
    if (g_OLEDDisplay) {
        g_OLEDDisplay->showBLEConnected();
    }
}

void oled_show_ble_disconnected() {
    extern OLEDDisplay* g_OLEDDisplay;
    if (g_OLEDDisplay) {
        g_OLEDDisplay->showBLEDisconnected();
    }
}

void oled_show_packet_sent(uint16_t packetID, uint16_t destID) {
    extern OLEDDisplay* g_OLEDDisplay;
    if (g_OLEDDisplay) {
        g_OLEDDisplay->showPacketSent(packetID, destID);
    }
}
