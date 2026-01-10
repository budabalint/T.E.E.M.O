#pragma once
#include <Arduino.h>
#include <CanSat.h>

extern CanSat canSat;

class Packet {
public:
    struct __attribute__((packed)) PacketA {
        uint8_t startByte = 0xFE;
        uint16_t sequence; 

        uint16_t temp;
        uint16_t hum;
        int32_t press;

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
        uint32_t lux;

        uint8_t crc;
    };

    struct __attribute__((packed)) PacketB {
        uint8_t startByte = 0xFE;
        uint16_t sequence;

        float lat;
        float lng;
        int32_t speed;
        int32_t alt;
        uint16_t course;
        uint8_t sats;
        uint16_t hdop;

        uint32_t current1;
        uint32_t current2;
        uint16_t voltage1;
        uint16_t voltage2;

        uint16_t white;

        uint8_t crc;
    };

    PacketA packetA;
    PacketB packetB;

    Packet();
    bool CreatePacket_A(int sequence);
    bool CreatePacket_B(int sequence);

    void PrintRaw_A();
    void PrintRaw_B();



private:
    
};