#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME680.h>
#include <MPU9250_asukiaaa.h>

class SensorManager {
public:
    SensorManager();
    
    void begin();
    bool initBME680();
    bool initMPU9250();
    
    void readAndPrintSensors();
    size_t serializePayload(uint8_t* buf, size_t maxLen);
    void printReceivedPayload(const uint8_t* buf, size_t len, uint16_t originId = 0);
    bool hasSensorsAttached() const;
    
    void enableBME680(bool enable) { useBME680 = enable; }
    void enableMPU9250(bool enable) { useMPU9250 = enable; }
    
    // Check if any sensors have completely failed
    bool allSensorsFailed() const {
        return (!useBME680 && !useMPU9250) || (!isBME680Ready && !isMPU9250Ready);
    }

private:
    Adafruit_BME680 bme;            // RAK1906 in Slot B
    MPU9250_asukiaaa mpu;           // RAK1905 in Slot A

    bool useBME680 = true;
    bool useMPU9250 = true;
    
    bool isBME680Ready = false;
    bool isMPU9250Ready = false;
    
    uint8_t bme680ZeroCount = 0;
    uint8_t mpu9250ZeroCount = 0;
    
    static const uint8_t ZERO_READING_THRESHOLD = 2;
};