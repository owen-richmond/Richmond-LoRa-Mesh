/**
 * @file main.cpp
 * @brief Main application file for the LoRa Mesh Node.
 * @version 2.0
 * @details This file initializes and runs a mesh node. The node's behavior
 * (sensor vs. mesh-node forwarder) is determined by compile-time flags
 * set in the platformio.ini file.
 */

#ifdef SLEEP_MODE_TEST
#include <Arduino.h>
#include <SX126x-Arduino.h>
#include <nrf.h>
#include "ProjectConfig.h"

#ifndef LED_BUILTIN
#define LED_BUILTIN PIN_LED1
#endif

#ifndef LED_STATE_ON
#define LED_STATE_ON HIGH
#endif

static void set_led(bool on)
{
    digitalWrite(LED_BUILTIN, on ? LED_STATE_ON : !LED_STATE_ON);
}

static RadioEvents_t g_radio_events = {};

static void OnTxDone(void) {}
static void OnTxTimeout(void) {}
static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    (void)payload;
    (void)size;
    (void)rssi;
    (void)snr;
}
static void OnRxTimeout(void) {}
static void OnRxError(void) {}

static bool g_radio_initialized = false;

static void radio_configure_rx()
{
    Radio.SetChannel(LORA_FREQUENCY_HZ);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODING_RATE, 0, LORA_LONG_PREAMBLE_SYMBOLS,
                      LORA_SYMB_TIMEOUT, LORA_FIX_LEN, LORA_PAYLOAD_LEN,
                      LORA_CRC_ENABLED, LORA_FREQ_HOP_ON, LORA_HOP_PERIOD,
                      LORA_IQ_INVERTED_ENABLED, LORA_RX_CONTINUOUS);
}

static void radio_on()
{
    if (!g_radio_initialized) {
        lora_rak4630_init();
        g_radio_events.TxDone = OnTxDone;
        g_radio_events.TxTimeout = OnTxTimeout;
        g_radio_events.RxDone = OnRxDone;
        g_radio_events.RxTimeout = OnRxTimeout;
        g_radio_events.RxError = OnRxError;
        Radio.Init(&g_radio_events);
        radio_configure_rx();
        g_radio_initialized = true;
    }
    Radio.Rx(0);
}

static void radio_off()
{
    if (g_radio_initialized) {
        Radio.Sleep();
    }
}

static void deep_sleep_ms(uint32_t duration_ms)
{
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < duration_ms) {
        // Clear event register, then sleep until next interrupt (e.g., SysTick)
        __SEV();
        __WFE();
        __WFE();
    }
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    radio_on();
    set_led(true);
}

void loop()
{
    static uint32_t phase_start_ms = 0;
    static uint8_t phase = 0; // 0=on, 1=deep-sleep/off
    const uint32_t phase_duration_ms = 3000;

    if (phase_start_ms == 0) {
        phase_start_ms = millis();
        set_led(true);
    }

    if (millis() - phase_start_ms >= phase_duration_ms) {
        if (phase == 0) {
            phase = 1;
            phase_start_ms = millis();
            radio_off();
            set_led(false);
            deep_sleep_ms(phase_duration_ms);
            phase = 0;
            phase_start_ms = millis();
            radio_on();
            set_led(true);
        }
    }
}
#else
#include <Arduino.h>
#include <nrf.h>
#include <Wire.h>
#include "MeshNode.h"
#include "SensorManager.h"
#include "ble_uart.h"
#include "OLEDDisplay.h"
#include "ProjectConfig.h"
#ifdef MESH_METRICS_ENABLED
#include "NodeMetrics.h"
static NodeMetrics metrics;
#endif

#if defined(SENDER_NODE) && !defined(SENSOR_NODE)
#define SENSOR_NODE
#endif

#ifndef EXPERIMENT_INTERVAL_MS
#define EXPERIMENT_INTERVAL_MS 5000
#endif

#ifndef SENSOR_RUNTIME_ENABLED
#define SENSOR_RUNTIME_ENABLED 1
#endif

#ifndef BLE_RUNTIME_ENABLED
#define BLE_RUNTIME_ENABLED 1
#endif

#ifndef SENSOR_TRAFFIC_ENABLED
#ifdef SENDER_TRAFFIC_ENABLED
#define SENSOR_TRAFFIC_ENABLED SENDER_TRAFFIC_ENABLED
#else
#define SENSOR_TRAFFIC_ENABLED 1
#endif
#endif

