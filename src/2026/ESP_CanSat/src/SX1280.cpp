#include "SX1280.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_task_wdt.h"
#include <SPI.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

extern SemaphoreHandle_t spiMutex;

VideoRadio::VideoRadio(uint8_t nss, uint8_t dio1, uint8_t nrst, uint8_t busy, SPIClass* spiObj) 
    : _nss(nss), _dio1(dio1), _nrst(nrst), _busy(busy) 
{
    if (spiObj != nullptr) {
        _module = new Module(nss, dio1, nrst, busy, *spiObj);
    } else {
        _module = new Module(nss, dio1, nrst, busy);
    }
    
    _radio = new SX1280(_module);
    
    seq = 0;
    frameId = -1;
    rrtCounter = -1;
    fecCount = 0;
    memset(fecGroup, 0, sizeof(fecGroup));
}

VideoRadio::~VideoRadio() {
    delete _radio;
    delete _module;
}

bool VideoRadio::begin() {
    if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
        int state = _radio->beginFLRC(2485, 1300, 2, 0, 16, RADIOLIB_SHAPING_0_5);
        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[RÁDIÓ] Hiba az inicializáláskor! Kód: %d\n", state);
            xSemaphoreGive(spiMutex);
            return false;
        }

        _radio->setOutputPower(0); 
        uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
        _radio->setSyncWord(syncWord, 4);
        _radio->setCRC(2);
        _radio->fixedPacketLengthMode(VIDEO_PACKET_SIZE); 
        _radio->setHighSensitivityMode(true);
        
        xSemaphoreGive(spiMutex);
        return true;
    }
    return false; 
}

uint8_t VideoRadio::calcCrc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        uint8_t extract = *data++;
        for (uint8_t tempI = 8; tempI; tempI--) {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum) crc ^= 0x8C;
            extract >>= 1;
        }
    }
    return crc;
}

void VideoRadio::sendPacket(Stream &out, const uint8_t *payload, int activeMaskIndex) {
    uint8_t mask = 255;
    if (activeMaskIndex != -1) {
        mask = (uint8_t)activeMaskIndex;
        rrtCounter = 0;
    } else if (rrtCounter >= 0) {
        rrtCounter++;
        if (rrtCounter <= 130) mask = (uint8_t)(120 + rrtCounter);
        else rrtCounter = -1;
    }

    uint8_t currentFrameId = (uint8_t)((frameId < 0 ? 0 : frameId) % 256);
    uint8_t packet[VIDEO_PACKET_SIZE];
    packet[0] = VIDEO_SYNC_BYTE; 
    packet[1] = VIDEO_TYPE_MJPEG;
    packet[2] = (uint8_t)seq;
    packet[3] = currentFrameId;
    packet[4] = mask;
    memcpy(&packet[5], payload, VIDEO_PAYLOAD_SIZE);
    packet[VIDEO_PACKET_SIZE - 1] = calcCrc8(packet, VIDEO_PACKET_SIZE - 1);

    static int pktCount = 0;
    pktCount++;
    
    // Csak az első párnál, illetve ritkábban logolunk, hogy ne árasszuk el a Serialt
    if (pktCount < 5 || pktCount % 500 == 0) {
        Serial.printf("  [MJPEG Küldés] pktCount=%d, SPI kikérése...\n", pktCount);
    }

    if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
        if (pktCount < 5 || pktCount % 500 == 0) Serial.println("  [MJPEG Küldés] transmit() hívása...");
        
        int state = _radio->transmit(packet, VIDEO_PACKET_SIZE);
        
        if (pktCount < 5 || pktCount % 500 == 0) Serial.printf("  [MJPEG Küldés] transmit() vége, state=%d\n", state);
        xSemaphoreGive(spiMutex);
    }

    memcpy(fecGroup[fecCount], packet, VIDEO_PACKET_SIZE);
    fecCount++;
    seq = (seq + 1) % (VIDEO_SEQ_MAX + 1);

    if (fecCount == 8) sendFecPacket(out);
}

void VideoRadio::sendFecPacket(Stream &out) {
    uint8_t fecId = 0, fecMask = 0;
    uint8_t fecPayload[VIDEO_PAYLOAD_SIZE] = {0};

    for (uint8_t i = 0; i < 8; i++) {
        fecId ^= fecGroup[i][3];
        fecMask ^= fecGroup[i][4];
        for (uint8_t j = 0; j < VIDEO_PAYLOAD_SIZE; j++) fecPayload[j] ^= fecGroup[i][5 + j];
    }

    uint8_t packet[VIDEO_PACKET_SIZE];
    packet[0] = VIDEO_SYNC_BYTE;
    packet[1] = VIDEO_TYPE_FEC;
    packet[2] = (uint8_t)seq;
    packet[3] = fecId;
    packet[4] = fecMask;
    memcpy(&packet[5], fecPayload, VIDEO_PAYLOAD_SIZE);
    packet[VIDEO_PACKET_SIZE - 1] = calcCrc8(packet, VIDEO_PACKET_SIZE - 1);

    if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
        _radio->transmit(packet, VIDEO_PACKET_SIZE);
        xSemaphoreGive(spiMutex);
    }

    seq = (seq + 1) % (VIDEO_SEQ_MAX + 1);
    fecCount = 0;
}

