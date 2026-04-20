#include "SensorManager.h"
#include "ble_uart.h"

SensorManager::SensorManager() {}

void SensorManager::begin() {
    Wire.begin();
    Wire.setClock(400000);

    if (useBME680) isBME680Ready = initBME680();
    if (useMPU9250) isMPU9250Ready = initMPU9250();

    Serial.println("\nSensor Init Status:");
    Serial.print("  BME680 (Slot B): "); 
    Serial.println(useBME680 ? (isBME680Ready ? "OK" : "FAIL") : "DISABLED");
    Serial.print("  MPU9250 (Slot A): "); 
    Serial.println(useMPU9250 ? (isMPU9250Ready ? "OK" : "FAIL") : "DISABLED");
}

bool SensorManager::initBME680() {
    bool ok = bme.begin(0x76);
    if (ok) {
        bme.setTemperatureOversampling(BME680_OS_8X);
        bme.setHumidityOversampling(BME680_OS_2X);
        bme.setPressureOversampling(BME680_OS_4X);
        bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
        bme.setGasHeater(320, 150);
    }
    return ok;
}

bool SensorManager::initMPU9250() {
    mpu.setWire(&Wire);
    mpu.beginAccel();
    mpu.beginGyro();
    mpu.beginMag();
    
    mpu.accelUpdate();
    return !isnan(mpu.accelX());
}

void SensorManager::readAndPrintSensors() {
    bool hasPrintedHeader = false;
    
    if (useBME680 && isBME680Ready) {
        if (bme.performReading()) {
            if (bme.temperature == 0.0f && bme.humidity == 0.0f && 
                bme.pressure == 0.0f && bme.gas_resistance == 0.0f) {
                bme680ZeroCount++;
                if (bme680ZeroCount >= ZERO_READING_THRESHOLD) {
                    Serial.println("WARN: BME680 returning only zeros - DISABLING permanently");
                    isBME680Ready = false;
                    useBME680 = false;
                }
            } else {
                if (!hasPrintedHeader) {
                    Serial.println("\n--- Sensor Readings ---");
                    hasPrintedHeader = true;
                }
                bme680ZeroCount = 0;
                Serial.print("BME680 T[C]: "); Serial.print(bme.temperature, 2);
                Serial.print("  P[hPa]: "); Serial.print(bme.pressure / 100.0f, 2);
                Serial.print("  RH[%]: "); Serial.print(bme.humidity, 2);
                Serial.print("  Gas[kΩ]: "); Serial.println(bme.gas_resistance / 1000.0f, 2);
            }
        }
    }

    if (useMPU9250 && isMPU9250Ready) {
        mpu.accelUpdate();
        mpu.gyroUpdate();
        
        float accel_x = mpu.accelX();
        float accel_y = mpu.accelY();
        float accel_z = mpu.accelZ();
        float gyro_x = mpu.gyroX();
        float gyro_y = mpu.gyroY();
        
        if (accel_x == 0.0f && accel_y == 0.0f && accel_z == 0.0f && 
            gyro_x == 0.0f && gyro_y == 0.0f) {
            mpu9250ZeroCount++;
            if (mpu9250ZeroCount >= ZERO_READING_THRESHOLD) {
                Serial.println("WARN: MPU9250 returning only zeros - DISABLING permanently");
                isMPU9250Ready = false;
                useMPU9250 = false;
            }
        } else {
            if (!hasPrintedHeader) {
                Serial.println("\n--- Sensor Readings ---");
                hasPrintedHeader = true;
            }
            mpu9250ZeroCount = 0;
            Serial.print("MPU9250 ACC[g]: ");
            Serial.print(accel_x, 2); Serial.print(", ");
            Serial.print(accel_y, 2); Serial.print(", ");
            Serial.println(accel_z, 2);
            Serial.print("  GYR[dps]: ");
            Serial.print(gyro_x, 1); Serial.print(", ");
            Serial.println(gyro_y, 1);
        }
    }
}

size_t SensorManager::serializePayload(uint8_t* buf, size_t maxLen) {
    if (!buf || maxLen < 21) return 0;

    memset(buf, 0, maxLen);
    size_t offset = 0;

    uint8_t bitmap = 0;
    if (useBME680 && isBME680Ready) bitmap |= 0x01;
    if (useMPU9250 && isMPU9250Ready) bitmap |= 0x02;
    buf[offset++] = bitmap;

    int16_t temp_scaled = 0;
    uint16_t hum_scaled = 0;
    uint32_t press_pa = 0;
    uint16_t gas_scaled = 0;
    int16_t accel_x = 0, accel_y = 0, accel_z = 0;
    int16_t gyro_x = 0, gyro_y = 0;

    if (useBME680 && isBME680Ready) {
        if (bme.performReading()) {
            temp_scaled = (int16_t)(bme.temperature * 100.0f);
            hum_scaled = (uint16_t)(bme.humidity * 100.0f);
            press_pa = (uint32_t)bme.pressure;
            gas_scaled = (uint16_t)(bme.gas_resistance / 100.0f);
        }
    }

    if (useMPU9250 && isMPU9250Ready) {
        mpu.accelUpdate();
        mpu.gyroUpdate();
        accel_x = (int16_t)(mpu.accelX() * 1000.0f);
        accel_y = (int16_t)(mpu.accelY() * 1000.0f);
        accel_z = (int16_t)(mpu.accelZ() * 1000.0f);
        gyro_x = (int16_t)(mpu.gyroX() * 100.0f);
        gyro_y = (int16_t)(mpu.gyroY() * 100.0f);
    }

    memcpy(&buf[offset], &temp_scaled, 2); offset += 2;
    memcpy(&buf[offset], &hum_scaled, 2); offset += 2;
    memcpy(&buf[offset], &press_pa, 4); offset += 4;
    memcpy(&buf[offset], &gas_scaled, 2); offset += 2;
    memcpy(&buf[offset], &accel_x, 2); offset += 2;
    memcpy(&buf[offset], &accel_y, 2); offset += 2;
    memcpy(&buf[offset], &accel_z, 2); offset += 2;
    memcpy(&buf[offset], &gyro_x, 2); offset += 2;
    memcpy(&buf[offset], &gyro_y, 2); offset += 2;

    return 21;
}

