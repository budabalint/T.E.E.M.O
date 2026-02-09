#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>

#define CS_PIN 5
#define CE_PIN 4
#define MOSI 7
#define MISO 8
#define CLK 6
#define IRQ 9


RF24 radio(CE_PIN, CS_PIN);

const byte address[6] = "00001";

void setup() {
  Serial.begin(921600);
  SPI.begin(CLK,MISO,MOSI);
  if (!radio.begin()) {
    Serial.println("unsucsessful radio init");
    while (1) {}
  }
  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_MIN); 
  radio.setChannel(85); 
  radio.openWritingPipe(address);
  radio.setPayloadSize(32);
  radio.stopListening();
  radio.setAutoAck(false);
  
  Serial.println("Init finished");
  radio.printDetails();

}

void loop() {
  char text[32] = "Ez egy 32 bájtos tesztcsomag.";
  bool success = radio.write(&text, sizeof(text));

  if (success) {
    Serial.println("Sucess");
  } else {
    Serial.println("error");
  }
}