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
uint8_t packetCounter = 0; // 0..255, automatikusan túlcsordul

// Ugyanaz a CRC8 algoritmus (poly 0x8C), mint a simulator.py calc_crc8() fuggvenyeben,
// hogy a vevo oldal ugyanugy tudja ellenorizni a csomagot.
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
  radio.setCRC(2); // Rádiós szintű (hardveres) CRC, ez a mi sw CRC8-unktól fuggetlen, extra vedelem
  radio.fixedPacketLengthMode(PACKET_LEN);

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
  // Csomag szerkezete (126 bájt):
  // [0]     = 0xDD  -> Start ID
  // [1]     = Számláló (0..255, körbefordul)
  // [2..124]= 0x00   -> kitöltés (123 bájt)
  // [125]   = CRC8

  videoPacket[0] = 0xDD;
  videoPacket[1] = packetCounter;

  for (int i = 2; i < PACKET_LEN - 1; i++) {
    videoPacket[i] = 0x00;
  }

  videoPacket[PACKET_LEN - 1] = calcCRC8(videoPacket, PACKET_LEN - 1);

  int state = radio.transmit(videoPacket, PACKET_LEN);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("Csomag elküldve! Számláló: ");
    Serial.println(packetCounter);
  } else {
    Serial.print("[Hiba] Küldés sikertelen, hibakód: ");
    Serial.println(state);
  }

  packetCounter++; // 0..255 után automatikusan 0-ra ugrik

  delay(10); // Kis szünet a következő csomag előtt
}