bool SensorManager::hasSensorsAttached() const {
    bool result = (useBME680 && isBME680Ready) || (useMPU9250 && isMPU9250Ready);
    return result;
}

void SensorManager::printReceivedPayload(const uint8_t* buf, size_t len, uint16_t originId) {
    if (!buf || len < 21) {
        Serial.println("ERROR: Invalid payload size");
        return;
    }

    uint8_t bitmap = buf[0];
    
    if (bitmap == 0) {
        Serial.println("Received packet with no sensor data (coordination packet)");
        if (ble_is_connected()) ble_println("Received packet with no sensor data (coordination packet)");
        return;
    }

    Serial.println("--- RECEIVED SENSOR DATA ---");

    size_t offset = 1;
    int16_t temp_scaled;
    uint16_t hum_scaled;
    uint32_t press_pa;
    uint16_t gas_scaled;
    int16_t accel_x, accel_y, accel_z;
    int16_t gyro_x, gyro_y;

    memcpy(&temp_scaled, &buf[offset], 2); offset += 2;
    memcpy(&hum_scaled, &buf[offset], 2); offset += 2;
    memcpy(&press_pa, &buf[offset], 4); offset += 4;
    memcpy(&gas_scaled, &buf[offset], 2); offset += 2;
    memcpy(&accel_x, &buf[offset], 2); offset += 2;
    memcpy(&accel_y, &buf[offset], 2); offset += 2;
    memcpy(&accel_z, &buf[offset], 2); offset += 2;
    memcpy(&gyro_x, &buf[offset], 2); offset += 2;
    memcpy(&gyro_y, &buf[offset], 2); offset += 2;

    if (bitmap & 0x01) {
        Serial.print("  BME680 - T: "); Serial.print(temp_scaled / 100.0f, 2);
        Serial.print("°C, P: "); Serial.print(press_pa / 100.0f, 2);
        Serial.print("hPa, RH: "); Serial.print(hum_scaled / 100.0f, 2);
        Serial.print("%, Gas: "); Serial.print(gas_scaled / 10.0f, 2); Serial.println("kΩ");
    }
    
    if (bitmap & 0x02) {
        Serial.print("  MPU9250 - Accel: "); Serial.print(accel_x / 1000.0f, 3);
        Serial.print(", "); Serial.print(accel_y / 1000.0f, 3);
        Serial.print(", "); Serial.print(accel_z / 1000.0f, 3); Serial.println(" g");
        Serial.print("           Gyro: "); Serial.print(gyro_x / 100.0f, 2);
        Serial.print(", "); Serial.println(gyro_y / 100.0f, 2);
    }

    // Send as fixed-width comma-separated values over BLE if connected
    // Format: temp(0-99C),humidity(0-99%),gas(0-99kΩ),accelX(0-99m/s²),accelY(0-99m/s²),accelZ(0-99m/s²)
    // All values are rounded integers, always 2 digits, acceleration multiplied by 9.8 for m/s²
    if (ble_is_connected()) {
        // Convert scaled values to actual units and round
        int temp_c = (int)((temp_scaled / 100.0f) + 0.5f);  // Round to nearest integer
        uint8_t humidity_pct = (uint8_t)((hum_scaled / 100.0f) + 0.5f);
        uint8_t gas_kohm = (uint8_t)((gas_scaled / 10.0f) + 0.5f);
        // Convert g to m/s² by multiplying with 9.8, use absolute value
        uint8_t accel_x_ms2 = (uint8_t)((fabs(accel_x) / 1000.0f * 9.8f) + 0.5f);
        uint8_t accel_y_ms2 = (uint8_t)((fabs(accel_y) / 1000.0f * 9.8f) + 0.5f);
        uint8_t accel_z_ms2 = (uint8_t)((fabs(accel_z) / 1000.0f * 9.8f) + 0.5f);
        
        // Clamp values to 0-99 range
        temp_c = (temp_c > 99) ? 99 : temp_c;
        humidity_pct = (humidity_pct > 99) ? 99 : humidity_pct;
        gas_kohm = (gas_kohm > 99) ? 99 : gas_kohm;
        accel_x_ms2 = (accel_x_ms2 > 99) ? 99 : accel_x_ms2;
        accel_y_ms2 = (accel_y_ms2 > 99) ? 99 : accel_y_ms2;
        accel_z_ms2 = (accel_z_ms2 > 99) ? 99 : accel_z_ms2;
        
        // Apply a +25 degree offset for BLE output to adjust the temperature value for BLE clients.
        int temp_c_print = temp_c + 25;
        if (temp_c_print < 0) temp_c_print = 0; // clamp to 0 for BLE formatted output
        if (temp_c_print > 99) temp_c_print = 99;

        // Prepend origin ID (two-digit) — use originId parameter passed to this function
        uint16_t origin_id = originId;
        if (origin_id > 99) origin_id = 99;

        char ble_data[40];
        snprintf(ble_data, sizeof(ble_data), 
            "%02u,%02u,%02u,%02u,%02u,%02u,%02u",
            (unsigned int)origin_id, (unsigned int)temp_c_print, humidity_pct, gas_kohm, accel_x_ms2, accel_y_ms2, accel_z_ms2);
        ble_print(ble_data);
    }
}