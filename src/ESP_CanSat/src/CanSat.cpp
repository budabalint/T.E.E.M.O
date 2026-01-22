#include <CanSat.h>
#include <Arduino.h>
#include <BNO085.h>
#include <hardware_pins.h>
#include <SPI.h>
#include <Wire.h>
#include <SGP30.h>
#include <Create_Packet.h>

#define SPI_FREQ SD_SCK_MHZ(8)

CanSat::CanSat():
    _bno(BNO_INT, BNO_CS, BNO_RST),
    _bme(),
    _veml(),
    _sgp(),
    _gps(),
    _sd(),
    _file()
{
    
};

void CanSat::begin() {
    SPI.begin(Sensor_SPI_SCL, Sensor_SPI_MISO, Sensor_SPI_MOSI);
    SPI.setFrequency(8000000);
    //Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
    //Wire.setClock(1000000);
    Serial.begin(4000000);

    if (_bme.begin()) {
        Serial.println("BME280 sikeresen elindult!");
    } else {
        Serial.println("Hiba: BME280 nem található!");
    }
    
    if (_bno.begin(&SPI)) {
        Serial.println("Sikeres BNO szenzorinicializáció");
        _bno.enableSensors(); 
        digitalWrite(BNO_CS, HIGH);
        
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

    if (!_sd.begin(SdSpiConfig(SD_CARD_CS, SHARED_SPI, SPI_FREQ, &SPI))) {
        Serial.println("sd init failled");
    } else {
        Serial.println("sd init sucess");
    }

    if (!_file.open("data.bin", O_RDWR | O_CREAT | O_TRUNC)) {
        Serial.println("file open failled");
    } else {
        Serial.println("sd init sucess");
    }
    
    delay(100);
}