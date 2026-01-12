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

PacketA Packet::CreatePacket_A(int seq) {
    unsigned long totalStart = micros();
    unsigned long lastTime = totalStart;

    memset(&packetA, 0, sizeof(PacketA));
    
    packetA.startByte = 0xFE;
    packetA.sequence = seq;

    // --- BNO UPDATE ---
    canSat._bno.update();
    unsigned long now = micros();
    Serial.print("BNO update: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- BNO ORIENTATION ---
    packetA.roll = (int16_t)(canSat._bno.getRoll() * 100.0f);
    packetA.pitch = (int16_t)(canSat._bno.getPitch() * 100.0f);
    packetA.yaw = (int16_t)(canSat._bno.getYaw() * 100.0f);
    
    now = micros();
    Serial.print("BNO get Orientation: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- BNO ACCELERATION ---
    Vector3 acc = canSat._bno.getLinearAcceleration();
    packetA.acc_x = (int16_t)(acc.x * 100.0f);
    packetA.acc_y = (int16_t)(acc.y * 100.0f);
    packetA.acc_z = (int16_t)(acc.z * 100.0f);

    now = micros();
    Serial.print("BNO get Acc: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- BNO GYRO ---
    Vector3 gyro = canSat._bno.getGyroscope();
    packetA.gyro_x = (int16_t)(gyro.x * 100.0f);
    packetA.gyro_y = (int16_t)(gyro.y * 100.0f);
    packetA.gyro_z = (int16_t)(gyro.z * 100.0f);

    now = micros();
    Serial.print("BNO get Gyro: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- BNO MAGNETOMETER ---
    Vector3 mag = canSat._bno.getMagnetometer();
    packetA.mag_x = (int16_t)(mag.x * 10.0f);
    packetA.mag_y = (int16_t)(mag.y * 10.0f);
    packetA.mag_z = (int16_t)(mag.z * 10.0f);

    now = micros();
    Serial.print("BNO get Mag: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- SGP MEASURE ---
    canSat._sgp.measure();
    now = micros();
    Serial.print("SGP measure: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- SGP GETTERS ---
    packetA.TVOC_index = canSat._sgp.GetTVOC();
    packetA.CO2_index = canSat._sgp.GetCo2();
    
    now = micros();
    Serial.print("SGP get values: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    packetA.current1 = 0;
    packetA.current2 = 0;
    packetA.voltage1 = 0;
    packetA.voltage2 = 0;
    
    packetA.crc = calculateCRC8((uint8_t*)&packetA, sizeof(PacketA) - 1);

    // --- TOTAL TIME PacketA ---
    Serial.print(">>> PacketA TOTAL: "); Serial.print(micros() - totalStart); Serial.println(" us");
    Serial.println("--------------------------------");

    return packetA;
}

PacketB Packet::CreatePacket_B(int seq) {
    unsigned long totalStart = micros();
    unsigned long lastTime = totalStart;

    memset(&packetB, 0, sizeof(PacketB));

    packetB.startByte = 0xFE;
    packetB.sequence = seq;

    // --- BME READINGS ---
    packetB.temp = (uint16_t)((canSat._bme.readTemperature() + 100.0f) * 100.0f);
    packetB.hum = (uint16_t)(canSat._bme.readHumidity() * 100.0f);
    packetB.press = (int32_t)(canSat._bme.readPressure() * 100.0f); 

    unsigned long now = micros();
    Serial.print("BME read all: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- GPS ENCODE ---
    canSat._gps.encode();
    now = micros();
    Serial.print("GPS encode: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- GPS GETTERS ---
    packetB.lat = canSat._gps.getLat();
    Serial.println(packetB.lat);
    packetB.lng = canSat._gps.getLng();
    packetB.speed = (int32_t)((canSat._gps.getSpeed() / 3.6f) * 100.0f);
    Serial.println(packetB.speed);
    packetB.alt = (int32_t)(canSat._gps.getAltitude() * 100.0f);
    Serial.println(packetB.alt);
    packetB.hdop = (uint16_t)(canSat._gps.getHDOP() * 100.0f);
    Serial.println(packetB.hdop);
    packetB.sats = (uint8_t)canSat._gps.getSatellites();
    Serial.println(packetB.sats);


    now = micros();
    Serial.print("GPS get values: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    // --- VEML READINGS ---
    packetB.white = canSat._veml.readWhite();
    packetB.lux = (uint32_t)canSat._veml.readLux();
    Serial.println(packetB.lux);
    Serial.println(packetB.white);

    now = micros();
    Serial.print("VEML read all: "); Serial.print(now - lastTime); Serial.println(" us");
    lastTime = now;

    packetB.crc = calculateCRC8((uint8_t*)&packetB, sizeof(PacketB) - 1);

    // --- TOTAL TIME PacketB ---
    Serial.print(">>> PacketB TOTAL: "); Serial.print(micros() - totalStart); Serial.println(" us");
    Serial.println("--------------------------------");

    return packetB;
}