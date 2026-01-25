#pragma once
#include <Arduino.h>
#include <CanSat.h>
#include <ThermalCam.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern CanSat canSat;
extern SemaphoreHandle_t dataMutex;

struct __attribute__((packed)) PacketA {
    uint8_t startByte = 0xFE;
    uint8_t id = 0xAA;
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
    uint8_t id = 0xBB;
    uint8_t sequence;

    uint16_t temp;
    uint16_t hum;
    int32_t press;

    int lat;
    int lng;

    int32_t speed;
    int32_t alt;
    uint16_t hdop;
    uint8_t sats;

    uint16_t white;
    uint32_t lux;
    int temp3;
    int16_t temp1;
    int8_t temp2;

    uint8_t crc;
};

class Packet {
public:
    PacketA packetA_1;
    PacketA packetA_2;
    PacketB packetB_1;
    PacketB packetB_2;

    Packet();
    void WriteI2CSensorDataToBuffer(int currentSeq);
    void WriteBNODataToBuffer(int currentSeq);
    
    void PreparePacketA_ForSending();
    void PreparePacketB_ForSending();
    
    PacketA* getPacketA_ReadPtr();
    PacketB* getPacketB_ReadPtr();

private:
    PacketA* PacketA_ReadPtr;
    PacketA* PacketA_WritePtr;
    
    PacketB* PacketB_ReadPtr;
    PacketB* PacketB_WritePtr;
};