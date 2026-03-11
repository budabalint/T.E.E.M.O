#include <SPI.h>
#include <Arduino.h>
#include 

const uint8_t data_RX = 17;
const uint8_t data_TX = 18;
const float lattitude = 47.12234;
const float longitude = 18.1335234;
const float altitude = 202.2;
const uint8_t Rotater_motor_forward = 10; 
const uint8_t Rotater_motor_backwards = 11; 
const uint8_t Lifter_motor_forward = 12; 
const uint8_t Lifter_motor_backwards = 13; 

void setup() {
  pinMode(data_RX, INPUT);
  pinMode(data_TX, OUTPUT);
  Serial1.begin(115200, -1, data_RX, data_TX);
  Serial.read();
}

void loop() {
  
}