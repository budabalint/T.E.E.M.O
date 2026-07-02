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

#define PACKET_LEN 126

uint8_t videoPacket[PACKET_LEN];

volatile bool receivedFlag = false;

// CRC8 algoritmus (poly 0x8C)
uint8_t calcCRC8(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    uint8_t extract = data[i];
    for (int j = 0; j < 8; j++) {
      uint8_t sum_bit = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum_bit) crc ^= 0x8C;
      extract >>= 1;
    }
  }
  return crc;
}

#if defined(ESP8266) || defined(ESP32)
  IRAM_ATTR
#endif
void setFlag(void) {
  receivedFlag = true;
}

void setupRadio() {
  int state = radio.beginFLRC(2486.0, 1300, 2, -18, 16, RADIOLIB_SHAPING_0_5);
  if (state != RADIOLIB_ERR_NONE) {
    // Hiba esetén végtelen ciklus (villogtathatsz egy LED-et itt, ha kell)
    while (1);
  }

  uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
  radio.setSyncWord(syncWord, 4);
  radio.setCRC(2); // Rádiós szintű (hardveres) CRC

  radio.fixedPacketLengthMode(PACKET_LEN);
  radio.setHighSensitivityMode(true);
  radio.setDio1Action(setFlag);

  radio.startReceive();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  customSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  setupRadio();
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;

    int state = radio.readData(videoPacket, PACKET_LEN);

    if (state == RADIOLIB_ERR_NONE) {
      // Csak akkor foglalkozunk vele, ha a Start ID megfelelő
      if (videoPacket[0] == 0xDD) {
        
        uint8_t crcCalc = calcCRC8(videoPacket, PACKET_LEN - 1);
        uint8_t crcRecv = videoPacket[PACKET_LEN - 1];

        // Ha a CRC is jó, kiküldjük soros porton a nyers bájtokat
        if (crcCalc == crcRecv) {
          
          // RSSI lekérése (float), és konvertálása 8-bites előjeles egésszé (pl. -85 dBm -> -85)
          float rssi_float = radio.getRSSI();
          int8_t rssi = (int8_t)rssi_float; 

          // 1. Kiküldjük a 126 bájtos csomagot
          Serial.write(videoPacket, PACKET_LEN);
          
          // 2. Hozzácsapjuk az 1 bájtos RSSI értéket a végéhez
          Serial.write((uint8_t*)&rssi, 1);
        }
      }
    }

    // Újra ráparancsolunk a rádióra, hogy figyeljen tovább!
    radio.startReceive();
  }
}