#ifndef SENSOR_PACKET_INTERVAL_MS
#ifdef SENDER_PACKET_INTERVAL_MS
#define SENSOR_PACKET_INTERVAL_MS SENDER_PACKET_INTERVAL_MS
#else
#define SENSOR_PACKET_INTERVAL_MS 10000
#endif
#endif

#ifndef SENSOR_TX_QUEUE_MAX_BEFORE_SKIP
#define SENSOR_TX_QUEUE_MAX_BEFORE_SKIP 2
#endif

#ifndef SENSOR_TX_STALE_BACKOFF_ENABLED
#define SENSOR_TX_STALE_BACKOFF_ENABLED 1
#endif

#ifndef SENSOR_TX_STALE_AFTER_MS
#define SENSOR_TX_STALE_AFTER_MS 60000
#endif

#ifndef SENSOR_TX_STALE_MULTIPLIER
#define SENSOR_TX_STALE_MULTIPLIER 3
#endif

#ifndef SENSOR_TX_SKIP_LOG_INTERVAL_MS
#define SENSOR_TX_SKIP_LOG_INTERVAL_MS 15000
#endif

#ifndef LED_GREEN
#define LED_GREEN LED_BUILTIN
#endif

#ifndef LED_STATE_ON
#define LED_STATE_ON HIGH
#endif

#ifndef GREEN_LED_BLINK_PERIOD_MS
#define GREEN_LED_BLINK_PERIOD_MS 1000
#endif

#ifndef GREEN_LED_BLINK_ON_MS
#define GREEN_LED_BLINK_ON_MS 120
#endif

#ifndef GREEN_LED_HEARTBEAT_ENABLED
#define GREEN_LED_HEARTBEAT_ENABLED 1
#endif

#ifndef SERIAL_DEBUG_INTERVAL_MS
#define SERIAL_DEBUG_INTERVAL_MS 10000
#endif

#ifndef OLED_FORCE_OFF_COMMAND
#define OLED_FORCE_OFF_COMMAND 0
#endif

#ifndef WISBLOCK_SLOT_POWER_CUT
#define WISBLOCK_SLOT_POWER_CUT 0
#endif

#ifndef WISBLOCK_SLOT_POWER_PIN
#define WISBLOCK_SLOT_POWER_PIN WB_IO2
#endif

#ifndef WISBLOCK_SLOT_POWER_ACTIVE_HIGH
#define WISBLOCK_SLOT_POWER_ACTIVE_HIGH 1
#endif

#ifndef WISBLOCK_SLOT_POWER_OFF_AT_BOOT
#define WISBLOCK_SLOT_POWER_OFF_AT_BOOT 0
#endif

#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif

#ifndef MAIN_VERBOSE_SETUP_LOGS
#define MAIN_VERBOSE_SETUP_LOGS 0
#endif

#ifndef MAIN_PERIODIC_STATUS_LOGS
#define MAIN_PERIODIC_STATUS_LOGS 1
#endif

#ifndef MCU_IDLE_SLEEP_ENABLED
#define MCU_IDLE_SLEEP_ENABLED 1
#endif

#ifndef MCU_IDLE_SLEEP_MAX_MS
#define MCU_IDLE_SLEEP_MAX_MS 50
#endif

#ifndef MCU_IDLE_SLEEP_WHEN_BLE_CONNECTED
#define MCU_IDLE_SLEEP_WHEN_BLE_CONNECTED 0
#endif

#ifndef MCU_IDLE_SLEEP_USE_RTC_DELAY
#define MCU_IDLE_SLEEP_USE_RTC_DELAY 0
#endif

#ifndef MCU_SYSTEMOFF_ENABLED
#define MCU_SYSTEMOFF_ENABLED 0
#endif

#ifndef MCU_SYSTEMOFF_IDLE_MIN_MS
#define MCU_SYSTEMOFF_IDLE_MIN_MS 3000
#endif

#ifndef MCU_SYSTEMOFF_WAKE_PIN
#define MCU_SYSTEMOFF_WAKE_PIN 47
#endif

#ifndef MCU_SYSTEMOFF_WAKE_LOGIC_HIGH
#define MCU_SYSTEMOFF_WAKE_LOGIC_HIGH 1
#endif

