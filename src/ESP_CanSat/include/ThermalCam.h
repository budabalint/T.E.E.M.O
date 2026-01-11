#pragma once
#include "mlx90642.h"
#include <Arduino.h>

struct __attribute__((packed)) ThermalPacket {
    uint8_t startByte = 0xFE;
    uint16_t sequence;

    uint8_t data[40];

    uint8_t crc;
};

class ThermalCam {
public:
    void begin(int i2c_speed);
    ThermalPacket GetThermalData(uint8_t row);
    ThermalCam();

private:
    uint16_t mlxAux[MLX90642_TOTAL_NUMBER_OF_AUX];     
    uint16_t mlxRawPix[MLX90642_TOTAL_NUMBER_OF_PIXELS];  
    uint16_t mlxPixVal[MLX90642_TOTAL_NUMBER_OF_PIXELS];
};