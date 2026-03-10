#include "Create_Packet.h"

uint8_t calculateCRC8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        uint8_t extract = *data++;
        for (uint8_t tempI = 8; tempI; tempI--) {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum) {
                crc ^= 0x8C;
            }
            extract >>= 1;
        }
    }
    return crc;
}

Packet::Packet() {
    memset(&packetA_1, 0, sizeof(PacketA));
    memset(&packetA_2, 0, sizeof(PacketA));
    memset(&packetB_1, 0, sizeof(PacketB));
    memset(&packetB_2, 0, sizeof(PacketB));

    PacketA_ReadPtr = &packetA_1;
    PacketA_WritePtr = &packetA_2;
    PacketB_ReadPtr = &packetB_1;
    PacketB_WritePtr = &packetB_2;
}

PacketA* Packet::getPacketA_ReadPtr() {
    return PacketA_ReadPtr;
}

PacketB* Packet::getPacketB_ReadPtr() {
    return PacketB_ReadPtr;
}

void Packet::WriteI2CSensorDataToBuffer(bool debug) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        
        unsigned long t_start;
        unsigned long dt_sgp = 0, dt_bme = 0, dt_gps = 0, dt_veml = 0, dt_ina = 0;

        // --- ALAPÉRTELMEZETT / DUMMY ÉRTÉKEK (Hiba esetére) ---
        uint16_t tvoc = 0, co2 = 0;
        float tempVal = 0, humVal = 0, pressVal = 0;
        double latVal = 0, lngVal = 0, speedVal = 0, altVal = 0, hdopVal = 0;
        uint8_t satsVal = 0;
        float whiteVal = 0, luxVal = 0;
        float vol3v3 = 0, cur3v3 = 0, vol12v = 0, cur12v = 0;

        // --- SGP41 OLVASÁSA ---
        if (canSat.sgp_ok) {
            t_start = micros();
            canSat._sgp.measure();
            tvoc = canSat._sgp.GetTVOC();
            co2 = canSat._sgp.GetCo2();
            dt_sgp = micros() - t_start;
        }

        // --- BME280 OLVASÁSA ---
        if (canSat.bme_ok) {
            t_start = micros();
            tempVal = canSat._bme.readTemperature();
            humVal = canSat._bme.readHumidity();
            pressVal = canSat._bme.readPressure();
            dt_bme = micros() - t_start;
        }

        // --- GPS OLVASÁSA ---
        if (canSat.gps_ok) {
            t_start = micros();
            canSat._gps.encode(); 
            latVal = canSat._gps.getLat();
            lngVal = canSat._gps.getLng();
            speedVal = canSat._gps.getSpeed();
            altVal = canSat._gps.getAltitude();
            hdopVal = canSat._gps.getHDOP();
            satsVal = canSat._gps.getSatellites();
            dt_gps = micros() - t_start;
        }

        // --- VEML OLVASÁSA ---
        if (canSat.veml_ok) {
            t_start = micros();
            whiteVal = canSat._veml.readWhite();
            luxVal = canSat._veml.readLux();
            dt_veml = micros() - t_start;
        }

        // --- INA SZENZOROK OLVASÁSA ---
        t_start = micros();
        if (canSat.ina3v3_ok) {
            canSat._ina3v3.measure();
            vol3v3 = canSat._ina3v3.GetVoltage();
            cur3v3 = canSat._ina3v3.GetCurrent();
        }
        if (canSat.ina12v_ok) {
            canSat._ina12v.measure();
            vol12v = canSat._ina12v.GetVoltage();
            cur12v = canSat._ina12v.GetCurrent();
        }
        dt_ina = micros() - t_start;

        // --- CSOMAG 'A' FELTÖLTÉSE ---
        PacketA_WritePtr->TVOC_index = tvoc;
        PacketA_WritePtr->CO2_index = co2;

        PacketA_WritePtr->voltage1 = (uint16_t)(vol3v3 * 1000);
        PacketA_WritePtr->current1 = (uint32_t)(cur3v3 * 1000);
        
        PacketA_WritePtr->voltage2 = (uint16_t)(vol12v * 1000);
        PacketA_WritePtr->current2 = (uint32_t)(cur12v * 1000);

        // --- CSOMAG 'B' FELTÖLTÉSE ---
        PacketB_WritePtr->startByte = 0xFE;
        PacketB_WritePtr->id = 0xBB;

        PacketB_WritePtr->temp = (uint16_t)((tempVal + 100.0f) * 100.0f);
        PacketB_WritePtr->hum = (uint16_t)(humVal * 100.0f);
        PacketB_WritePtr->press = (int32_t)(pressVal * 100.0f); 

        PacketB_WritePtr->lat = (int)(latVal * 10000000);
        PacketB_WritePtr->lng = (int)(lngVal * 10000000);
        PacketB_WritePtr->speed = (int32_t)((speedVal / 3.6f) * 100.0f);
        PacketB_WritePtr->alt = (int32_t)(altVal * 100.0f);
        PacketB_WritePtr->hdop = (uint16_t)(hdopVal * 100.0f);
        PacketB_WritePtr->sats = satsVal;

        PacketB_WritePtr->white = whiteVal;
        PacketB_WritePtr->lux = (uint32_t)luxVal;

        if (debug) {
            Serial.println("\n----------------- I2C SENSOR DATA -----------------");
            Serial.println("SENSOR  | TIME (us) | DATA");
            Serial.println("---------------------------------------------------");
            
            Serial.printf("SGP41   | %9lu | TVOC: %d ppb, CO2: %d ppm\n", dt_sgp, tvoc, co2);
            Serial.printf("BME280  | %9lu | T: %.2f C, H: %.2f %%, P: %.2f hPa\n", dt_bme, tempVal, humVal, pressVal);
            Serial.printf("GPS     | %9lu | Sats: %d, Alt: %.1f m, Pos: %.6f, %.6f\n", dt_gps, satsVal, altVal, latVal, lngVal);
            Serial.printf("VEML    | %9lu | Lux: %.1f, White: %.1f\n", dt_veml, luxVal, whiteVal);
            Serial.printf("PWR 3V3 | %9lu | U: %.6f V, I: %.6f mA\n", dt_ina, vol3v3, cur3v3);
            Serial.printf("PWR 12V |           | U: %.6f V, I: %.6f mA\n", vol12v, cur12v);
            
            Serial.println("---------------------------------------------------\n");
        }

        xSemaphoreGive(dataMutex);
    }
}

