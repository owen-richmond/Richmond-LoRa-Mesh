#pragma once
#include <stddef.h>

// Initialize BLE UART (NUS) with a given device name.
// Only initializes if sensors are disabled/failed.
void ble_init(const char* device_name, bool sensor_failure);

// Call often (non-blocking) to service incoming BLE UART data.
void ble_poll();

// Return true if a Central (phone) is connected.
bool ble_is_connected();

// Print a line to the Central over BLE UART if connected.
void ble_println(const char* s);

// Print without newline to the Central over BLE UART if connected.
void ble_print(const char* s);

// printf-style send over BLE UART if connected.
void ble_printf(const char* fmt, ...);
