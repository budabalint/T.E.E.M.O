#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>

#define SD_MISO  13
#define SD_SCK   12
#define SD_MOSI  11
#define SD_CS    10
#define SPI_FREQ SD_SCK_MHZ(8)

const size_t BUF_SIZE = 4096;
const size_t PACKET_SIZE = 44;

struct __attribute__((packed)) StructA {
    uint32_t header;
    uint32_t id;
    uint8_t data[36];
};

struct __attribute__((packed)) StructB {
    uint32_t header;
    float values[10];
};

uint8_t buf[BUF_SIZE];
size_t bufOffset = 0;
uint32_t packetCounter = 0;

SdFs sd;
FsFile file;

void setup() {
    Serial.begin(115200);
    
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    if (!sd.begin(SdSpiConfig(SD_CS, SHARED_SPI, SPI_FREQ, &SPI))) {
        Serial.println("sd init failled");
        return;
    }

    if (!file.open("data.bin", O_RDWR | O_CREAT | O_TRUNC)) {
        Serial.println("file open failled");
        return;
    }
}

void loop() {
    while (bufOffset + PACKET_SIZE <= BUF_SIZE) {
        if (packetCounter % 2 == 0) {
            StructA pktA;
            pktA.header = 0xAA55AA55;
            pktA.id = packetCounter;
            memset(pktA.data, 0x01, sizeof(pktA.data));
            memcpy(&buf[bufOffset], &pktA, PACKET_SIZE);
        } else {
            StructB pktB;
            pktB.header = 0xBB66BB66;
            for (int i = 0; i < 10; i++) {
                pktB.values[i] = (float)packetCounter * 0.1f;
            }
            memcpy(&buf[bufOffset], &pktB, PACKET_SIZE);
        }
        
        bufOffset += PACKET_SIZE;
        packetCounter++;
    }

    file.write(buf, bufOffset);
    
    bufOffset = 0;
}