#include <Arduino.h>
#include <CanSat.h>
#include <Create_Packet.h>
#include <ThermalCam.h>
#include <SdFat.h>
#include <config.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/stream_buffer.h> // <-- EZ ÚJ

CanSat canSat;
Packet packet;
ThermalCam cam;

SemaphoreHandle_t dataMutex;
SemaphoreHandle_t spiMutex;
StreamBufferHandle_t sdStreamBuffer;

volatile int writeIndex = 0;
int readPhase = 0; 

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress camIP(192, 168, 0, 144);
const int camPort = 554;
const char* streamURL = "rtsp://192.168.0.144/stream=1";
const char* authHeader = "Authorization: Basic cm9vdDphYWFh"; 
uint8_t seq = 0;

uint8_t* packetBuffer = NULL;

void printHex(void* ptr, size_t size) {
    uint8_t* data = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

void TaskRadioSender(void *pvParameters) {
    while (1) {
        packet.PreparePacketA_ForSending(seq);
        uint8_t* dataA = (uint8_t*)packet.getPacketA_ReadPtr();
        canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, dataA, sizeof(PacketA));
        
        xStreamBufferSend(sdStreamBuffer, dataA, sizeof(PacketA), 0);
        seq++;

        vTaskDelay(pdMS_TO_TICKS(1));
        
        packet.PreparePacketB_ForSending(seq);
        uint8_t* dataB = (uint8_t*)packet.getPacketB_ReadPtr();
        canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, dataB, sizeof(PacketB));
        
        xStreamBufferSend(sdStreamBuffer, dataB, sizeof(PacketB), 0);
        seq++;
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void TaskSDWriter(void *pvParameters) {
    const size_t CHUNK_SIZE = 4096; // 16 KB (16 * 1024 byte)
    uint8_t* writeCache = (uint8_t*)heap_caps_malloc(CHUNK_SIZE, MALLOC_CAP_8BIT);

    if (writeCache == NULL) {
        Serial.println("KRITIKUS HIBA: Nem sikerült RAM-ot foglalni az SD cache-nek (16KB)!");
        vTaskDelete(NULL);
    }
    
    size_t cachedBytes = 0;
    Serial.println(">>> SD Writer Task sikeresen elindult! (16KB Cache) <<<");
    int counter = 0;
    while (1) {
        size_t bytesRead = xStreamBufferReceive(
            sdStreamBuffer, 
            &writeCache[cachedBytes], 
            CHUNK_SIZE - cachedBytes, 
            pdMS_TO_TICKS(100)
        );
        cachedBytes += bytesRead;

        if (cachedBytes >= CHUNK_SIZE) {
            //Serial.print("SD Írás indul (4KB)... ");
            unsigned long t_start = millis();

            if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
                counter++;
                // Adat kiírása és kártyára szinkronizálása
                size_t written = canSat._file.write(writeCache, cachedBytes);
                if (counter>= 10) {
                    canSat._file.sync();
                    counter = 0;
                }
                
                xSemaphoreGive(spiMutex);
                vTaskDelay(pdMS_TO_TICKS(1));
                
                if (written != cachedBytes) {
                    Serial.printf("HIBA! Csak %d byte íródott ki a %d-ből!\n", written, cachedBytes);
                } else {
                    //Serial.printf("Kész! %lu ms alatt.\n", millis() - t_start);
                }
            }
            cachedBytes = 0;
        }
    }
}

void ReadThermalCam(void *pvParameters) {
    uint8_t frameSeq = 0;
    while (1) {
        if (cam.captureFrameToBuffer()) {
            cam.swapBuffersIfNew(); 
            for (uint8_t row = 0; row < 24; row++) {
                ThermalPacket tp = cam.getPacketFromBuffer(row, frameSeq);
                xStreamBufferSend(sdStreamBuffer, &tp, sizeof(ThermalPacket), 0);
            }
            
            frameSeq++;
        }

        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

void SPICommunication(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void ReadI2CSensors(void *pvParameters) {
    while(1) {
        packet.WriteI2CSensorDataToBuffer();
        packet.WriteBNODataToBuffer();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    dataMutex = xSemaphoreCreateMutex();
    spiMutex = xSemaphoreCreateMutex();
    
    sdStreamBuffer = xStreamBufferCreate(32768, 1); 

    //digitalWrite(CAM_CS, LOW);
    //digitalWrite(BNO_RST, HIGH);
    canSat.begin();
    delay(100);
    cam.begin(1000000);
    delay(100);

    //xTaskCreatePinnedToCore(SPICommunication, "SPI_BNO",        4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(ReadI2CSensors,   "I2C_Sensors",    4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(TaskRadioSender,  "RadioSender",    4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskSDWriter,     "SD_Writer",      4096, NULL, 1, NULL, 0); 
    xTaskCreatePinnedToCore(ReadThermalCam, "ThermalReader", 10240, NULL, 1, NULL, 1);
}

void loop() {
    vTaskDelete(NULL);
}