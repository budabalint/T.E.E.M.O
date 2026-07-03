#include "SX1280.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t spiMutex;

VideoRadio::VideoRadio(uint8_t nss, uint8_t dio1, uint8_t nrst, uint8_t busy) 
    : _nss(nss), _dio1(dio1), _nrst(nrst), _busy(busy) 
{
    _module = new Module(nss, dio1, nrst, busy);
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
    
    int state = _radio->beginFLRC(2440, 1300, 2, -18, 16, RADIOLIB_SHAPING_0_5);
    if (state != RADIOLIB_ERR_NONE) {
        return false;
    }

    _radio->setOutputPower(-18);
    uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
    _radio->setSyncWord(syncWord, 4);
    _radio->setCRC(2);
    _radio->fixedPacketLengthMode(VIDEO_PACKET_SIZE);
    _radio->setHighSensitivityMode(true);
    
    return true;
}

uint8_t VideoRadio::calcCrc8(const uint8_t *data, size_t len) {
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

    //out.write(packet, VIDEO_PACKET_SIZE);

    // BIZTONSÁGOS SPI HASZNÁLAT (Védelem az SD kártya írástól)
    if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
        _radio->transmit(packet, VIDEO_PACKET_SIZE);
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

    // out.write(packet, VIDEO_PACKET_SIZE);

    // BIZTONSÁGOS SPI HASZNÁLAT
    if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
        _radio->transmit(packet, VIDEO_PACKET_SIZE);
        xSemaphoreGive(spiMutex);
    }

    seq = (seq + 1) % (VIDEO_SEQ_MAX + 1);
    fecCount = 0;
}

void VideoRadio::streamMjpegFromFS(const char *path, Stream &out) {
    File f = LittleFS.open(path, "r");
    if (!f) return;

    size_t fileLen = f.size();
    uint8_t *data = (uint8_t *)heap_caps_malloc(fileLen, MALLOC_CAP_SPIRAM);
    
    if (!data) {
        f.close();
        return;
    }

    size_t readLen = f.read(data, fileLen);
    f.close();

    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t payloadBuffer[VIDEO_PAYLOAD_SIZE];
    int payloadIdx = 0, activeMaskIdx = -1;
    size_t idx = 0;
    int packetCounter = 0;
    
    while (idx < fileLen) {
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
        }
        packetCounter++;
        if (packetCounter % 10 == 0) { // Minden 10. elküldött csomag után
            vTaskDelay(pdMS_TO_TICKS(1)); // 1ms pihenő, hogy a Watchdog nullázódjon
        }
    }

    if (payloadIdx > 0) {
        for (int i = payloadIdx; i < VIDEO_PAYLOAD_SIZE; i++) payloadBuffer[i] = 0x00;
        sendPacket(out, payloadBuffer, activeMaskIdx);
    }

    heap_caps_free(data);
}