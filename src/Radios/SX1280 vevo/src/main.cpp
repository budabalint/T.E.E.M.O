/*
 * ESP32-S3 VEVŐ (Receiver)
 * SX1280 rádión fogadja a 126 bájtos adatcsomagokat,
 * hozzáfűzi az RSSI-t (optimalizált lekérdezéssel), és 127 bájtként továbbítja.
 */

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
#define SERIAL_LEN 127
#define SYNC_BYTE  0xFE
#define RSSI_UPDATE_INTERVAL 50

uint8_t videoPacket[PACKET_LEN];
volatile bool receivedFlag = false;

// RSSI optimalizációhoz
uint32_t packetCount = 0;
uint8_t lastRssiByte = 0;
uint32_t packetCounter = 0;
int8_t lastRssi = 0;              // cache-elt utolsó RSSI érték

uint8_t calcCrc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
    while (len--) {
        uint8_t extract = *data++;
        for (uint8_t tempI = 8; tempI; tempI--) {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum) {
                crc ^= 0x8C;
            }
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
  int state = radio.beginFLRC(2440.0, 1300, 2, 0, 16, RADIOLIB_SHAPING_0_5);
  if (state != RADIOLIB_ERR_NONE) {
    while (1); 
  }

  uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
  radio.setSyncWord(syncWord, 4);
  radio.setCRC(2); 

  radio.fixedPacketLengthMode(PACKET_LEN);
  radio.setHighSensitivityMode(true);
  radio.setDio1Action(setFlag);

  radio.startReceive();
}

void setup() {
  Serial.begin(921600);
  delay(100);

  customSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  setupRadio();
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;

    int state = radio.readData(videoPacket, PACKET_LEN);

    bool packetOk = false;
    if (state == RADIOLIB_ERR_NONE && videoPacket[0] == SYNC_BYTE) {
      uint8_t crcCalc = calcCrc8(videoPacket, PACKET_LEN - 1);
      uint8_t crcRecv = videoPacket[PACKET_LEN - 1];
      packetOk = (crcCalc == crcRecv);
    }

    if (packetOk) {
      packetCounter++;
      if (packetCounter % RSSI_UPDATE_INTERVAL == 0) {
        // FONTOS: még startReceive() ELŐTT kell lekérni, különben a chip törli a regisztert!
        float rawRssi = radio.getRSSI();
        int rssiInt = (int)rawRssi;
        if (rssiInt > 0)    rssiInt = 0;
        if (rssiInt < -127) rssiInt = -127;
        lastRssi = (int8_t)rssiInt;
      }
    }

    // Csak EZUTÁN indítjuk újra a vételt
    radio.startReceive();

    if (packetOk) {
      uint8_t rssiByte = (uint8_t)(-lastRssi);

      uint8_t outBuffer[SERIAL_LEN];
      memcpy(outBuffer, videoPacket, PACKET_LEN);
      outBuffer[PACKET_LEN] = (255-rssiByte);

      Serial.write(outBuffer, SERIAL_LEN);
    }
  }
}