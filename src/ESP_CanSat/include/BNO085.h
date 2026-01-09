#pragma once
#include <Arduino.h>
#include <hardware_pins.h>
#include <Adafruit_BNO08x.h>

struct Vector3 {
    float x;
    float y;
    float z;
};

struct Rotacio {
    float x, y, z, w;
};

struct EulerRotacio {
    float roll, pitch, yaw;
};

class BNO085 {

public:
    BNO085(uint8_t bno_int, uint8_t cs, uint8_t rst);
    bool begin(SPIClass *spi);

    void enableSensors();
    void update();
    bool hasNewData();

    Rotacio getRotation();
    EulerRotacio getEulerAngle();
    
    Vector3 getLinearAcceleration();
    Vector3 getGyroscope();
    Vector3 getGravity();
    Vector3 getMagnetometer();

    float getRoll();
    float getPitch();
    float getYaw();

private:
    Adafruit_BNO08x bno;
    uint8_t _int, _cs, _rst;

    sh2_SensorValue_t _sensorValue;
    
    Rotacio _quatData;
    Vector3 _linAccelData;
    Vector3 _gyroData;
    Vector3 _gravityData;
    Vector3 _magData;
    
    bool _newDataAvailable;
};