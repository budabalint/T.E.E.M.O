#pragma once
#include "mlx90642.h"
#include <Arduino.h>

struct __attribute__((packed)) ThermalPacket {
    uint8_t  startByte;   // 0xFE
    uint8_t  packetId;    // 0xCC
    uint8_t  groupId;     // 0-7, melyik 3-soros csoport
    uint16_t frameSeq;    // frame számláló (2 bájt)
    uint8_t  data[120];   // 3 sor kép, soronként 40 bájt
    uint8_t  crc;         // CRC8
};

class ThermalCam {
public:
    void begin(int i2c_speed);
    bool captureFrameToBuffer();
    ThermalPacket getPacketFromBuffer(uint8_t groupId, uint16_t frameSeq);
    void swapBuffersIfNew();
    bool hasNewFrame();
    ThermalCam();

private:
    uint16_t bufferA[MLX90642_TOTAL_NUMBER_OF_PIXELS+2];
    uint16_t bufferB[MLX90642_TOTAL_NUMBER_OF_PIXELS+2];


    uint16_t* writePtr;
    uint16_t* readPtr;

    volatile bool newFrameReady;
    SemaphoreHandle_t bufferMutex;

    uint16_t mlxAux[MLX90642_TOTAL_NUMBER_OF_AUX];     
    uint16_t mlxRawPix[MLX90642_TOTAL_NUMBER_OF_PIXELS];  
    uint16_t mlxPixVal[MLX90642_TOTAL_NUMBER_OF_PIXELS];
};