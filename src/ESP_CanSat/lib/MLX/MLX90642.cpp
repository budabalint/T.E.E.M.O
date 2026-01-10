#include <MLX90642.h>
#include <Wire.h>

int MLX90642_GetRefreshRate(uint8_t slaveAddr)
{
    uint16_t data;
    int status = MLX90642_I2CRead(slaveAddr, MLX90642_REFRESH_RATE_ADDRESS, 1, &data);
    if(status < 0)
        return status;

    status = data & MLX90642_REFRESH_RATE_MASK;

    if(status < MLX90642_REF_RATE_2HZ)
        return MLX90642_REF_RATE_2HZ;

    return status;
}

int MLX90642_SetRefreshRate(uint8_t slaveAddr, uint16_t ref_rate)
{
    uint16_t data;
    int status = 0;

    if((ref_rate < MLX90642_REF_RATE_2HZ) || (ref_rate > MLX90642_REF_RATE_32HZ))
        return -MLX90642_INVAL_VAL_ERR;

    status = MLX90642_I2CRead(slaveAddr, MLX90642_REFRESH_RATE_ADDRESS, 1, &data);
    if(status < 0)
        return status;

    data &= ~MLX90642_REFRESH_RATE_MASK;
    data |= ref_rate;

    status = MLX90642_Config(slaveAddr, MLX90642_REFRESH_RATE_ADDRESS, data);
    MLX90642_Wait_ms(MLX90642_EE_WRITE_TIME);

    return status;
}

int MLX90642_GetRefreshTime(uint8_t slaveAddr)
{
    uint16_t ref_time_ms = MLX90642_REF_TIME;
    int status = 0;

    status = MLX90642_GetRefreshRate(slaveAddr);
    if(status < 0)
        return status;

    ref_time_ms >>= status;

    return (int16_t)ref_time_ms;
}

int MLX90642_IsDataReady(uint8_t slaveAddr)
{
    uint16_t data;
    int status;

    status = MLX90642_I2CRead(slaveAddr, MLX90642_FLAGS_ADDRESS, 1, &data);
    if(status >= 0) {
        status = data & MLX90642_FLAGS_READY_MASK;
        status >>= MLX90642_FLAGS_READY_SHIFT;
    }

    return status;
}

int MLX90642_ClearDataReady(uint8_t slaveAddr)
{
    uint16_t data;
    int status;

    status = MLX90642_I2CRead(slaveAddr, MLX90642_TO_DATA_ADDRESS, 1, &data);
    if(status < 0)
        return status;

    return MLX90642_IsDataReady(slaveAddr);
}

int MLX90642_Init(uint8_t slaveAddr)
{
    uint16_t ref_time;
    int status;
    int poll_tries;

    status = MLX90642_GetRefreshTime(slaveAddr);
    if(status < 0)
        return status;

    ref_time = (uint16_t)status;

    status = MLX90642_ClearDataReady(slaveAddr);
    if(status !=0)
        return -MLX90642_INVAL_VAL_ERR;

    status = MLX90642_StartSync(slaveAddr);
    if(status < 0)
        return status;

    MLX90642_Wait_ms(ref_time);

    for(poll_tries=0; poll_tries<MLX90642_MAX_POLL_TRIES; poll_tries++) {
        MLX90642_Wait_ms(MLX90642_POLL_TIME_MS);
        status = MLX90642_IsDataReady(slaveAddr);
        if(status < 0)
            return status;

        if(status == MLX90642_YES)
            break;
    }

    if(poll_tries == MLX90642_MAX_POLL_TRIES)
        return -MLX90642_TIMEOUT_ERR;

    return 0;
}

int MLX90642_StartSync(uint8_t slaveAddr)
{
    return MLX90642_I2CCmd(slaveAddr, MLX90642_START_SYNC_MEAS_CMD);
}

int MLX90642_GetFrameData(uint8_t slaveAddr, uint16_t *aux, uint16_t *rawpix, uint16_t *pixVal)
{
    int status;

    status = MLX90642_I2CRead(slaveAddr, MLX90642_AUX_DATA_ADDRESS, MLX90642_TOTAL_NUMBER_OF_AUX, aux);
    if(status < 0)
        return status;

    status = MLX90642_I2CRead(slaveAddr, MLX90642_IR_DATA_ADDRESS, MLX90642_TOTAL_NUMBER_OF_PIXELS, rawpix);
    if(status < 0)
        return status;

    status = MLX90642_I2CRead(slaveAddr, MLX90642_TO_DATA_ADDRESS, MLX90642_TOTAL_NUMBER_OF_PIXELS + 1, pixVal);

    return status;
}

int MLX90642_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data) {
    int chunkSize = 64; 
    int chunks = nMemAddressRead / chunkSize;
    int remainder = nMemAddressRead % chunkSize;
    int currentAddr = startAddress;
    int dataIdx = 0;
    
    uint8_t buf[128]; 

    for (int c = 0; c < chunks; c++) {
        Wire.beginTransmission(slaveAddr);
        Wire.write(currentAddr >> 8);
        Wire.write(currentAddr & 0xFF);
        if (Wire.endTransmission(false) != 0) return -1; 

        int bytesRequested = chunkSize * 2;
        if (Wire.requestFrom((int)slaveAddr, bytesRequested) != bytesRequested) return -1;

        Wire.readBytes(buf, bytesRequested);

        for (int i = 0; i < chunkSize; i++) {
            data[dataIdx++] = (uint16_t)(buf[i*2] << 8) | buf[i*2+1];
        }
        currentAddr += chunkSize;
    }

    if (remainder > 0) {
        Wire.beginTransmission(slaveAddr);
        Wire.write(currentAddr >> 8);
        Wire.write(currentAddr & 0xFF);
        if (Wire.endTransmission(false) != 0) return -1;

        int bytesRequested = remainder * 2;
        if (Wire.requestFrom((int)slaveAddr, bytesRequested) != bytesRequested) return -1;
        
        Wire.readBytes(buf, bytesRequested);

        for (int i = 0; i < remainder; i++) {
            data[dataIdx++] = (uint16_t)(buf[i*2] << 8) | buf[i*2+1];
        }
    }
    return 0; 
}

int MLX90642_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
    Wire.beginTransmission(slaveAddr);
    Wire.write(writeAddress >> 8);
    Wire.write(writeAddress & 0xFF);
    Wire.write(data >> 8);
    Wire.write(data & 0xFF);
    return (Wire.endTransmission() == 0) ? 0 : -1;
}
int MLX90642_I2CCmd(uint8_t slaveAddr, uint16_t cmd) {
    Wire.beginTransmission(slaveAddr);
    Wire.write(cmd >> 8);
    Wire.write(cmd & 0xFF);
    return (Wire.endTransmission() == 0) ? 0 : -1;
}
int MLX90642_Config(uint8_t slaveAddr, uint16_t writeAddress, uint16_t wData) {
    return MLX90642_I2CWrite(slaveAddr, writeAddress, wData);
}
void MLX90642_Wait_ms(uint16_t ms) {
    delay(ms);
}