#ifndef FORCE_BLE_ON_RECEIVER
#define FORCE_BLE_ON_RECEIVER 1
#endif

static void setGreenLed(bool on)
{
    digitalWrite(LED_GREEN, on ? LED_STATE_ON : !LED_STATE_ON);
}

static void cpuIdleSleepMs(uint32_t durationMs)
{
    if (durationMs == 0) {
        return;
    }

#if MCU_IDLE_SLEEP_USE_RTC_DELAY
    // On nRF52 FreeRTOS, delay() blocks the task and uses tickless RTC wakeups.
    delay(durationMs);
#else
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < durationMs) {
        __SEV();
        __WFE();
        __WFE();
    }
#endif
}

static void setWisBlockSlotPower(bool on)
{
#if WISBLOCK_SLOT_POWER_CUT
    pinMode(WISBLOCK_SLOT_POWER_PIN, OUTPUT);
    const bool activeLevel = (WISBLOCK_SLOT_POWER_ACTIVE_HIGH != 0);
    const bool pinHigh = activeLevel ? on : !on;
    digitalWrite(WISBLOCK_SLOT_POWER_PIN, pinHigh ? HIGH : LOW);
#else
    (void)on;
#endif
}

static void forceOledOff()
{
#if OLED_FORCE_OFF_COMMAND
    auto oledWriteCommand1 = [](uint8_t command) {
        Wire.beginTransmission((uint8_t)OLED_I2C_ADDR);
        Wire.write((uint8_t)0x00); // command stream
        Wire.write(command);
        (void)Wire.endTransmission();
    };
    auto oledWriteCommand2 = [](uint8_t command1, uint8_t command2) {
        Wire.beginTransmission((uint8_t)OLED_I2C_ADDR);
        Wire.write((uint8_t)0x00); // command stream
        Wire.write(command1);
        Wire.write(command2);
        (void)Wire.endTransmission();
    };
    // SSD1315 is command-compatible with SSD1306 for display off.
    oledWriteCommand1(0xAE);       // DISPLAYOFF
    oledWriteCommand2(0x8D, 0x10); // Charge pump OFF (SSD1306-compatible)
#endif
}

#if MCU_SYSTEMOFF_ENABLED
static void enterSystemOff()
{
    setGreenLed(false);
    forceOledOff();
    setWisBlockSlotPower(false);
    Radio.Sleep();
    Serial.flush();
    delay(5);
    systemOff((uint32_t)MCU_SYSTEMOFF_WAKE_PIN, (uint8_t)(MCU_SYSTEMOFF_WAKE_LOGIC_HIGH ? 1 : 0));
    // Should never return. Fallback if systemOff is stubbed out.
    NRF_POWER->SYSTEMOFF = 1;
}
#endif

// Create instances
MeshNode meshNode(DEVICE_ID, NETWORK_ID);
SensorManager sensorManager;
#if OLED_ENABLED
OLEDDisplay oledDisplay;
#endif

// Global pointer to the node instance for the callbacks
MeshNode* g_MeshNode = nullptr;
OLEDDisplay* g_OLEDDisplay = nullptr;

// --- Main Setup and Loop ---
void setup()
{
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_GREEN, OUTPUT);
    setGreenLed(false);

    Serial.printf("\nSETUP: node=%u network=%u\n", (unsigned)DEVICE_ID, (unsigned)NETWORK_ID);
#if MAIN_VERBOSE_SETUP_LOGS
    Serial.printf("SETUP: green LED heartbeat on pin %u\n", (unsigned)LED_GREEN);
#endif

    // Assign the global pointer
    g_MeshNode = &meshNode;
#if OLED_ENABLED
    g_OLEDDisplay = &oledDisplay;
#else
    g_OLEDDisplay = nullptr;
#endif

    Wire.begin();
    forceOledOff();
#if WISBLOCK_SLOT_POWER_CUT
    setWisBlockSlotPower(WISBLOCK_SLOT_POWER_OFF_AT_BOOT == 0);
#else
    setWisBlockSlotPower(true);
#endif

    // Initialize sensors
#if SENSOR_RUNTIME_ENABLED
    sensorManager.begin();
#else
    Serial.println("SETUP: sensors disabled (low-power build)");
