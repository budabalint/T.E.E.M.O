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


Packet::Packet() {}

bool Packet::CreatePacket_A(int seq) {
    memset(&packetA, 0, sizeof(PacketA));
    
    packetA.startByte = 0xFE;
    packetA.sequence = seq;

    bool iaq = canSat._sgp.measure();
    Serial.println(iaq);

    packetA.temp = (uint16_t)((canSat._bme.readTemperature() + 100.0f) * 100.0f);
    packetA.hum = (uint16_t)(canSat._bme.readHumidity() * 100.0f);
    packetA.press = (int32_t)(canSat._bme.readPressure());

    canSat._bno.update();

    packetA.roll = (int16_t)(canSat._bno.getRoll() * 100.0f);
    packetA.pitch = (int16_t)(canSat._bno.getPitch() * 100.0f);
    packetA.yaw = (int16_t)(canSat._bno.getYaw() * 100.0f);

    Vector3 acc = canSat._bno.getLinearAcceleration();
    packetA.acc_x = (int16_t)(acc.x * 100.0f);
    packetA.acc_y = (int16_t)(acc.y * 100.0f);
    packetA.acc_z = (int16_t)(acc.z * 100.0f);

    Vector3 gyro = canSat._bno.getGyroscope();
    packetA.gyro_x = (int16_t)(gyro.x * 100.0f);
    packetA.gyro_y = (int16_t)(gyro.y * 100.0f);
    packetA.gyro_z = (int16_t)(gyro.z * 100.0f);

    Vector3 mag = canSat._bno.getMagnetometer();
    packetA.mag_x = (int16_t)(mag.x * 10.0f);
    packetA.mag_y = (int16_t)(mag.y * 10.0f);
    packetA.mag_z = (int16_t)(mag.z * 10.0f);

    packetA.TVOC_index = canSat._sgp.GetTVOC();
    packetA.CO2_index = canSat._sgp.GetCo2();
    

    packetA.lux = (uint32_t)canSat._veml.readLux();

    packetA.crc = calculateCRC8((uint8_t*)&packetA, sizeof(PacketA) - 1);

    return true;
}

bool Packet::CreatePacket_B(int seq) {
    memset(&packetB, 0, sizeof(PacketB));

    packetB.startByte = 0xFE;
    packetB.sequence = seq;

    canSat._gps.encode();

    if (canSat._gps.isUpdated() || canSat._gps.getSatellites() > 0) {
        packetB.lat = canSat._gps.getLat();
        packetB.lng = canSat._gps.getLng();
        Serial.println("1");
        packetB.speed = (int32_t)((canSat._gps.getSpeed() / 3.6f) * 100.0f);
        packetB.alt = (int32_t)(canSat._gps.getAltitude() * 100.0f);
        packetB.course = 0;
        packetB.sats = (uint8_t)canSat._gps.getSatellites();
        packetB.hdop = (uint16_t)(canSat._gps.getHDOP() * 100.0f);
    }

    packetB.current1 = 0;
    packetB.current2 = 0;
    packetB.voltage1 = 0;
    packetB.voltage2 = 0;

    packetB.white = canSat._veml.readWhite();

    packetB.crc = calculateCRC8((uint8_t*)&packetB, sizeof(PacketB) - 1);

    return true;
}

void Packet::PrintRaw_A() {
    uint8_t *data = (uint8_t*)&packetA;
    size_t len = sizeof(PacketA);

    Serial.println(F("\n--- PACKET A [RAW BYTES] ---"));
    for (size_t i = 0; i < len; i++) {
        Serial.print(F("["));
        if (i < 10) Serial.print(F("0"));
        Serial.print(i);
        Serial.print(F("] : 0x"));
        
        if (data[i] < 0x10) Serial.print(F("0"));
        Serial.print(data[i], HEX);

        if ((i + 1) % 8 == 0) {
            Serial.println();
        } else {
            Serial.print(F("\t"));
        }
    }
    Serial.println(F("\n----------------------------"));
}

void Packet::PrintRaw_B() {
    uint8_t *data = (uint8_t*)&packetB;
    size_t len = sizeof(PacketB);

    Serial.println(F("\n--- PACKET B [RAW BYTES] ---"));
    for (size_t i = 0; i < len; i++) {
        Serial.print(F("["));
        if (i < 10) Serial.print(F("0"));
        Serial.print(i);
        Serial.print(F("] : 0x"));
        
        if (data[i] < 0x10) Serial.print(F("0"));
        Serial.print(data[i], HEX);

        if ((i + 1) % 8 == 0) {
            Serial.println();
        } else {
            Serial.print(F("\t"));
        }
    }
    Serial.println(F("\n----------------------------"));
}