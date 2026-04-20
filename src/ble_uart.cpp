#include "ble_uart.h"
#include <Arduino.h>
#include <bluefruit.h>
#include <stdarg.h>

// Thin wrappers declared in OLEDDisplay module to avoid direct type includes
extern void oled_show_ble_connected();
extern void oled_show_ble_disconnected();

static BLEUart bleuart;       // Nordic UART Service (NUS)
static BLEDfu bledfu;         // OTA DFU service
static volatile bool s_connected = false;
static volatile bool s_initialized = false;

#ifndef BLE_LOG_LEVEL
#define BLE_LOG_LEVEL 1
#endif

#ifndef BLE_RX_HEX_DUMP
#define BLE_RX_HEX_DUMP 0
#endif

#ifndef BLE_ECHO_RX_TO_SERIAL
#define BLE_ECHO_RX_TO_SERIAL 1
#endif

#if BLE_LOG_LEVEL >= 1
#define BLE_LOG_INFO(...) Serial.printf(__VA_ARGS__)
#else
#define BLE_LOG_INFO(...) do {} while (0)
#endif

#if BLE_LOG_LEVEL >= 2
#define BLE_LOG_DEBUG(...) Serial.printf(__VA_ARGS__)
#define BLE_LOG_DEBUG_LN(msg) Serial.println(msg)
#else
#define BLE_LOG_DEBUG(...) do {} while (0)
#define BLE_LOG_DEBUG_LN(msg) do {} while (0)
#endif

static void connect_cb(uint16_t conn_handle) {
    BLE_LOG_INFO("BLE: connected (handle=%u)\n", conn_handle);
    s_connected = true;

    // Notify OLED display through wrapper
    oled_show_ble_connected();
}

static void disconnect_cb(uint16_t conn_handle, uint8_t reason) {
    (void)conn_handle;
    BLE_LOG_INFO("BLE: disconnected (reason=%u)\n", reason);
    s_connected = false;

    // Notify OLED display through wrapper
    oled_show_ble_disconnected();
}

static void start_advertising(void) {
    // Advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(bleuart);

    // Scan response packet
    Bluefruit.ScanResponse.addName();

    // Start advertising with a timeout of 0 (forever)
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);  // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);    // number of seconds in fast mode
    Bluefruit.Advertising.start(0);              // 0 = no timeout, advertise forever
    BLE_LOG_DEBUG_LN("BLE: advertising started");
}

void ble_init(const char* device_name, bool sensor_failure)
{
    // Only initialize BLE if sensors have failed
    if (!sensor_failure) {
        BLE_LOG_INFO("BLE: disabled (sensors attached)\n");
        s_initialized = false; // ensure BLE state reflects that it's not active
        return;
    }

    BLE_LOG_INFO("BLE: init start (%s)\n", device_name);
    Bluefruit.begin();

    // Set TX Power
    Bluefruit.setTxPower(4);

    // Set the device name
    Bluefruit.setName(device_name);

    // Set connect/disconnect callbacks
    Bluefruit.Periph.setConnectCallback(connect_cb);
    Bluefruit.Periph.setDisconnectCallback(disconnect_cb);

    // Configure and start BLE UART service (NUS)
    bleuart.begin();

    // Set up DFU service
    bledfu.begin();

    // Start advertising
    start_advertising();

    // Mark BLE as initialized so ble_poll and BLE helper functions run
    s_initialized = true;

    BLE_LOG_INFO("BLE: init complete\n");
}

void ble_poll() {
    if (!s_initialized) return;

    int avail = bleuart.available();
    if (avail <= 0) return;

    BLE_LOG_DEBUG("BLE: rx available=%d connected=%d\n", avail, s_connected ? 1 : 0);

    // Read up to a buffer.
    uint8_t buf[256];
    size_t idx = 0;
    while (bleuart.available() && idx < sizeof(buf)) {
        int c = bleuart.read();
        if (c < 0) break;
        buf[idx++] = (uint8_t)c;
    }

    if (idx == 0) return;

#if BLE_LOG_LEVEL >= 2
    BLE_LOG_DEBUG("BLE RX (%u bytes)\n", (unsigned)idx);
#if BLE_RX_HEX_DUMP
    for (size_t i = 0; i < idx; ++i) {
        if (buf[i] < 0x10) Serial.print('0');
        Serial.print(buf[i], HEX);
        if (i + 1 < idx) Serial.print(' ');
    }
    Serial.println();
#endif
#endif

#if BLE_ECHO_RX_TO_SERIAL
    if (idx > 0) {
        // Mirror raw BLE UART bytes to Serial so existing command handlers keep working.
        Serial.write(buf, idx);
        Serial.println();
    }
#endif
}

bool ble_is_connected() {
    return s_initialized && s_connected;
}

void ble_println(const char* s) {
    if (!s_initialized || !s_connected) return;
    bleuart.println(s);
}

void ble_print(const char* s) {
    if (!s_initialized || !s_connected) return;
    bleuart.print(s);
}

void ble_printf(const char* fmt, ...) {
    if (!s_initialized || !s_connected) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bleuart.print(buf);
}
