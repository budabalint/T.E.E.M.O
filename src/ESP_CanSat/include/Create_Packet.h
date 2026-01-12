#pragma once
#include <Arduino.h>
#include <CanSat.h>
#include <ThermalCam.h>

extern CanSat canSat;

struct __attribute__((packed)) PacketA {
        uint8_t startByte = 0xFE;
        uint8_t id = 0x18;
        uint8_t sequence;

        int16_t roll;
        int16_t pitch;
        int16_t yaw;

        int16_t acc_x;
        int16_t acc_y;
        int16_t acc_z;

        int16_t gyro_x;
        int16_t gyro_y;
        int16_t gyro_z;

        int16_t mag_x;
        int16_t mag_y;
        int16_t mag_z;

        uint16_t TVOC_index;
        uint16_t CO2_index;

        uint32_t current1;
        uint32_t current2;
        uint16_t voltage1;
        uint16_t voltage2;

        uint8_t crc;
    };

    struct __attribute__((packed)) PacketB {
        uint8_t startByte = 0xFE;
        uint8_t id = 0x19;
        uint8_t sequence;

        uint16_t temp;
        uint16_t hum;
        int32_t press;

        float lat;
        float lng;
        int32_t speed;
        int32_t alt;
        uint16_t course;
        uint16_t hdop;
        uint8_t sats;

        uint16_t white;
        uint32_t lux;

        uint8_t crc;
    };

class Packet {
public:
    

    PacketA packetA;
    PacketB packetB;

    Packet();
    PacketA CreatePacket_A(int sequence);
    PacketB CreatePacket_B(int sequence);
    bool CreateThermalCamPacket(int sequence);

};