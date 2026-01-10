#include "SGP30.h"

SGP30::SGP30() : _sgp() {
}

bool SGP30::begin() {
    if (!_sgp.begin()) {
        return false;
    }
    return true;
}

bool SGP30::measure() {
    return _sgp.IAQmeasure();
}

bool SGP30::measureRaw() {
    return _sgp.IAQmeasureRaw();
}

int SGP30::GetCo2() {
    return (int)_sgp.eCO2;
}

int SGP30::GetTVOC() {
    return (int)_sgp.TVOC;
}

int SGP30::GetH2() {
    return (int)_sgp.rawH2;
}

int SGP30::GetEtanol() {
    return (int)_sgp.rawEthanol;
}