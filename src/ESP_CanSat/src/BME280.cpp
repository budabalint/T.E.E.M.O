#include <BME280.h>

BME280::BME280() : bme() {}

bool BME280::begin() {
    return bme.begin(0x76, &Wire);
}

float BME280::readTemperature() {
    return bme.readTemperature();
}

float BME280::readHumidity() {
    return bme.readHumidity();
}

float BME280::readPressure() {
    return bme.readPressure();
}
