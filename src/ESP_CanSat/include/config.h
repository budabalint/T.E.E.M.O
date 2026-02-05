#pragma once
#include <Arduino.h>

const uint8_t SRC_ADDH = 0;
const uint8_t SRC_ADDL = 1;
const uint8_t DEST_ADDH = 0;
const uint8_t DEST_ADDL = 2;
const uint8_t CHANNEL = 23; //410,125 + CHANNEL

const bool SENSOR_DEBUG = false;


const int SPI_SPEED = 8000000; //8Mhz max
const int I2C_SPEED = 400000; // 4Khz max
const int UART_SPEED = 921600;


const int GPS_UART_SPEED = 115200;