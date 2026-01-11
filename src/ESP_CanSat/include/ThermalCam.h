#pragma once
#include "mlx90642.h"

class ThermalCam {
public:
    bool begin(int i2c_speed);
    bool GetThermalData();

    struct __attribute__((packed)) ThermalPacket {
        uint8_t startByte = 0xFE;
        uint16_t sequence;

        uint8_t data[40];

        uint8_t crc;
    };

};