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

PacketA Packet::CreatePacket_A(int seq, bool debug) {
    unsigned long totalStart = micros();
    unsigned long lastTime = totalStart;

    memset(&packetA, 0, sizeof(PacketA));
    
    packetA.startByte = 0xFE;
    packetA.id = 0xAA;
    packetA.sequence = seq;

    canSat._bno.update();
    unsigned long now = micros();
    if (debug) {
        Serial.print("BNO update: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    packetA.roll = (int16_t)(canSat._bno.getRoll() * 100.0f);
    packetA.pitch = (int16_t)(canSat._bno.getPitch() * 100.0f);
    packetA.yaw = (int16_t)(canSat._bno.getYaw() * 100.0f);
    
    now = micros();
    if (debug) {
        Serial.print("Roll: "); Serial.println(packetA.roll);
        Serial.print("Pitch: "); Serial.println(packetA.pitch);
        Serial.print("Yaw: "); Serial.println(packetA.yaw);
        Serial.print("BNO get Orientation: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    Vector3 acc = canSat._bno.getLinearAcceleration();
    packetA.acc_x = (int16_t)(acc.x * 100.0f);
    packetA.acc_y = (int16_t)(acc.y * 100.0f);
    packetA.acc_z = (int16_t)(acc.z * 100.0f);

    now = micros();
    if (debug) {
        Serial.print("Acc X: "); Serial.println(packetA.acc_x);
        Serial.print("Acc Y: "); Serial.println(packetA.acc_y);
        Serial.print("Acc Z: "); Serial.println(packetA.acc_z);
        Serial.print("BNO get Acc: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    Vector3 gyro = canSat._bno.getGyroscope();
    packetA.gyro_x = (int16_t)(gyro.x * 100.0f);
    packetA.gyro_y = (int16_t)(gyro.y * 100.0f);
    packetA.gyro_z = (int16_t)(gyro.z * 100.0f);

    now = micros();
    if (debug) {
        Serial.print("Gyro X: "); Serial.println(packetA.gyro_x);
        Serial.print("Gyro Y: "); Serial.println(packetA.gyro_y);
        Serial.print("Gyro Z: "); Serial.println(packetA.gyro_z);
        Serial.print("BNO get Gyro: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    Vector3 mag = canSat._bno.getMagnetometer();
    packetA.mag_x = (int16_t)(mag.x * 10.0f);
    packetA.mag_y = (int16_t)(mag.y * 10.0f);
    packetA.mag_z = (int16_t)(mag.z * 10.0f);

    now = micros();
    if (debug) {
        Serial.print("Mag X: "); Serial.println(packetA.mag_x);
        Serial.print("Mag Y: "); Serial.println(packetA.mag_y);
        Serial.print("Mag Z: "); Serial.println(packetA.mag_z);
        Serial.print("BNO get Mag: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    canSat._sgp.measure();
    now = micros();
    if (debug) {
        Serial.print("SGP measure: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    packetA.TVOC_index = canSat._sgp.GetTVOC();
    packetA.CO2_index = canSat._sgp.GetCo2();
    
    now = micros();
    if (debug) {
        Serial.print("TVOC: "); Serial.println(packetA.TVOC_index);
        Serial.print("eCO2: "); Serial.println(packetA.CO2_index);
        Serial.print("SGP get values: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    packetA.current1 = 0;
    packetA.current2 = 0;
    packetA.voltage1 = 0;
    packetA.voltage2 = 0;
    
    packetA.crc = calculateCRC8((uint8_t*)&packetA, sizeof(PacketA) - 1);

    if (debug) {
        Serial.print("\n>>> PacketA TOTAL: "); Serial.print(micros() - totalStart); Serial.println(" us\n");
        Serial.println("--------------------------------");
    }

    return packetA;
}

PacketB Packet::CreatePacket_B(int seq, bool debug) {
    unsigned long totalStart = micros();
    unsigned long lastTime = totalStart;

    memset(&packetB, 0, sizeof(PacketB));

    packetB.startByte = 0xFE;
    packetB.id = 0xBB;
    packetB.sequence = seq;

    packetB.temp = (uint16_t)((canSat._bme.readTemperature() + 100.0f) * 100.0f);
    packetB.hum = (uint16_t)(canSat._bme.readHumidity() * 100.0f);
    packetB.press = (int32_t)(canSat._bme.readPressure() * 100.0f); 

    unsigned long now = micros();
    if (debug) {
        Serial.print("Temp: "); Serial.println(packetB.temp);
        Serial.print("Hum: "); Serial.println(packetB.hum);
        Serial.print("Press: "); Serial.println(packetB.press);
        Serial.print("BME read all: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    canSat._gps.encode();
    now = micros();
    if (debug) {
        Serial.print("GPS encode: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    packetB.lat = (int)(canSat._gps.getLat()*10000000);
    packetB.lng = (int)(canSat._gps.getLng()*10000000);
    packetB.speed = (int32_t)((canSat._gps.getSpeed() / 3.6f) * 100.0f);
    packetB.alt = (int32_t)(canSat._gps.getAltitude() * 100.0f);
    packetB.hdop = (uint16_t)(canSat._gps.getHDOP() * 100.0f);
    packetB.sats = (uint8_t)canSat._gps.getSatellites();

    now = micros();
    if (debug) {
        Serial.print("Lat: "); Serial.println(packetB.lat);
        Serial.print("Lng: "); Serial.println(packetB.lng);
        Serial.print("Speed: "); Serial.println(packetB.speed);
        Serial.print("Alt: "); Serial.println(packetB.alt);
        Serial.print("HDOP: "); Serial.println(packetB.hdop);
        Serial.print("Sats: "); Serial.println(packetB.sats);
        Serial.print("GPS get values: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }
    lastTime = now;

    packetB.white = canSat._veml.readWhite();
    packetB.lux = (uint32_t)canSat._veml.readLux();

    now = micros();
    if (debug) {
        Serial.print("White: "); Serial.println(packetB.white);
        Serial.print("Lux: "); Serial.println(packetB.lux);
        Serial.print("VEML read all: "); Serial.print(now - lastTime); Serial.println(" us\n");
    }

    packetB.temp3 = 0x00;
    packetB.temp1 = 0x00;
    packetB.temp2 = 0x00;
    lastTime = now;

    packetB.crc = calculateCRC8((uint8_t*)&packetB, sizeof(PacketB) - 1);

    if (debug) {
        Serial.print("\n>>> PacketB TOTAL: "); Serial.print(micros() - totalStart); Serial.println(" us\n");
        Serial.println("--------------------------------");
    }

    return packetB;
}