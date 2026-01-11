    #include <Arduino.h>
    #include <Wire.h>
    #include <ThermalCam.h>
    #include <hardware_pins.h>

    #define MLX_I2C_ADDR 0x66

    uint16_t mlxAux[MLX90642_TOTAL_NUMBER_OF_AUX];     
    uint16_t mlxRawPix[MLX90642_TOTAL_NUMBER_OF_PIXELS];  
    uint16_t mlxPixVal[MLX90642_TOTAL_NUMBER_OF_PIXELS];

    ThermalCam::ThermalCam() {};

    bool ThermalCam::begin(int i2c_speed) {
        Wire.begin(THERMAL_CAM_I2C_SDA, THERMAL_CAM_I2C_SCL); 
        Wire.setBufferSize(4096);
        
        Wire.setClock(i2c_speed); 

        MLX90642_Init(MLX_I2C_ADDR);
        MLX90642_SetRefreshRate(MLX_I2C_ADDR, MLX90642_REF_RATE_32HZ);
    }

    bool ThermalCam::GetThermalData() {
        int status = MLX90642_GetFrameData(MLX_I2C_ADDR, mlxAux, mlxRawPix, mlxPixVal);
        
        ThermalPacket packet;
        uint8_t Row[40];
        uint16_t data[4] = {0,0,0,0};

        if (status >= 0) {
            int index = 0;

            for (int i = 0; i < 8; i++) {
                memcpy(data, &mlxPixVal[i * 8], 8);

                Row[index++] = (uint8_t)((data[0] >> 2) & 0xFF);
                Row[index++] = (uint8_t)((data[0] & 0x03) << 6) | ((data[1] >> 4) & 0x3F);
                Row[index++] = (uint8_t)((data[1] & 0x0F) << 4) | ((data[2] >> 6) & 0x0F);
                Row[index++] = (uint8_t)((data[2] & 0x3F) << 2) | ((data[3] >> 8) & 0x03);
                Row[index++] = (uint8_t)(data[3] & 0xFF);
            }

            memcpy(packet.data, Row, 40);
            // radio.send(packet);
    }
        
    }


