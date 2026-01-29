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

void Packet::WriteI2CSensorDataToBuffer(int currentSeq, bool debug) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        
        // --- Változók az időméréshez ---
        unsigned long t_start;
        unsigned long dt_sgp, dt_bme, dt_gps, dt_veml;

        // 1. SGP30 mérés
        t_start = micros();
        canSat._sgp.measure();
        uint16_t tvoc = canSat._sgp.GetTVOC();
        uint16_t co2 = canSat._sgp.GetCo2();
        dt_sgp = micros() - t_start;

        // 2. BME280 mérés
        t_start = micros();
        float tempVal = canSat._bme.readTemperature();
        float humVal = canSat._bme.readHumidity();
        float pressVal = canSat._bme.readPressure();
        dt_bme = micros() - t_start;

        // 3. GPS mérés
        t_start = micros();
        canSat._gps.encode(); 
        double latVal = canSat._gps.getLat();
        double lngVal = canSat._gps.getLng();
        double speedVal = canSat._gps.getSpeed();
        double altVal = canSat._gps.getAltitude();
        double hdopVal = canSat._gps.getHDOP();
        uint8_t satsVal = canSat._gps.getSatellites();
        dt_gps = micros() - t_start;

        // 4. VEML mérés
        t_start = micros();
        float whiteVal = canSat._veml.readWhite();
        float luxVal = canSat._veml.readLux();
        dt_veml = micros() - t_start;

        // --- Adatok írása a csomagba (PacketA & PacketB) ---
        // (Itt már a lementett változókat használjuk, nem mérünk újra)
        PacketA_WritePtr->TVOC_index = tvoc;
        PacketA_WritePtr->CO2_index = co2;

        PacketA_WritePtr->current1 = 0;
        PacketA_WritePtr->current2 = 0;
        PacketA_WritePtr->voltage1 = 0;
        PacketA_WritePtr->voltage2 = 0;

        PacketB_WritePtr->startByte = 0xFE;
        PacketB_WritePtr->id = 0xBB;
        PacketB_WritePtr->sequence = currentSeq;

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

        // --- DEBUG KIÍRATÁS ---
        if (debug) {
            Serial.println("\n----------------- I2C SENSOR DATA -----------------");
            Serial.println("SENSOR  | TIME (us) | DATA");
            Serial.println("---------------------------------------------------");
            
            Serial.printf("SGP30   | %9lu | TVOC: %d ppb, CO2: %d ppm\n", dt_sgp, tvoc, co2);
            Serial.printf("BME280  | %9lu | T: %.2f C, H: %.2f %%, P: %.2f hPa\n", dt_bme, tempVal, humVal, pressVal);
            Serial.printf("GPS     | %9lu | Sats: %d, Alt: %.1f m, Pos: %.6f, %.6f\n", dt_gps, satsVal, altVal, latVal, lngVal);
            Serial.printf("VEML    | %9lu | Lux: %.1f, White: %.1f\n", dt_veml, luxVal, whiteVal);
            
            Serial.println("---------------------------------------------------\n");
        }

        xSemaphoreGive(dataMutex);
    }
}

void Packet::WriteBNODataToBuffer(int currentSeq, bool debug) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        
        unsigned long t_start;
        unsigned long dt_update, dt_orient, dt_acc, dt_gyro, dt_mag;

        PacketA_WritePtr->startByte = 0xFE;
        PacketA_WritePtr->id = 0xAA;
        PacketA_WritePtr->sequence = currentSeq;
        
        t_start = micros();
        canSat._bno.update();
        dt_update = micros() - t_start;

        t_start = micros();
        float roll = canSat._bno.getRoll();
        float pitch = canSat._bno.getPitch();
        float yaw = canSat._bno.getYaw();
        PacketA_WritePtr->roll = (int16_t)(roll * 100.0f);
        PacketA_WritePtr->pitch = (int16_t)(pitch * 100.0f);
        PacketA_WritePtr->yaw = (int16_t)(yaw * 100.0f);
        digitalWrite(BNO_CS, HIGH); // Az eredeti kódod alapján
        dt_orient = micros() - t_start;
        
        t_start = micros();
        Vector3 acc = canSat._bno.getLinearAcceleration();
        PacketA_WritePtr->acc_x = (int16_t)(acc.x * 100.0f);
        PacketA_WritePtr->acc_y = (int16_t)(acc.y * 100.0f);
        PacketA_WritePtr->acc_z = (int16_t)(acc.z * 100.0f);
        digitalWrite(BNO_CS, HIGH);
        dt_acc = micros() - t_start;

        t_start = micros();
        Vector3 gyro = canSat._bno.getGyroscope();
        PacketA_WritePtr->gyro_x = (int16_t)(gyro.x * 100.0f);
        PacketA_WritePtr->gyro_y = (int16_t)(gyro.y * 100.0f);
        PacketA_WritePtr->gyro_z = (int16_t)(gyro.z * 100.0f);
        digitalWrite(BNO_CS, HIGH);
        dt_gyro = micros() - t_start;

        t_start = micros();
        Vector3 mag = canSat._bno.getMagnetometer();
        PacketA_WritePtr->mag_x = (int16_t)(mag.x * 10.0f);
        PacketA_WritePtr->mag_y = (int16_t)(mag.y * 10.0f);
        PacketA_WritePtr->mag_z = (int16_t)(mag.z * 10.0f);
        digitalWrite(BNO_CS, HIGH);
        dt_mag = micros() - t_start;
        
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

void Packet::PreparePacketA_ForSending() {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        PacketA* temp = PacketA_ReadPtr;
        PacketA_ReadPtr = PacketA_WritePtr;
        PacketA_WritePtr = temp;
        xSemaphoreGive(dataMutex);
    }
    PacketA_ReadPtr->crc = calculateCRC8((uint8_t*)PacketA_ReadPtr, sizeof(PacketA) - 1);
}

void Packet::PreparePacketB_ForSending() {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        PacketB* temp = PacketB_ReadPtr;
        PacketB_ReadPtr = PacketB_WritePtr;
        PacketB_WritePtr = temp;
        xSemaphoreGive(dataMutex);
    }
    PacketB_ReadPtr->crc = calculateCRC8((uint8_t*)PacketB_ReadPtr, sizeof(PacketB) - 1);
}