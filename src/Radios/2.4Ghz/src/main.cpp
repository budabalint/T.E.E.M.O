#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>

#define CE_PIN   4
#define CS_PIN   5
#define MOSI_PIN 7
#define MISO_PIN 8
#define CLK_PIN  6

RF24 radio(CE_PIN, CS_PIN);

const byte address[6] = "00001";

void setup() {
  Serial.begin(921600);
  while (!Serial) {
  }
  Serial.println("RF24 Vevő inicializálása...");
  SPI.begin(CLK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

  if (!radio.begin()) {
    Serial.println("A rádió inicializálása sikertelen! Ellenőrizd a bekötést.");
    while (1) {}
  }

  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_MIN);
  radio.setChannel(30);
  radio.setPayloadSize(32);
  radio.setAutoAck(false);
  radio.openReadingPipe(1, address);
  radio.startListening();
  
  Serial.println("Vevő kész. Várakozás az adatokra...");
  radio.printDetails();
}

void loop() {
  if (radio.available()) {
    char text[32] = {0};
    radio.read(&text, sizeof(text));
    Serial.print("Kapott adat: ");
    Serial.println(text);
  }
}