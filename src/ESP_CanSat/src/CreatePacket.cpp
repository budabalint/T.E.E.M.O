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

void Packet::WriteI2CSensorDataToBuffer(int currentSeq) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        
        canSat._sgp.measure();
        PacketA_WritePtr->TVOC_index = canSat._sgp.GetTVOC();
        PacketA_WritePtr->CO2_index = canSat._sgp.GetCo2();

        PacketA_WritePtr->current1 = 0;
        PacketA_WritePtr->current2 = 0;
        PacketA_WritePtr->voltage1 = 0;
        PacketA_WritePtr->voltage2 = 0;

        PacketB_WritePtr->startByte = 0xFE;
        PacketB_WritePtr->id = 0xBB;
        PacketB_WritePtr->sequence = currentSeq;

        PacketB_WritePtr->temp = (uint16_t)((canSat._bme.readTemperature() + 100.0f) * 100.0f);
        PacketB_WritePtr->hum = (uint16_t)(canSat._bme.readHumidity() * 100.0f);
        PacketB_WritePtr->press = (int32_t)(canSat._bme.readPressure() * 100.0f); 

        canSat._gps.encode();

        PacketB_WritePtr->lat = (int)(canSat._gps.getLat()*10000000);
        PacketB_WritePtr->lng = (int)(canSat._gps.getLng()*10000000);
        PacketB_WritePtr->speed = (int32_t)((canSat._gps.getSpeed() / 3.6f) * 100.0f);
        PacketB_WritePtr->alt = (int32_t)(canSat._gps.getAltitude() * 100.0f);
        PacketB_WritePtr->hdop = (uint16_t)(canSat._gps.getHDOP() * 100.0f);
        PacketB_WritePtr->sats = (uint8_t)canSat._gps.getSatellites();

        PacketB_WritePtr->white = canSat._veml.readWhite();
        PacketB_WritePtr->lux = (uint32_t)canSat._veml.readLux();

        xSemaphoreGive(dataMutex);
    }
}

void Packet::WriteBNODataToBuffer(int currentSeq) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        
        PacketA_WritePtr->startByte = 0xFE;
        PacketA_WritePtr->id = 0xAA;
        PacketA_WritePtr->sequence = currentSeq;
        
        canSat._bno.update();

        PacketA_WritePtr->roll = (int16_t)(canSat._bno.getRoll() * 100.0f);
        PacketA_WritePtr->pitch = (int16_t)(canSat._bno.getPitch() * 100.0f);
        PacketA_WritePtr->yaw = (int16_t)(canSat._bno.getYaw() * 100.0f);
        
        digitalWrite(BNO_CS, HIGH);
        
        Vector3 acc = canSat._bno.getLinearAcceleration();
        PacketA_WritePtr->acc_x = (int16_t)(acc.x * 100.0f);
        PacketA_WritePtr->acc_y = (int16_t)(acc.y * 100.0f);
        PacketA_WritePtr->acc_z = (int16_t)(acc.z * 100.0f);
        
        digitalWrite(BNO_CS, HIGH);

        Vector3 gyro = canSat._bno.getGyroscope();
        PacketA_WritePtr->gyro_x = (int16_t)(gyro.x * 100.0f);
        PacketA_WritePtr->gyro_y = (int16_t)(gyro.y * 100.0f);
        PacketA_WritePtr->gyro_z = (int16_t)(gyro.z * 100.0f);
        
        digitalWrite(BNO_CS, HIGH);

        Vector3 mag = canSat._bno.getMagnetometer();
        PacketA_WritePtr->mag_x = (int16_t)(mag.x * 10.0f);
        PacketA_WritePtr->mag_y = (int16_t)(mag.y * 10.0f);
        PacketA_WritePtr->mag_z = (int16_t)(mag.z * 10.0f);
        
        digitalWrite(BNO_CS, HIGH);
        
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