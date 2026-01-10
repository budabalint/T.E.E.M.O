#include "GPS.h"

GPS::GPS() : _gps() {
}

bool GPS::begin(unsigned long baud, uint8_t rx, uint8_t tx) {
    Serial1.begin(baud, SERIAL_8N1, rx, tx);
    return true; 
}

bool GPS::encode() {
    bool newData = false;
    while (Serial1.available() > 0) {
        if (_gps.encode(Serial1.read())) {
            newData = true;
        }
    }
    return newData;
}

double GPS::getLat() {
    return _gps.location.isValid() ? _gps.location.lat() : 0.0;
}

double GPS::getLng() {
    return _gps.location.isValid() ? _gps.location.lng() : 0.0;
}

double GPS::getAltitude() {
    return _gps.altitude.isValid() ? _gps.altitude.meters() : 0.0;
}

double GPS::getSpeed() {
    return _gps.speed.isValid() ? _gps.speed.kmph() : 0.0;
}

double GPS::getCourse() {
    return _gps.course.isValid() ? _gps.course.deg() : 0.0;
}

uint32_t GPS::getSatellites() {
    return _gps.satellites.isValid() ? _gps.satellites.value() : 0;
}

double GPS::getHDOP() {
    return _gps.hdop.isValid() ? _gps.hdop.hdop() : 0.0;
}

bool GPS::isValid() {
    return _gps.location.isValid();
}

bool GPS::isUpdated() {
    return _gps.location.isUpdated();
}