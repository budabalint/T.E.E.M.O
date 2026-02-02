#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <INA226.h>

class INA_Sensor {
public:
    INA_Sensor(uint8_t address); // Alapértelmezett I2C cím: 0x40
    bool begin();
    bool measure();
    
    float GetVoltage();
    float GetCurrent();
    float GetPower();
    float GetShuntVoltage();

private:
    INA226 _ina;
    float _busVoltage;
    float _current;
    float _power;
    float _shuntVoltage;
};