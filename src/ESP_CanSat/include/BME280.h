#pragma once
#include <Arduino.h>
#include <Adafruit_BME280.h>

class BME280 {
public:
    BME280();
    bool begin();
    float readTemperature();
    float readHumidity();
    float readPressure();
private:
    Adafruit_BME280 bme;
};