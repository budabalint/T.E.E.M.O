#pragma once
#include <Adafruit_VEML7700.h>
#include <Wire.h>

class VEML7700 {
public:
    VEML7700();

    bool begin(TwoWire* wire = &Wire);

    void setGain(uint8_t gain);
    void setIntegrationTime(uint8_t it);

    void setLowThreshold(uint16_t value);
    void setHighThreshold(uint16_t value);
    void enableInterrupt(bool enable);

    uint16_t readALS();
    uint16_t readWhite();
    float readLux();

    uint16_t getInterruptStatus();
    bool isLowThresholdTriggered();
    bool isHighThresholdTriggered();

    uint8_t getGain();
    uint8_t getIntegrationTime();

private:
    Adafruit_VEML7700 _veml;
};
