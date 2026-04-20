#pragma once

#include <stdint.h>

//==============================================================
// LoRa radio configuration (SX126x-Arduino)
//==============================================================

// Frequency: US ISM band (902-928 MHz, FCC Part 15.247). 915 MHz is the band center.
static const uint32_t LORA_FREQUENCY_HZ = 915000000UL;

// Spreading factor, bandwidth, coding rate
// SX126x: bandwidth 0=125kHz, 1=250kHz, 2=500kHz
static const uint8_t LORA_SPREADING_FACTOR = 12;
static const uint8_t LORA_BANDWIDTH = 0;
static const uint32_t LORA_BANDWIDTH_HZ = (LORA_BANDWIDTH == 0 ? 125000UL
                                      : (LORA_BANDWIDTH == 1 ? 250000UL : 500000UL));
// SX126x: coding rate 1=4/5 (matches LoRa.setCodingRate(5))
static const uint8_t LORA_CODING_RATE = 1;

// Preamble length in symbols
// Long preamble = sleepPeriodSymbols + explicit margin (adjust as needed)
#ifndef LORA_SLEEP_PERIOD_SYMBOLS
#define LORA_SLEEP_PERIOD_SYMBOLS 64
#endif

#ifndef LORA_PREAMBLE_MARGIN_SYMBOLS
#define LORA_PREAMBLE_MARGIN_SYMBOLS 8
#endif

static const uint16_t LORA_LONG_PREAMBLE_SYMBOLS =
    (uint16_t)LORA_SLEEP_PERIOD_SYMBOLS + (uint16_t)LORA_PREAMBLE_MARGIN_SYMBOLS;

// Derived timing for wake/listen margins
static const uint32_t LORA_SYMBOL_DURATION_US =
    ((uint32_t)1 << LORA_SPREADING_FACTOR) * 1000000UL / LORA_BANDWIDTH_HZ;
static const uint32_t LORA_PREAMBLE_MARGIN_US =
    LORA_SYMBOL_DURATION_US * (uint32_t)LORA_PREAMBLE_MARGIN_SYMBOLS;
static const uint32_t LORA_PREAMBLE_MARGIN_MS = (LORA_PREAMBLE_MARGIN_US + 500UL) / 1000UL;

// Packet options
static const bool LORA_CRC_ENABLED = true;
static const bool LORA_FIX_LEN = false;     // Explicit header (variable length)
static const bool LORA_IQ_INVERTED_ENABLED = false;
static const bool LORA_FREQ_HOP_ON = false;
static const uint8_t LORA_HOP_PERIOD = 0;

// RX options
static const uint8_t LORA_SYMB_TIMEOUT = 0;
static const uint8_t LORA_PAYLOAD_LEN = 0;
static const bool LORA_RX_CONTINUOUS = true;

// TX options
static const int8_t LORA_TX_POWER_DBM = 22;
static const uint32_t LORA_TX_TIMEOUT_MS = 10000;
