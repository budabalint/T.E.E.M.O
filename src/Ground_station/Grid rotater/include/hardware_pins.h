#pragma once
#include <Arduino.h>

const uint8_t data_RX = 16;
const uint8_t data_TX = 15;
const uint8_t data2_TX = 17;
const uint8_t data2_RX = 18;

const uint8_t BNO_SDA = 4;
const uint8_t BNO_SCL = 5;

const uint8_t relay1 = 35;
const uint8_t relay2 = 36;
const uint8_t relay3 = 37;
const uint8_t relay4 = 38;

const uint8_t ANALOG_BUTTON = 13; // FIGYELEM: 38-as foglalt volt a relay4-nek!
const uint8_t MODE_SELECTER_BUTTON = 40; // HIGH = Manuális, LOW = Auto

const uint8_t NEOPIXEL_PIN = 48; // NeoPixel adatláb