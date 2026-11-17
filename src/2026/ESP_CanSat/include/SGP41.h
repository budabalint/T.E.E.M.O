#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2cSgp41.h>
#include <VOCGasIndexAlgorithm.h>
#include <NOxGasIndexAlgorithm.h>

class SGP41 {
public:
    SGP41();
    bool begin();
    bool measure();
    bool measureRaw();
    
    int GetCo2();
    int GetTVOC();
    int GetH2();
    int GetEtanol();

private:
    SensirionI2CSgp41 _sgp41;
    VOCGasIndexAlgorithm _vocAlgorithm;
    NOxGasIndexAlgorithm _noxAlgorithm;

    uint16_t _rawVoc;
    uint16_t _rawNox;
    int32_t _vocIndex;
    int32_t _noxIndex;
};