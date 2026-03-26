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

volatile bool receivedFlag = false;

#if defined(ESP8266) || defined(ESP32)
  IRAM_ATTR
#endif
void setFlag(void) {
  receivedFlag = true;
}

void setupRadio() {
  Serial.println("[SX1280] VEVŐ inicializálás (Interrupt mód)...");

  int state = radio.beginFLRC(2486.0, 1300, 2, 0, 16, RADIOLIB_SHAPING_0_5); // +0 dBm elég teszthez
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Hiba az indulásnál: "); Serial.println(state);
    while (1);
  }
  
  uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
  radio.setSyncWord(syncWord, 4);
  radio.setCRC(2);
  
  radio.fixedPacketLengthMode(126);
  radio.setHighSensitivityMode(true);
  radio.setDio1Action(setFlag);

  state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[SX1280] Vevő megszakításos módban, várja a jelet...");
  } else {
    Serial.print("Hiba a vétel indításakor: "); Serial.println(state);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  customSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  setupRadio();
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;

    int state = radio.readData(videoPacket, 126);

    if (state == RADIOLIB_ERR_NONE) {
      uint16_t receivedCounter = (videoPacket[0] << 8) | videoPacket[1];
      Serial.print("[RX] JÓ CSOMAG! Sorszám: ");
      Serial.print(receivedCounter);
      Serial.print(" | RSSI: ");
      Serial.print(radio.getRSSI());
      Serial.println(" dBm");
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println("[RX] Sérült csomag (CRC)!");
    } else {
      Serial.print("[RX] Olvasási hiba: ");
      Serial.println(state);
    }

    // Mivel kiolvastuk az adatot, újra ráparancsolunk a rádióra, hogy figyeljen tovább!
    radio.startReceive();
  }
}