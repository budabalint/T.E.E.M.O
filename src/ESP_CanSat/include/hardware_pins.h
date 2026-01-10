#pragma once
#include <Arduino.h>

const int Sensor_SPI_MOSI = 11;
const int Sensor_SPI_MISO = 13;
const int Sensor_SPI_SCL = 12;

const uint8_t BNO_CS = 16;
const uint8_t RADIO_24GHZ_CS = 14;
const uint8_t SD_CARD_CS = 45;
const uint8_t CAM_CS = 15;


const uint8_t SENSOR_I2C_SDA = 1;
const uint8_t SENSOR_I2C_SCL = 2;

const uint8_t THERMAL_CAM_I2C_SDA = 4;
const uint8_t THERMAL_CAM_I2C_SCL = 5;

const uint8_t RADIO_TX = 41;
const uint8_t RADIO_RX = 42;
const uint8_t GPS_TX = 18;
const uint8_t GPS_RX = 17;

const int BNO_INT = 8;
const uint8_t CAM_INT = 7;
const uint8_t RADIO_24GHZ_INT = 9;

const int BNO_RST = 39;
const uint8_t CAM_RST = 38;
const uint8_t RADIO_24GHZ_EN = 40;

const uint8_t RADIO_433MHZ_M1 = 47;
const uint8_t RADIO_433MHZ_M0 = 48;