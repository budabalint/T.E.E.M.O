#include <RadioLib.h>

#define SX1280_NSS    5
#define SX1280_DIO1   34
#define SX1280_NRST   14
#define SX1280_BUSY   35

#define EBYTE_RX_EN   32 
#define EBYTE_TX_EN   33 

SX1280 radio = new Module(SX1280_NSS, SX1280_DIO1, SX1280_NRST, SX1280_BUSY);

uint8_t videoPacket[126];
uint16_t packetCounter = 0;

void setupRadio() {
  Serial.println("[SX1280] Inicializálás megkezdése...");

  radio.setRfSwitchPins(EBYTE_RX_EN, EBYTE_TX_EN);

  // 2486.0 : Frekvencia
  // 1300   : Adatsebesség kbps-ban max 1300
  // 2      : Kódolási arány (CR). 1/2-es FEC kódolás (Max védelem a H.264-nek)
  // 0      : TX Power (0 dBm a SX1280-ból -> 27 dBm/500mW a külső erősítőből!)
  // 16     : Preamble hossz 
  // RADIOLIB_SHAPING_0_5 : Gaussian szűrő
  int state = radio.beginFLRC(2486.0, 1300, 2, 0, 16, RADIOLIB_SHAPING_0_5);
  
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[Hiba] Rádió indulási hibakód: ");
    Serial.println(state);
    while (1);
  }
  
  radio.setOutputPower(-18); // -18 és 0 között legyen 

  radio.setHighSensitivityMode(true);

  uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
  radio.setSyncWord(syncWord, 4);

  radio.setCRC(2);

  radio.variablePacketLengthMode(127);

  Serial.println("[SX1280] Sikeresen konfigurálva az 500mW-os videóátvitelhez!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  setupRadio();
}

void loop() {
  videoPacket[0] = (packetCounter >> 8) & 0xFF; // Sorszám felső 8 bitje
  videoPacket[1] = packetCounter & 0xFF;        // Sorszám alsó 8 bitje
  
  for(int i = 2; i < 126; i++) {
    videoPacket[i] = 'X'; 
  }
  int state = radio.transmit(videoPacket, 126);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("Csomag elküldve! Sorszám: ");
    Serial.println(packetCounter);
  } else {
    Serial.print("[Hiba] Küldés sikertelen, hibakód: ");
    Serial.println(state);
  }

  packetCounter++; // Sorszám növelése a következő csomaghoz

  delay(10); 
}