#include <stdint.h>

#ifndef _MLX90642_H_
#define _MLX90642_H_

#define MLX90642_INVAL_VAL_ERR 2
#define MLX90642_TIMEOUT_ERR 3
#define MLX90642_YES 1

#define MLX90642_EE_WRITE_TIME 15 
#define MLX90642_POLL_TIME_MS 2
#define MLX90642_MAX_POLL_TRIES 100
#define MLX90642_REF_TIME 2000

#define MLX90642_AUX_DATA_ADDRESS 0x2E02
#define MLX90642_IR_DATA_ADDRESS 0x2E2A
#define MLX90642_TO_DATA_ADDRESS 0x342C
#define MLX90642_FLAGS_ADDRESS 0x3C14
#define MLX90642_REFRESH_RATE_ADDRESS 0x11F0

#define MLX90642_FLAGS_READY_MASK 0x0100
#define MLX90642_FLAGS_READY_SHIFT 8
#define MLX90642_REFRESH_RATE_MASK 0x0007

#define MLX90642_TOTAL_NUMBER_OF_AUX 20
#define MLX90642_TOTAL_NUMBER_OF_PIXELS 768

#define MLX90642_REF_RATE_2HZ 2
#define MLX90642_REF_RATE_32HZ 6

#define MLX90642_START_SYNC_MEAS_CMD 0x0001

int MLX90642_Init(uint8_t slaveAddr);
int MLX90642_SetRefreshRate(uint8_t slaveAddr, uint16_t ref_rate);
int MLX90642_GetRefreshRate(uint8_t slaveAddr);
int MLX90642_GetRefreshTime(uint8_t slaveAddr);
int MLX90642_GetFrameData(uint8_t slaveAddr, uint16_t *aux, uint16_t *rawpix, uint16_t *pixVal);
int MLX90642_StartSync(uint8_t slaveAddr);
int MLX90642_IsDataReady(uint8_t slaveAddr);
int MLX90642_ClearDataReady(uint8_t slaveAddr);

int MLX90642_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *rData);
int MLX90642_Config(uint8_t slaveAddr, uint16_t writeAddress, uint16_t wData);
int MLX90642_I2CCmd(uint8_t slaveAddr, uint16_t i2c_cmd);
void MLX90642_Wait_ms(uint16_t time_ms);

int MLX90642_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data);
int MLX90642_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data);
int MLX90642_I2CCmd(uint8_t slaveAddr, uint16_t cmd);
int MLX90642_Config(uint8_t slaveAddr, uint16_t writeAddress, uint16_t wData);
void MLX90642_Wait_ms(uint16_t ms);


#endif