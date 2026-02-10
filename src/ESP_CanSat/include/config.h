#pragma once
#include <Arduino.h>

const uint8_t SRC_ADDH = 0;
const uint8_t SRC_ADDL = 1;
const uint8_t DEST_ADDH = 0;
const uint8_t DEST_ADDL = 2;
const uint8_t CHANNEL = 23; //410,125 + CHANNEL

const bool SENSOR_DEBUG = false;

const byte address[6] = "00001";
const uint8_t CHANNEL_24 = 85; // 2400Mhz + CHANNEL_24

const int SPI_SPEED = 8000000; //8Mhz max
const int I2C_SPEED = 400000; // 4Khz max
const int UART_SPEED = 921600;


const int GPS_UART_SPEED = 115200;

//main cam
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress camIP(192, 168, 0, 144);
const int camPort = 554;
const char* streamURL = "rtsp://192.168.0.144/stream=1";
const char* authHeader = "Authorization: Basic cm9vdDphYWFh"; //pass:aaaa

#define BUFFER_SIZE 32768
uint8_t* packetBuffer; 