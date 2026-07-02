/*
 * ESP32-S3 ADÓ (Transmitter) - JAVÍTOTT verzió!
 * Az eredeti, bevált (doc2) indexelt PSRAM-alapú JPEG parsing logika,
 * kiegészítve az SX1280 rádiós küldéssel (doc1-ből).
 *
 * A korábbi "on-the-fly" byte-by-byte állapotgépes parsing (prevByte)
 * hibás volt: bizonyos esetekben nem nullázta a prevByte-ot, ami
 * elcsúsztatta a payload tartalmát -> emiatt már a helyi Serial kimenet
 * sem volt dekódolható, nem csak a rádiós út.
 *
 * Ez a verzió a teljes fájlt PSRAM-ba tölti, és indexelt (data[idx],
 * data[idx+1]) előretekintéssel keresi a SOI (FF D8) és DHT (FF C4)
 * markereket - pontosan úgy, ahogy az eredeti, jól működő szketchben.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <RadioLib.h>
#include <SPI.h>

// ---------- SX1280 Lábkiosztás ----------
#define SPI_SCK       18
#define SPI_MOSI      17
#define SPI_MISO      7
#define SX1280_NSS    15
#define SX1280_DIO1   8
#define SX1280_NRST   16
#define SX1280_BUSY   6

// HSPI-t használunk FSPI helyett, hogy ne ütközzön a belső Flash memóriával!
SPIClass customSPI(HSPI);
Module* module = new Module(SX1280_NSS, SX1280_DIO1, SX1280_NRST, SX1280_BUSY, customSPI);
SX1280 radio(module);

// ---------- Protokoll Konstansok ----------
static const uint8_t  PACKET_SIZE  = 126;
static const uint8_t  PAYLOAD_SIZE = 120;
static const uint8_t  SYNC_BYTE    = 0xFE;
static const uint8_t  TYPE_MJPEG   = 0xDD;
static const uint8_t  TYPE_FEC     = 0xFF;
static const uint16_t SEQ_MAX      = 251;

// ---------- Kimeneti port ----------
#define DATA_SERIAL   Serial
#define DATA_BAUD     115200

// ---------- CRC8-CCITT (poly 0x07) ----------
static uint8_t calcCrc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x80) {
        crc = (uint8_t)((crc << 1) ^ 0x07);
      } else {
        crc = (uint8_t)(crc << 1);
      }
    }
  }
  return crc;
}

// ---------- Adó állapotgép ----------
struct Transmitter {
  int seq        = 0;
  int frameId    = -1;
  int rrtCounter = -1;

  uint8_t fecGroup[8][PACKET_SIZE];
  uint8_t fecCount = 0;

  void sendPacket(Stream &out, const uint8_t *payload, int activeMaskIndex) {
    uint8_t mask = 255;

    if (activeMaskIndex != -1) {
      mask = (uint8_t)activeMaskIndex;
      rrtCounter = 0;
    } else if (rrtCounter >= 0) {
      rrtCounter++;
      if (rrtCounter <= 130) {
        mask = (uint8_t)(120 + rrtCounter);
      } else {
        rrtCounter = -1;
      }
    }

    uint8_t currentFrameId = (uint8_t)((frameId < 0 ? 0 : frameId) % 256);

    uint8_t packet[PACKET_SIZE];
    packet[0] = SYNC_BYTE;
    packet[1] = TYPE_MJPEG;
    packet[2] = (uint8_t)seq;
    packet[3] = currentFrameId;
    packet[4] = mask;
    memcpy(&packet[5], payload, PAYLOAD_SIZE);
    packet[PACKET_SIZE - 1] = calcCrc8(packet, PACKET_SIZE - 1);

    // 1. Küldés soros porton (PC felé)
    out.write(packet, PACKET_SIZE);

    // 2. Küldés rádión (Vevő felé)
    radio.transmit(packet, PACKET_SIZE);

    memcpy(fecGroup[fecCount], packet, PACKET_SIZE);
    fecCount++;
    seq = (seq + 1) % (SEQ_MAX + 1);

    if (fecCount == 8) {
      sendFecPacket(out);
    }
  }

  void sendFecPacket(Stream &out) {
    uint8_t fecId   = 0;
    uint8_t fecMask = 0;
    uint8_t fecPayload[PAYLOAD_SIZE] = {0};

    for (uint8_t i = 0; i < 8; i++) {
      fecId   ^= fecGroup[i][3];
      fecMask ^= fecGroup[i][4];
      for (uint8_t j = 0; j < PAYLOAD_SIZE; j++) {
        fecPayload[j] ^= fecGroup[i][5 + j];
      }
    }

    uint8_t packet[PACKET_SIZE];
    packet[0] = SYNC_BYTE;
    packet[1] = TYPE_FEC;
    packet[2] = (uint8_t)seq;
    packet[3] = fecId;
    packet[4] = fecMask;
    memcpy(&packet[5], fecPayload, PAYLOAD_SIZE);
    packet[PACKET_SIZE - 1] = calcCrc8(packet, PACKET_SIZE - 1);

    out.write(packet, PACKET_SIZE);
    radio.transmit(packet, PACKET_SIZE);

    seq = (seq + 1) % (SEQ_MAX + 1);
    fecCount = 0;
  }
};

Transmitter tx;

// ---------- SX1280 Inicializálás ----------
void setupRadio() {
  int state = radio.beginFLRC(2486.0, 1300, 2, -18, 16, RADIOLIB_SHAPING_0_5);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[Hiba] Rádió indulási hibakód: ");
    Serial.println(state);
    while (1);
  }

  radio.setOutputPower(-18);

  uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
  radio.setSyncWord(syncWord, 4);
  radio.setCRC(2);
  radio.fixedPacketLengthMode(PACKET_SIZE);
  radio.setHighSensitivityMode(true);
}

// ---------- MJPEG streamelő: fájl beolvasása LittleFS-ből, majd csomagolás ----------
// (indexelt, PSRAM-buffer alapú parsing - megegyezik az eredeti, jól működő verzióval)
void streamMjpegFromFS(const char *path, Stream &out) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.println("Nem sikerult megnyitni a fajlt!");
    return;
  }

  size_t fileLen = f.size();
  Serial.printf("Fajlmeret: %u bajt\n", (unsigned)fileLen);

  uint8_t *data = (uint8_t *)heap_caps_malloc(fileLen, MALLOC_CAP_SPIRAM);
  if (!data) {
    Serial.println("Nem sikerult PSRAM buffert foglalni!");
    f.close();
    return;
  }

  size_t readLen = f.read(data, fileLen);
  f.close();

  if (readLen != fileLen) {
    Serial.println("Figyelmeztetes: nem sikerult a teljes fajlt beolvasni!");
  }

  Serial.println("Streameles inditasa... (Innentol TILOS a logolas!)");
  delay(500);
  unsigned long startMs = millis();

  uint8_t payloadBuffer[PAYLOAD_SIZE];
  int payloadIdx    = 0;
  int activeMaskIdx = -1;

  size_t idx = 0;
  while (idx < fileLen) {
    // 1. SOI (Start of Image) keresése - FF D8
    if (idx + 1 < fileLen && data[idx] == 0xFF && data[idx + 1] == 0xD8) {
      tx.frameId++;
      activeMaskIdx = payloadIdx;
      idx += 2;
      continue;
    }

    // 2. DHT (Huffman-tábla) keresése - FF C4
    if (idx + 1 < fileLen && data[idx] == 0xFF && data[idx + 1] == 0xC4) {
      if (idx + 3 < fileLen) {
        uint16_t dhtLen = (uint16_t)((data[idx + 2] << 8) | data[idx + 3]);
        idx += 2 + dhtLen;
        continue;
      }
    }

    // Egyéb bájt -> bekerül a payloadba
    payloadBuffer[payloadIdx++] = data[idx];
    idx++;

    // 3. Payload megtelt -> csomag küldése
    if (payloadIdx == PAYLOAD_SIZE) {
      tx.sendPacket(out, payloadBuffer, activeMaskIdx);
      payloadIdx    = 0;
      activeMaskIdx = -1;
    }
  }

  // 4. Zero-padding az utolsó csonka csomaghoz
  if (payloadIdx > 0) {
    for (int i = payloadIdx; i < PAYLOAD_SIZE; i++) {
      payloadBuffer[i] = 0x00;
    }
    tx.sendPacket(out, payloadBuffer, activeMaskIdx);
  }

  heap_caps_free(data);

  unsigned long elapsedMs = millis() - startMs;
  Serial.printf("\n\nStreameles befejezve! Ido: %lu ms\n", elapsedMs);
}

void setup() {
  Serial.begin(DATA_BAUD);
  delay(1000);

  Serial.println("Indulas...");

  // 1. LÉPÉS: fájlrendszer felcsatolása (false -> nem formáz újra véletlenül)
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed! Kérlek töltsd fel újra a fájlrendszert (Upload Filesystem Image)!");
    return;
  }
  Serial.println("LittleFS mount OK");

  // 2. LÉPÉS: rádió SPI busz és rádió indítása
  customSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SX1280_NSS);
  setupRadio();
  Serial.println("Radio inicializalva.");

  // Stream elindítása
  streamMjpegFromFS("/stream.mjpeg", DATA_SERIAL);

  Serial.println("Kesz, varakozas...");
}

void loop() {
  delay(1000);
}
