#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>

#define SD_MISO  13
#define SD_SCK   12
#define SD_MOSI  11
#define SD_CS    45
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

    if (!file.open("faszabence.bin", O_RDWR | O_CREAT | O_TRUNC)) {
        Serial.println("file open failled");
        return;
    }
    memset(buf, 0xB2, 4096);
    file.write(buf, 4096);
    file.sync();
}

void loop() {
    delay(1000);
    Serial.println("1");
}