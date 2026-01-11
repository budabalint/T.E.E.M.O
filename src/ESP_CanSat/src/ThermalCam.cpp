#include <Wire.h>
#include <ThermalCam.h>
#include <hardware_pins.h>

#define MLX_I2C_ADDR 0x66

ThermalCam::ThermalCam() {};

void ThermalCam::begin(int i2c_speed) {
    Wire.begin(THERMAL_CAM_I2C_SDA, THERMAL_CAM_I2C_SCL); 
    Wire.setBufferSize(4096);
    
    Wire.setClock(i2c_speed); 

    MLX90642_Init(MLX_I2C_ADDR);
    MLX90642_SetRefreshRate(MLX_I2C_ADDR, MLX90642_REF_RATE_32HZ);
}

ThermalPacket ThermalCam::GetThermalData(uint8_t row) {
    if (true) {
        MLX90642_GetFrameData(MLX_I2C_ADDR, mlxAux, mlxRawPix, mlxPixVal);
    }
    
    ThermalPacket packet;
    uint8_t Row[40];
    uint16_t data[4] = {0, 0, 0, 0};

    int index = 0;

    for (size_t i = 0; i < 768; i++)
    {
        Serial.println(mlxRawPix[i]);
    }
    

    for (int i = row; i < 8; i++) {
        memcpy(data, &mlxPixVal[i * 8], 8);
        data[0] /= 5;
        data[1] /= 5;
        data[2] /= 5;
        data[3] /= 5;

        Serial.println(data[0]);

        Row[index++] = (uint8_t)((data[0] >> 2) & 0xFF);
        Row[index++] = (uint8_t)((data[0] & 0x03) << 6) | ((data[1] >> 4) & 0x3F);
        Row[index++] = (uint8_t)((data[1] & 0x0F) << 4) | ((data[2] >> 6) & 0x0F);
        Row[index++] = (uint8_t)((data[2] & 0x3F) << 2) | ((data[3] >> 8) & 0x03);
        Row[index++] = (uint8_t)(data[3] & 0xFF);
    }

    memcpy(packet.data, Row, 40);
    return packet;
}


