#pragma once
#include <Adafruit_SGP30.h>

class SGP30 {
public:
    SGP30();
    bool begin();
    bool measure();
    bool measureRaw();
    
    int GetCo2();
    int GetTVOC();
    int GetH2();
    int GetEtanol();
private:
    Adafruit_SGP30 _sgp;
};