#endif

    // Check sensor status immediately
    bool sensorsOk = sensorManager.hasSensorsAttached();
    Serial.printf("SETUP: sensors %s\n", sensorsOk ? "attached" : "missing");
    
    // Initialize BLE only on non-sensor nodes
    char bleName[32];
#ifdef SENSOR_NODE
    snprintf(bleName, sizeof(bleName), "Sensor%u", (unsigned)DEVICE_ID);
#else
    snprintf(bleName, sizeof(bleName), "MeshNode%u", (unsigned)DEVICE_ID);
#endif
#if BLE_RUNTIME_ENABLED
#ifdef SENSOR_NODE
#if MAIN_VERBOSE_SETUP_LOGS
    Serial.println("SETUP: sensor role, BLE disabled");
#endif
    ble_init(bleName, false);  // Disable BLE on sensor
#else
    // Optional: keep BLE always enabled for receiver/forwarder nodes while testing.
#if MAIN_VERBOSE_SETUP_LOGS
    Serial.println("SETUP: receiver/forwarder role, BLE enabled");
#endif
#if FORCE_BLE_ON_RECEIVER
    ble_init(bleName, true);
#else
    ble_init(bleName, !sensorsOk);
#endif
#endif
#else
    (void)bleName;
    Serial.println("BLE: disabled (low-power build)");
#endif

    // Initialize the node
    meshNode.begin();
    
#if OLED_ENABLED
    // Initialize OLED display
#if MAIN_VERBOSE_SETUP_LOGS
    Serial.println("SETUP: initializing OLED");
#endif
    if (oledDisplay.begin()) {
        Serial.println("OLED: Display initialized successfully");
    } else {
        Serial.println("OLED: Failed to initialize display");
    }

    // Configure display for sensor/mesh-node mode
#ifdef SENSOR_NODE
    oledDisplay.setSenderMode(true);
    // Initialize sensor-node info immediately so it displays on startup
    oledDisplay.setSenderNodeInfo(DEVICE_ID, 0, 0);
#else
    oledDisplay.setSenderMode(false);
#endif
#endif

    Serial.println("SETUP: complete\n");
}

