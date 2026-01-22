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

class CanSat {
public:
    BNO085 _bno;
    BME280 _bme;
    VEML7700 _veml;
    SGP30 _sgp;
    GPS _gps;
    SdFat _sd;
    SdFile _file;
    CanSat();
    void begin();


private:
};