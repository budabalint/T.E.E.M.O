#pragma once
#include <Arduino.h>
#include <IPAddress.h>

const uint8_t SRC_ADDH = 0;
const uint8_t SRC_ADDL = 1;
const uint8_t DEST_ADDH = 0;
const uint8_t DEST_ADDL = 2;
const uint8_t CHANNEL = 67; 

const bool SENSOR_DEBUG = true;
const byte address[6] = "00001";
const uint8_t CHANNEL_24 = 85; 

const int SPI_SPEED = 8000000; 
const int I2C_SPEED = 400000; 
const int UART_SPEED = 921600;
const int GPS_UART_SPEED = 115200;


extern byte mac[6];
extern IPAddress camIP;
extern const int camPort;
extern const char* streamURL;
extern const char* authHeader;

#define BUFFER_SIZE 32768
extern uint8_t* packetBuffer;


uint8_t calculateCRC8(const uint8_t *data, size_t len);