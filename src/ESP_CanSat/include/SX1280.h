#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <RadioLib.h>
#include <SPI.h>

#define VIDEO_PACKET_SIZE  126
#define VIDEO_PAYLOAD_SIZE 120
#define VIDEO_SYNC_BYTE    0xFE
#define VIDEO_TYPE_MJPEG   0xDD
#define VIDEO_TYPE_FEC     0xFF
#define VIDEO_SEQ_MAX      251
typedef void (*StreamIdleCallback)();
class VideoRadio {
public:
    VideoRadio(uint8_t nss, uint8_t dio1, uint8_t nrst, uint8_t busy, SPIClass* spiObj = nullptr);
    ~VideoRadio();

    bool begin(); // Nem kellenek ide pinek, a már futó SPI-t használja
    void streamMjpegFromFS(const char *path, Stream &out, StreamIdleCallback idleCb = nullptr);
    void transmitRawPadded(const uint8_t* data, size_t len);

private:
    uint8_t _nss, _dio1, _nrst, _busy;
    Module* _module;
    SX1280* _radio;

    int seq;
    int frameId;
    int rrtCounter;
    uint8_t fecGroup[8][VIDEO_PACKET_SIZE];
    uint8_t fecCount;

    uint8_t calcCrc8(const uint8_t *data, size_t len);
    void sendPacket(Stream &out, const uint8_t *payload, int activeMaskIndex);
    void sendFecPacket(Stream &out);
};