void loop()
{
    const uint32_t now = millis();
    (void)now;

    // Keep BLE responsive if enabled
#if BLE_RUNTIME_ENABLED
    ble_poll();
#endif

    // Heartbeat LED: short pulse every second.
#if GREEN_LED_HEARTBEAT_ENABLED
    static uint32_t lastBlinkStartMs = 0;
    static bool ledIsOn = false;
    if (!ledIsOn && (now - lastBlinkStartMs >= GREEN_LED_BLINK_PERIOD_MS)) {
        lastBlinkStartMs = now;
        ledIsOn = true;
        setGreenLed(true);
    }
    if (ledIsOn && (now - lastBlinkStartMs >= GREEN_LED_BLINK_ON_MS)) {
        ledIsOn = false;
        setGreenLed(false);
    }
#endif

#if OLED_ENABLED
    // Update OLED display
    oledDisplay.update();
#endif

    // Core mesh runtime (wake scheduling, TX queue, RX processing)
    meshNode.run();

#ifdef MESH_METRICS_ENABLED
    metrics.tick(meshNode);
#endif

    // Periodic serial status for runtime debugging.
    static uint32_t lastDebugLogMs = 0;
#if MAIN_PERIODIC_STATUS_LOGS
    if (now - lastDebugLogMs >= SERIAL_DEBUG_INTERVAL_MS) {
        lastDebugLogMs = now;
        Serial.printf("STATUS: uptime=%lu ms, lastPacket=%u, lastPayloadLen=%u, q=%u, dupDrop=%lu, fwdQ=%lu, fwdDrop=%lu, cadBusy=%lu, scRetry=%lu, fwdWin=%u, load=%u, nbrs=%u, rssi=%d, orphan=%u\n",
                      (unsigned long)now,
                      meshNode.getLastProcessedPacketID(),
                      meshNode.getLastPayloadLen(),
                      (unsigned)meshNode.getTxQueueDepth(),
                      (unsigned long)meshNode.getDuplicateDropCount(),
                      (unsigned long)meshNode.getForwardQueuedCount(),
                      (unsigned long)meshNode.getForwardDropCount(),
                      (unsigned long)meshNode.getCadBusyCount(),
                      (unsigned long)meshNode.getSleepControlRetryCount(),
                      (unsigned)meshNode.getForwardWindowCount(),
                      (unsigned)meshNode.getLoadScore(),
                      (unsigned)meshNode.getNeighborCount(),
                      (int)meshNode.getLastRxRssi(),
                      (unsigned)meshNode.getSyncOrphan());
    }
#else
    (void)lastDebugLogMs;
#endif

    // Print any received sensor data from SENSOR packets
    if (meshNode.getLastPayloadLen() > 0) {
        const uint8_t* lastPayload = meshNode.getLastPayload();
        uint16_t originId = meshNode.getLastOriginalSenderID();
        // Print to serial/BLE only (OLED removed for now)
        sensorManager.printReceivedPayload(lastPayload, meshNode.getLastPayloadLen(), originId);

        // Clear payload so we don't process it repeatedly
        meshNode.clearLastPayload();
    }

    // The core logic is event-driven through radio callbacks (OnRxDone, etc.).
    // The loop is only used for nodes that need to initiate sending.

#ifdef SENSOR_NODE
#if SENSOR_TRAFFIC_ENABLED
    // For sensor nodes, continuously update display with fresh sensor data
    static uint32_t lastDisplayUpdateTime = 0;
    if (millis() - lastDisplayUpdateTime > 2000) {
        lastDisplayUpdateTime = millis();
        
        if (sensorManager.hasSensorsAttached()) {
            // Get fresh sensor readings
            uint8_t payload[21];
#if OLED_ENABLED
            size_t payloadLen = sensorManager.serializePayload(payload, sizeof(payload));
            if (payloadLen >= 17 && payloadLen <= sizeof(payload)) {
                // Extract temp and accel from payload using memcpy to respect endianess
                int16_t temp_scaled;
                memcpy(&temp_scaled, &payload[1], 2);
                int temp_c = (int)((float)temp_scaled / 100.0f + 0.5f);

                int16_t accel_x = 0, accel_y = 0, accel_z = 0;
                memcpy(&accel_x, &payload[11], 2);
                memcpy(&accel_y, &payload[13], 2);
                memcpy(&accel_z, &payload[15], 2);

                float ax = (float)accel_x / 1000.0f * 9.8f;
                float ay = (float)accel_y / 1000.0f * 9.8f;
                float az = (float)accel_z / 1000.0f * 9.8f;
                float accel_mag_f = sqrtf(ax*ax + ay*ay + az*az);
                uint8_t accel_mag = (uint8_t)((accel_mag_f) + 0.5f);
                if (accel_mag > 99) accel_mag = 99;

                // Update display with fresh data
                oledDisplay.setSenderNodeInfo(DEVICE_ID, (uint8_t)temp_c, accel_mag);
            }
#else
            (void)sensorManager.serializePayload(payload, sizeof(payload));
#endif
        }
    }
    
    // If this is a designated sensor node, send packets every interval.
    static uint32_t lastSendTime = 0;
    static uint32_t lastTxSkipLogMs = 0;
    uint32_t effectiveSendIntervalMs = (uint32_t)SENSOR_PACKET_INTERVAL_MS;
#if SENSOR_TX_STALE_BACKOFF_ENABLED
    if ((now >= (uint32_t)SENSOR_TX_STALE_AFTER_MS) &&
        !meshNode.hasRecentMeshRx((uint32_t)SENSOR_TX_STALE_AFTER_MS)) {
        const uint32_t staleMultiplier = (uint32_t)SENSOR_TX_STALE_MULTIPLIER;
        if (staleMultiplier > 1 && effectiveSendIntervalMs <= (UINT32_MAX / staleMultiplier)) {
            effectiveSendIntervalMs *= staleMultiplier;
        }
    }
#endif

    if (now - lastSendTime > effectiveSendIntervalMs) {
#if SENSOR_TX_QUEUE_MAX_BEFORE_SKIP > 0
        if (meshNode.getTxQueueDepth() >= (uint8_t)SENSOR_TX_QUEUE_MAX_BEFORE_SKIP) {
            if ((now - lastTxSkipLogMs) >= (uint32_t)SENSOR_TX_SKIP_LOG_INTERVAL_MS) {
                lastTxSkipLogMs = now;
                Serial.printf("TX: skip app packet (queue depth=%u)\n",
                              (unsigned)meshNode.getTxQueueDepth());
            }
            lastSendTime = now;
        } else
#endif
        {
            lastSendTime = now;

            // If we have sensors, send a SENSOR packet with data
            if (sensorManager.hasSensorsAttached()) {
                Serial.println("TX: sensor packet");
                sensorManager.readAndPrintSensors();

                // Serialize sensor data into compact 21-byte payload
                uint8_t payload[21];
                size_t payloadLen = sensorManager.serializePayload(payload, sizeof(payload));

                if (payloadLen > 0 && payloadLen <= sizeof(payload)) {
                    // Send the packet (MeshNode will call showPacketSent with correct packet ID)
                    meshNode.sendSensorPacket(DESTINATION_ID, payload, (uint8_t)payloadLen);
#ifdef MESH_METRICS_ENABLED
                    metrics.onAppTx();
#endif

#if OLED_ENABLED
                    // Update sensor node info for SENDING screen display using properly parsed values
                    if (payloadLen >= 17) {
                        int16_t temp_scaled;
                        memcpy(&temp_scaled, &payload[1], 2);
                        int temp_c = (int)((float)temp_scaled / 100.0f + 0.5f);

                        int16_t accel_x = 0, accel_y = 0, accel_z = 0;
                        memcpy(&accel_x, &payload[11], 2);
                        memcpy(&accel_y, &payload[13], 2);
                        memcpy(&accel_z, &payload[15], 2);

                        float ax = (float)accel_x / 1000.0f * 9.8f;
                        float ay = (float)accel_y / 1000.0f * 9.8f;
                        float az = (float)accel_z / 1000.0f * 9.8f;
                        float accel_mag_f = sqrtf(ax*ax + ay*ay + az*az);
                        uint8_t accel_mag = (uint8_t)((accel_mag_f) + 0.5f);
                        if (accel_mag > 99) accel_mag = 99;

                        oledDisplay.setSenderNodeInfo(DEVICE_ID, (uint8_t)temp_c, accel_mag);
                    }
#endif
                }
            } else {
                // No sensors attached, send a coordination packet for mesh routing/testing
                Serial.println("TX: coordination packet");
                meshNode.sendCoordinationPacket(DESTINATION_ID);
#ifdef MESH_METRICS_ENABLED
                metrics.onAppTx();
#endif
            }
        }
    }

#ifdef EXPERIMENT_MODE
    // Optional experiment traffic for RTT/delivery logging
    static uint32_t lastExperimentSend = 0;
    if (millis() - lastExperimentSend > EXPERIMENT_INTERVAL_MS) {
        lastExperimentSend = millis();
        meshNode.queueExperimentPacket(DESTINATION_ID);
    }
#endif
#else
    // Keep sensor node mostly idle in low-power profiles.
    // No periodic application traffic is generated in this mode.
#endif
#endif

#if MCU_IDLE_SLEEP_ENABLED
    uint32_t idleSleepMs = meshNode.getIdleSleepBudgetMs();
#if MCU_IDLE_SLEEP_WHEN_BLE_CONNECTED == 0
#if BLE_RUNTIME_ENABLED
    if (ble_is_connected()) {
        idleSleepMs = 0;
    }
#endif
#endif
    if (Serial && Serial.available() > 0) {
        idleSleepMs = 0;
    }
    if (idleSleepMs > MCU_IDLE_SLEEP_MAX_MS) {
        idleSleepMs = MCU_IDLE_SLEEP_MAX_MS;
    }
    cpuIdleSleepMs(idleSleepMs);
#else
    uint32_t idleSleepMs = meshNode.getIdleSleepBudgetMs();
#if MCU_IDLE_SLEEP_WHEN_BLE_CONNECTED == 0
#if BLE_RUNTIME_ENABLED
    if (ble_is_connected()) {
        idleSleepMs = 0;
    }
#endif
#endif
#endif

#if MCU_SYSTEMOFF_ENABLED
    if (idleSleepMs >= (uint32_t)MCU_SYSTEMOFF_IDLE_MIN_MS) {
        Serial.printf("POWER: entering system off (wake pin=%u logic=%s)\n",
                      (unsigned)MCU_SYSTEMOFF_WAKE_PIN,
                      MCU_SYSTEMOFF_WAKE_LOGIC_HIGH ? "HIGH" : "LOW");
        enterSystemOff();
    }
#endif

    // Radio interrupts are handled automatically by the SX126x library.
    // meshNode.run() coordinates wake scheduling and queued TX/RX work.
}
#endif