void VideoRadio::streamMjpegFromFS(const char *path, Stream &out, StreamIdleCallback idleCb) {
    Serial.println("\n[VIDEO_STREAM] ---- START ----");
    
    Serial.printf("[VIDEO_STREAM] Fájl keresése: %s\n", path);
    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.println("[VIDEO_STREAM] HIBA: Fájl nem található vagy nem nyitható meg!");
        return;
    }

    size_t fileLen = f.size();
    Serial.printf("[VIDEO_STREAM] Fájlméret: %d byte\n", fileLen);
    
    if (fileLen == 0) {
        Serial.println("[VIDEO_STREAM] HIBA: Üres fájl!");
        f.close();
        return;
    }

    Serial.println("[VIDEO_STREAM] PSRAM foglalás...");
    uint8_t *data = (uint8_t *)heap_caps_malloc(fileLen, MALLOC_CAP_SPIRAM);
    
    if (!data) {
        Serial.println("[VIDEO_STREAM] HIBA: Nincs elég PSRAM!");
        f.close();
        return;
    }
    Serial.println("[VIDEO_STREAM] PSRAM lefoglalva. Fájl olvasása darabokban...");

    size_t bytesRead = 0;
    const size_t chunkSize = 16384; 
    
    while (bytesRead < fileLen) {
        size_t toRead = fileLen - bytesRead;
        if (toRead > chunkSize) toRead = chunkSize;
        
        size_t res = f.read(data + bytesRead, toRead);
        if (res == 0) break; 
        bytesRead += res;
        
        esp_task_wdt_reset(); 
        vTaskDelay(pdMS_TO_TICKS(2)); // Kicsit megnövelt szünet
    }
    f.close();
    Serial.printf("[VIDEO_STREAM] Fájl beolvasva: %d byte.\n", bytesRead);

    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.println("[VIDEO_STREAM] Kezdődik a parseolás és küldés...");

    uint8_t payloadBuffer[VIDEO_PAYLOAD_SIZE];
    int payloadIdx = 0, activeMaskIdx = -1;
    size_t idx = 0;
    int packetCounter = 0;
    
    while (idx < fileLen) {
        // Biztonsági WDT reset minden 1000 iterációnál
        if (idx % 1000 == 0) {
            esp_task_wdt_reset();
            vTaskDelay(1);
        }

        if (idx + 1 < fileLen && data[idx] == 0xFF && data[idx + 1] == 0xD8) {
            frameId++;
            activeMaskIdx = payloadIdx;
            idx += 2;
            continue;
        }

        if (idx + 1 < fileLen && data[idx] == 0xFF && data[idx + 1] == 0xC4) {
            if (idx + 3 < fileLen) {
                uint16_t dhtLen = (uint16_t)((data[idx + 2] << 8) | data[idx + 3]);
                idx += 2 + dhtLen;
                continue;
            }
        }

        payloadBuffer[payloadIdx++] = data[idx];
        idx++;

        if (payloadIdx == VIDEO_PAYLOAD_SIZE) {
            sendPacket(out, payloadBuffer, activeMaskIdx);
            payloadIdx = 0;
            activeMaskIdx = -1;

            packetCounter++;
            if (packetCounter % 50 == 0) {   
                esp_task_wdt_reset(); 
                vTaskDelay(pdMS_TO_TICKS(1)); 
            }
            if (idleCb != nullptr) {
                idleCb();
            }
        }
    }

    if (payloadIdx > 0) {
        for (int i = payloadIdx; i < VIDEO_PAYLOAD_SIZE; i++) payloadBuffer[i] = 0x00;
        sendPacket(out, payloadBuffer, activeMaskIdx);
    }

    heap_caps_free(data);
    Serial.println("[VIDEO_STREAM] ---- STREAM KÉSZ ----");
}


void VideoRadio::transmitRawPadded(const uint8_t* data, size_t len) {
    uint8_t packet[VIDEO_PACKET_SIZE] = {0}; // Alapból feltöltjük nullákkal
    size_t copyLen = (len < VIDEO_PACKET_SIZE) ? len : VIDEO_PACKET_SIZE;
    memcpy(packet, data, copyLen);
    
    if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
        _radio->transmit(packet, VIDEO_PACKET_SIZE);
        xSemaphoreGive(spiMutex);
    }
}