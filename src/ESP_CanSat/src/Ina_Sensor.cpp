#include "Ina_Sensor.h"


INA_Sensor::INA_Sensor(uint8_t address) : _ina(address) {
    _busVoltage = 0.0;
    _current = 0.0;
    _power = 0.0;
    _shuntVoltage = 0.0;
}

bool INA_Sensor::begin() {
    Wire.begin();
    
    if (!_ina.begin()) {
        return false;
    }

    _ina.setMaxCurrentShunt(0.5, 0.082);

    return true;
}

bool INA_Sensor::measure() {
    if (_ina.isConversionReady()) {
        _busVoltage = _ina.getBusVoltage(); // V
        _current = _ina.getCurrent();       // A
        _power = _ina.getPower();           // W
        _shuntVoltage = _ina.getShuntVoltage(); // mV
        
        return true;
    }
    return false;
}


float INA_Sensor::GetVoltage() {
    return _busVoltage;
}

float INA_Sensor::GetCurrent() {
    return _current*1000;
}

float INA_Sensor::GetPower() {
    return _power;
}

float INA_Sensor::GetShuntVoltage() {
    return _shuntVoltage;
}