#include "VEML7700.h"

VEML7700::VEML7700() : _veml() {}

bool VEML7700::begin(TwoWire* wire) {
    if (!_veml.begin(wire)) {
        return false;
    }

    _veml.setLowThreshold(10000);
    _veml.setHighThreshold(20000);
    _veml.interruptEnable(true);

    return true;
}

void VEML7700::setGain(uint8_t gain) {
    _veml.setGain(gain);
}

void VEML7700::setIntegrationTime(uint8_t it) {
    _veml.setIntegrationTime(it);
}

void VEML7700::setLowThreshold(uint16_t value) {
    _veml.setLowThreshold(value);
}

void VEML7700::setHighThreshold(uint16_t value) {
    _veml.setHighThreshold(value);
}

void VEML7700::enableInterrupt(bool enable) {
    _veml.interruptEnable(enable);
}

uint16_t VEML7700::readALS() {
    return _veml.readALS();
}

uint16_t VEML7700::readWhite() {
    return _veml.readWhite();
}

float VEML7700::readLux() {
    return _veml.readLux();
}

uint16_t VEML7700::getInterruptStatus() {
    return _veml.interruptStatus();
}

bool VEML7700::isLowThresholdTriggered() {
    return _veml.interruptStatus() & VEML7700_INTERRUPT_LOW;
}

bool VEML7700::isHighThresholdTriggered() {
    return _veml.interruptStatus() & VEML7700_INTERRUPT_HIGH;
}

uint8_t VEML7700::getGain() {
    return _veml.getGain();
}

uint8_t VEML7700::getIntegrationTime() {
    return _veml.getIntegrationTime();
}
