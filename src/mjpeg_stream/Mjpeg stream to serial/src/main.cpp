/*
 * ESP32-S3 MJPEG streamer LittleFS-ből a beépített USB Serial porton keresztül
 * A Python (DDEEFF_Transmitter) protokollal 100%-ban kompatibilis:
 *  - 126 bájtos csomagok (5 bájt fejléc + 120 bájt payload + 1 bájt CRC8)
 *  - SYNC = 0xFE, TYPE_MJPEG = 0xDD, TYPE_FEC = 0xFF
 *  - SOI (FF D8) stripping + FRAME_ID növelés
 *  - DHT (FF C4) stripping (marker + teljes DHT tartalom kihagyása)
 *  - RRT állapotgép a MASK bájthoz (0-119 aktív, 120-250 relatív távolság, 255 nincs infó)
 *  - Modulo-9 FEC: minden 8. adatcsomag után egy XOR paritáscsomag (0xFF)
 *  - CRC8-CCITT, poly 0x07, init 0x00, nincs reflektálás
 *
 * FONTOS: minden - a bináris adat is - a beépített USB Serial-ra megy (ugyanaz
 * a port, amit a PC lát). Emiatt a streamelés IDEJE ALATT tilos bármilyen
 * Serial.print/println-t hívni, mert az szétverné a bájtfolyamot a vevő
 * (PC-s dekóder) oldalán. A logok ezért csak streamelés előtt/után futnak.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

// ---------- Konstansok (a Python szkripttel megegyezően) ----------
static const uint8_t  PACKET_SIZE  = 126;
static const uint8_t  PAYLOAD_SIZE = 120;
static const uint8_t  SYNC_BYTE    = 0xFE;
static const uint8_t  TYPE_MJPEG   = 0xDD;
static const uint8_t  TYPE_FEC     = 0xFF;
static const uint16_t SEQ_MAX      = 251;   // seq: 0..251 -> modulo 252

// ---------- Kimeneti port: a beépített USB Serial ----------
#define DATA_SERIAL   Serial
#define DATA_BAUD     115200   // a natív USB-CDC-nél ez csak formalitás
                                 // (a tényleges átvitel USB sebességgel megy),
                                 // de a Python oldali port.open()-hez illesszd

// ---------- CRC8-CCITT (poly 0x07, init 0x00, no reflect) ----------
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
  int frameId    = -1;   // első SOI-nál lesz 0
  int rrtCounter = -1;   // Relatív Reset Távolság, csomagokban mérve

  uint8_t fecGroup[8][PACKET_SIZE];
  uint8_t fecCount = 0;

  // Egy 0xDD adatcsomag összeállítása és küldése
  void sendPacket(Stream &out, const uint8_t *payload, int activeMaskIndex) {
    uint8_t mask = 255; // alapértelmezett: nincs infó

    if (activeMaskIndex != -1) {
      // Aktív SOI marker van ebben a csomagban (0-119. pozíción)
      mask = (uint8_t)activeMaskIndex;
      rrtCounter = 0;
    } else if (rrtCounter >= 0) {
      rrtCounter++;
      if (rrtCounter <= 130) {
        mask = (uint8_t)(120 + rrtCounter); // relatív távolság: 121-250
      } else {
        rrtCounter = -1; // túl messze -> vissza 255-re
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

    out.write(packet, PACKET_SIZE);

    // FEC csoportba mentés
    memcpy(fecGroup[fecCount], packet, PACKET_SIZE);
    fecCount++;

    seq = (seq + 1) % (SEQ_MAX + 1);

    if (fecCount == 8) {
      sendFecPacket(out);
    }
  }

  // A 8 összegyűjtött adatcsomagból XOR paritáscsomag (0xFF) generálása és küldése
  void sendFecPacket(Stream &out) {
    uint8_t fecId   = 0;
    uint8_t fecMask = 0;
    uint8_t fecPayload[PAYLOAD_SIZE] = {0};

    for (uint8_t i = 0; i < 8; i++) {
      fecId   ^= fecGroup[i][3]; // FRAME_ID XOR
      fecMask ^= fecGroup[i][4]; // MASK XOR
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

    seq = (seq + 1) % (SEQ_MAX + 1);
    fecCount = 0;
  }
};

Transmitter tx;

// ---------- MJPEG streamelő: fájl beolvasása LittleFS-ből, majd csomagolás ----------
void streamMjpegFromFS(const char *path, Stream &out) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.println("Nem sikerult megnyitni a fajlt!");
    return;
  }

  size_t fileLen = f.size();
  Serial.printf("Fajlmeret: %u bajt\n", (unsigned)fileLen);

  // Teljes fájl PSRAM-ba töltése (8 MB PSRAM bőven elég egy 6 MB-os fájlhoz)
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

  Serial.println("Streameles inditasa...");
  unsigned long startMs = millis();

  uint8_t payloadBuffer[PAYLOAD_SIZE];
  int payloadIdx    = 0;
  int activeMaskIdx = -1;

  size_t idx = 0;
  while (idx < fileLen) {
    // 1. SOI (Start of Image) keresése - FF D8
    if (idx + 1 < fileLen && data[idx] == 0xFF && data[idx + 1] == 0xD8) {
      tx.frameId++;
      activeMaskIdx = payloadIdx; // aktuális bájthely a payloadon belül
      idx += 2;                  // a markert kihagyjuk
      continue;
    }

    // 2. DHT (Huffman-tábla) keresése - FF C4
    if (idx + 1 < fileLen && data[idx] == 0xFF && data[idx + 1] == 0xC4) {
      if (idx + 3 < fileLen) {
        uint16_t dhtLen = (uint16_t)((data[idx + 2] << 8) | data[idx + 3]);
        idx += 2 + dhtLen; // marker + hossz + tartalom kihagyása
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
  Serial.printf("Streameles befejezve! Ido: %lu ms\n", elapsedMs);
}

void setup() {
  Serial.begin(DATA_BAUD);
  delay(1000);

  // --- Ez a blokk MÉG a streamelés előtt fut, itt szabad logolni ---
  Serial.println("Indulas...");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }
  Serial.println("LittleFS mount OK");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("Fajl: %s, meret: %u\n", file.name(), (unsigned)file.size());
    file = root.openNextFile();
  }

  // Kis szünet, hogy a PC oldali script biztosan el tudja indítani a portot
  // olvasásra, mielőtt az első bináris bájt megérkezik.
  delay(500);

  // --- Innentől a streamMjpegFromFS() BELSEJÉBEN sem szabad Serial.print-et hívni ---
  streamMjpegFromFS("/stream.mjpeg", DATA_SERIAL);

  // --- A streamelés véget ért, innentől megint szabad logolni ---
  Serial.println("Kesz, varakozas...");
}

void loop() {
  delay(1000);
}
