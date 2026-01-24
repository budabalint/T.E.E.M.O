#pragma once
#include "mlx90642.h"
#include <Arduino.h>

struct __attribute__((packed)) ThermalPacket {
    uint8_t startByte = 0xFE;
    uint8_t id;
    uint8_t sequence;

    uint8_t data[40];

    uint8_t crc;
};

class ThermalCam {
public:
    void begin(int i2c_speed);
    bool captureFrameToBuffer();
    ThermalPacket getPacketFromBuffer(uint8_t row, uint8_t seq);
    void swapBuffersIfNew();
    bool hasNewFrame();
    ThermalCam();

private:
    uint16_t bufferA[MLX90642_TOTAL_NUMBER_OF_PIXELS];
    uint16_t bufferB[MLX90642_TOTAL_NUMBER_OF_PIXELS];


    uint16_t* writePtr;
    uint16_t* readPtr;

    volatile bool newFrameReady;
    SemaphoreHandle_t bufferMutex;

    uint16_t mlxAux[MLX90642_TOTAL_NUMBER_OF_AUX];     
    uint16_t mlxRawPix[MLX90642_TOTAL_NUMBER_OF_PIXELS];  
    uint16_t mlxPixVal[MLX90642_TOTAL_NUMBER_OF_PIXELS];
};