#pragma once
#include <TinyGPS++.h>
#include <Arduino.h>
#include <hardware_pins.h>

class GPS {
public:
    GPS();
    bool begin(unsigned long baud = 115200, uint8_t rx = GPS_RX, uint8_t tx = GPS_TX);
    bool encode();

    double getLat();
    double getLng();
    double getAltitude(); 
    double getSpeed();    
    double getCourse();
    uint32_t getSatellites();
    double getHDOP();

    bool isValid();
    bool isUpdated();

private:
    TinyGPSPlus _gps;
};