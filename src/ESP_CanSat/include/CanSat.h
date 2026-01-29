#pragma once
#include <Arduino.h>
#include <BNO085.h>
#include <hardware_pins.h>
#include <SPI.h>
#include <BME280.h>
#include <VEML7700.h>
#include <SGP30.h>
#include <GPS.h>
#include <SdFat.h>
#include <LoRa_E220.h>

class CanSat {
public:
    BNO085 _bno;
    BME280 _bme;
    VEML7700 _veml;
    SGP30 _sgp;
    GPS _gps;
    SdFat _sd;
    SdFile _file;
    LoRa_E220 _433radio;
    CanSat();
    void begin();
    void RadioSetconfig();
    void BMEBegin();
    void BNOBegin();
    void VEMLBegin();
    void SGPBegin();
    void GPSBegin();
    void SDBegin();
    void bus_init(int spi_speed, int i2c_speed, int serial_speed);
    void I2CScan();

    void sendRadioMsg(uint8_t addh, uint8_t addl, uint8_t chan, const void *msg, const uint8_t size);


private:
};