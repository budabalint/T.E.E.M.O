#pragma once
#include <Arduino.h>
#include <BNO085.h>
#include <hardware_pins.h>
#include <SPI.h>
#include <BME280.h>
#include <VEML7700.h>
#include <SGP41.h>
#include <GPS.h>
#include <SdFat.h>
#include <LoRa_E220.h>
#include <Ina_Sensor.h>
#include <4Kcam.h>

class CanSat {
public:
    BNO085 _bno;
    BME280 _bme;
    VEML7700 _veml;
    SGP41 _sgp;
    GPS _gps;
    SdFat _sd;
    SdFile _file;
    LoRa_E220 _433radio;
    INA_Sensor _ina3v3;
    INA_Sensor _ina12v;
    Maincam _camera;


    CanSat();
    void begin();
    void LoraRadioSetconfig();
    void RF24RadioSetconfig();
    void SetPinModes();
    void BMEBegin();
    void BNOBegin();
    void VEMLBegin();
    void SGPBegin();
    void GPSBegin();
    void SDBegin();
    void InaBegin();
    void bus_init(int spi_speed, int i2c_speed, int serial_speed);
    void I2CScan();
    void CameraBegin();
    
    bool bme_ok = false;
    bool bno_ok = false;
    bool veml_ok = false;
    bool sgp_ok = false;
    bool gps_ok = false;
    bool ina3v3_ok = false;
    bool ina12v_ok = false;

    void sendRadioMsg(uint8_t addh, uint8_t addl, uint8_t chan, const void *msg, const uint8_t size);


private:
};
