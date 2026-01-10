#include <CanSat.h>
#include <Arduino.h>
#include <BNO085.h>
#include <hardware_pins.h>
#include <SPI.h>
#include <Wire.h>
#include <SGP30.h>

CanSat::CanSat():
    _bno(BNO_INT, BNO_CS, BNO_RST),
    _bme(),
    _veml(),
    _sgp(),
    _gps()
{
    
};

void CanSat::begin() {
    SPI.begin(Sensor_SPI_SCL, Sensor_SPI_MISO, Sensor_SPI_MOSI);
    Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
    Serial.begin(115200);

    if (_bme.begin()) {
        Serial.println("BME280 sikeresen elindult!");
    } else {
        Serial.println("Hiba: BME280 nem található!");
    }
    
    if (_bno.begin(&SPI)) {
        Serial.println("Sikeres BNO szenzorinicializáció");
        
        _bno.enableSensors(); 
        
    } else {
        Serial.println("Sikertelen BNO inicializáció");
    }

    if (_veml.begin(&Wire)) {
        Serial.println("Sikeres VEML szenzorinicializáció");
    } else {
        Serial.println("Sikertelen VEML inicializáció");
    }

    if (_sgp.begin()) {
        Serial.println("Sikeres SGP30 szenzorinicializáció");
    } else {
        Serial.println("Sikertelen SGP30 inicializáció");
    }

    if (_gps.begin(9600)) {
        Serial.println("Sikeres GPS szenzorinicializáció");
    } else {
        Serial.println("Sikertelen GPS inicializáció");
    }
    
    delay(100);
}