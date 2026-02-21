#pragma once
#include <Arduino.h>

const uint8_t RX_PIN = 5;
const uint8_t TX_PIN = 6;
const uint8_t AUX_PIN = 4;
const uint8_t M1_PIN = 7;
const uint8_t M0_PIN = 8;

const uint8_t MY_ADDH = 0;
const uint8_t MY_ADDL = 2;
const uint8_t CHANNEL = 23;

const uint8_t SDA_PIN = 21;
const uint8_t SCL_PIN = 47;

#define LED_COUNT   8
#define LED_PIN     48

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C