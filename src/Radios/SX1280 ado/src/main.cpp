#include <RadioLib.h>
#include <SPI.h>

#define SPI_SCK       18
#define SPI_MOSI      17
#define SPI_MISO      7

#define SX1280_NSS    15
#define SX1280_DIO1   8
#define SX1280_NRST   16
#define SX1280_BUSY   6

SPIClass customSPI(FSPI); 

Module* module = new Module(SX1280_NSS, SX1280_DIO1, SX1280_NRST, SX1280_BUSY, customSPI);
SX1280 radio(module);

uint8_t videoPacket[126];
uint16_t packetCounter = 0;

void setupRadio() {
  Serial.println("[SX1280] Inicializálás megkezdése...");
  int state = radio.beginFLRC(2486.0, 1300, 2, -18, 16, RADIOLIB_SHAPING_0_5);
  
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[Hiba] Rádió indulási hibakód: ");
    Serial.println(state);
    while (1); // Végtelen ciklus, ha hiba van
  }

  radio.setOutputPower(-18); 

  uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
  radio.setSyncWord(syncWord, 4);
  radio.setCRC(2);
  radio.fixedPacketLengthMode(126);

  radio.setHighSensitivityMode(true);

  Serial.println("[SX1280] Sikeresen konfigurálva ADÓ módra, MINIMÁL teljesítménnyel (-18 dBm)!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  customSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SX1280_NSS);
  
  setupRadio();
}

void loop() {
  // Csomag fejléce: Sorszám (16 bit)
  videoPacket[0] = (packetCounter >> 8) & 0xFF; // Felső 8 bit
  videoPacket[1] = packetCounter & 0xFF;        // Alsó 8 bit

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

  packetCounter++; // Következő sorszám

  delay(10); // Kis szünet a következő csomag előtt
}