void Packet::WriteBNODataToBuffer(bool debug) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        
        unsigned long t_start;
        unsigned long dt_update = 0, dt_orient = 0, dt_acc = 0, dt_gyro = 0, dt_mag = 0;

        // Alapértelmezett értékek (ha a BNO rossz)
        float roll = 0, pitch = 0, yaw = 0;
        Vector3 acc = {0,0,0}, gyro = {0,0,0}, mag = {0,0,0};

        PacketA_WritePtr->startByte = 0xFE;
        PacketA_WritePtr->id = 0xAA;
        
        if (canSat.bno_ok) {
            t_start = micros();
            canSat._bno.update();
            dt_update = micros() - t_start;

            t_start = micros();
            roll = canSat._bno.getRoll();
            pitch = canSat._bno.getPitch();
            yaw = canSat._bno.getYaw();
            digitalWrite(BNO_CS, HIGH);
            dt_orient = micros() - t_start;
            
            t_start = micros();
            acc = canSat._bno.getLinearAcceleration();
            digitalWrite(BNO_CS, HIGH);
            dt_acc = micros() - t_start;

            t_start = micros();
            gyro = canSat._bno.getGyroscope();
            digitalWrite(BNO_CS, HIGH);
            dt_gyro = micros() - t_start;

            t_start = micros();
            mag = canSat._bno.getMagnetometer();
            digitalWrite(BNO_CS, HIGH);
            dt_mag = micros() - t_start;
        }

        // Csomag kitöltése (ha jó a szenzor, a mért adat megy, ha rossz, akkor 0)
        PacketA_WritePtr->roll = (int16_t)(roll * 100.0f);
        PacketA_WritePtr->pitch = (int16_t)(pitch * 100.0f);
        PacketA_WritePtr->yaw = (int16_t)(yaw * 100.0f);

        PacketA_WritePtr->acc_x = (int16_t)(acc.x * 100.0f);
        PacketA_WritePtr->acc_y = (int16_t)(acc.y * 100.0f);
        PacketA_WritePtr->acc_z = (int16_t)(acc.z * 100.0f);

        PacketA_WritePtr->gyro_x = (int16_t)(gyro.x * 100.0f);
        PacketA_WritePtr->gyro_y = (int16_t)(gyro.y * 100.0f);
        PacketA_WritePtr->gyro_z = (int16_t)(gyro.z * 100.0f);

        PacketA_WritePtr->mag_x = (int16_t)(mag.x * 10.0f);
        PacketA_WritePtr->mag_y = (int16_t)(mag.y * 10.0f);
        PacketA_WritePtr->mag_z = (int16_t)(mag.z * 10.0f);

        if (debug) {
            Serial.println("\n-------------------- BNO085 IMU -------------------");
            Serial.println("TYPE    | TIME (us) | X / R    | Y / P    | Z / Y");
            Serial.println("---------------------------------------------------");
            
            Serial.printf("UPDATE  | %9lu | -        | -        | -\n", dt_update);
            Serial.printf("ORIENT  | %9lu | %8.2f | %8.2f | %8.2f\n", dt_orient, roll, pitch, yaw);
            Serial.printf("ACCEL   | %9lu | %8.2f | %8.2f | %8.2f\n", dt_acc, acc.x, acc.y, acc.z);
            Serial.printf("GYRO    | %9lu | %8.2f | %8.2f | %8.2f\n", dt_gyro, gyro.x, gyro.y, gyro.z);
            Serial.printf("MAG     | %9lu | %8.2f | %8.2f | %8.2f\n", dt_mag, mag.x, mag.y, mag.z);
            
            Serial.println("---------------------------------------------------\n");
        }

        xSemaphoreGive(dataMutex);
    }
}

void Packet::PreparePacketA_ForSending(uint8_t seq) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        PacketA* temp = PacketA_ReadPtr;
        PacketA_ReadPtr = PacketA_WritePtr;
        PacketA_WritePtr = temp;
        xSemaphoreGive(dataMutex);
    }
    ((uint8_t*)PacketA_ReadPtr)[2] = seq; // Feltételezve hogy a 2-es index a szekvenciaszám
    
    PacketA_ReadPtr->crc = calculateCRC8((uint8_t*)PacketA_ReadPtr, sizeof(PacketA) - 1);
}

void Packet::PreparePacketB_ForSending(uint8_t seq) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        PacketB* temp = PacketB_ReadPtr;
        PacketB_ReadPtr = PacketB_WritePtr;
        PacketB_WritePtr = temp;
        xSemaphoreGive(dataMutex);
    }
    ((uint8_t*)PacketB_ReadPtr)[2] = seq;

    PacketB_ReadPtr->crc = calculateCRC8((uint8_t*)PacketB_ReadPtr, sizeof(PacketB) - 1);
}