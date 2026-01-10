#include <Arduino.h>
#include <Wire.h>
#include "mlx90642.h"

#define I2C_SDA 4
#define I2C_SCL 5
#define MLX_I2C_ADDR 0x66

uint16_t mlxAux[MLX90642_TOTAL_NUMBER_OF_AUX];     
uint16_t mlxRawPix[MLX90642_TOTAL_NUMBER_OF_PIXELS];  
uint16_t mlxPixVal[MLX90642_TOTAL_NUMBER_OF_PIXELS];

void setup() {
    Serial.begin(4000000); 
    
    Wire.setBufferSize(4096); 
    Wire.begin(I2C_SDA, I2C_SCL); 
    
    Wire.setClock(400000); 

    MLX90642_Init(MLX_I2C_ADDR);
    MLX90642_SetRefreshRate(MLX_I2C_ADDR, MLX90642_REF_RATE_32HZ);
}

void loop() {
    int status = MLX90642_GetFrameData(MLX_I2C_ADDR, mlxAux, mlxRawPix, mlxPixVal);

    if (status >= 0) {
        for (int i = 0; i < MLX90642_TOTAL_NUMBER_OF_PIXELS; i++) {
            Serial.print(mlxPixVal[i]);
            if (i < MLX90642_TOTAL_NUMBER_OF_PIXELS - 1) {
                Serial.print(","); 
            }
        }
    Serial.println();
    }
}

