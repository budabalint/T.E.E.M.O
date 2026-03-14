#include "SGP41.h"

SGP41::SGP41() {
    _rawVoc = 0;
    _rawNox = 0;
    _vocIndex = 0;
    _noxIndex = 0;
}

bool SGP41::begin() {
    Wire.begin();
    _sgp41.begin(Wire);

    uint16_t serialNumber[3];
    uint16_t error = _sgp41.getSerialNumber(serialNumber);

    if (error) {
        return false;
    }
    return true;
}

bool SGP41::measureRaw() {
    uint16_t error;

    uint16_t defaultRh = 0x8000;
    uint16_t defaultT = 0x6666;
    error = _sgp41.measureRawSignals(defaultRh, defaultT, _rawVoc, _rawNox);

    if (error) {
        return false;
    }
    return true;
}

bool SGP41::measure() {
    if (!measureRaw()) {
        return false;
    }
    _vocIndex = _vocAlgorithm.process(_rawVoc);
    _noxIndex = _noxAlgorithm.process(_rawNox);
    Serial.println(_vocIndex);
    Serial.println(_noxIndex);
    Serial.println(_rawVoc);
    Serial.println(_rawNox);

    return true;
}

int SGP41::GetCo2() {
    return (int)_noxIndex;
}

int SGP41::GetTVOC() {
    return (int)_vocIndex;
}

int SGP41::GetH2() {
    return (int)_rawVoc;
}

int SGP41::GetEtanol() {
    return (int)_rawNox;
}