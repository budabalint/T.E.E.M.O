#include <Arduino.h>
#include <CanSat.h>
#include <Create_Packet.h>
#include <ThermalCam.h>
#include <SdFat.h>
#include <config.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

CanSat canSat;
Packet packet;
ThermalCam cam;

SemaphoreHandle_t dataMutex;
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
uint8_t SD_BUFFER[16384];

void appendData(uint8_t* data, int len) {
    if (writeIndex + len <= 32768) {
        memcpy(&SD_BUFFER[writeIndex], data, len);
        writeIndex += len;
    } else {
        int spaceLeft = 32768 - writeIndex;
        memcpy(&SD_BUFFER[writeIndex], data, spaceLeft);
        memcpy(&SD_BUFFER[0], data + spaceLeft, len - spaceLeft);
        writeIndex = len - spaceLeft;
    }

    if (writeIndex == 32768) {
        writeIndex = 0;
    }
}

void TaskRadioSender(void *pvParameters) {
  int sequenceCounter = 0;
  while (1) {
    /*cam.swapBuffersIfNew();
    for (int chunk = 0; chunk < 3; chunk++) {
      for (int i = 0; i < 8; i++) {
        int currentRow = (chunk * 8) + i;
        ThermalPacket tPacket = cam.getPacketFromBuffer(currentRow, 1);
        canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, (uint8_t*)&tPacket, sizeof(ThermalPacket));
        appendData((uint8_t*)&tPacket, sizeof(ThermalPacket));
        vTaskDelay(pdMS_TO_TICKS(12)); 
      }*/
    

      packet.PreparePacketA_ForSending(seq);
      uint8_t* dataA = (uint8_t*)packet.getPacketA_ReadPtr();
      canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, dataA, sizeof(PacketA));
      seq++;
      
      //appendData(dataA, 44);
      vTaskDelay(pdMS_TO_TICKS(12));

      packet.PreparePacketB_ForSending(seq);
      uint8_t* dataB = (uint8_t*)packet.getPacketB_ReadPtr();
      canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, dataB, sizeof(PacketB));
      seq++;
      
      //appendData(dataB, 44);
      vTaskDelay(pdMS_TO_TICKS(12));
    }
    sequenceCounter++;
  }

void TaskDebug(void *pvParameters) {
  int sequenceCounter = 0;
  while (1) {
    cam.swapBuffersIfNew();
    for (int chunk = 0; chunk < 3; chunk++) {
      
      for (int i = 0; i < 8; i++) {
        int currentRow = (chunk * 8) + i;
        ThermalPacket tPacket = cam.getPacketFromBuffer(currentRow, 1);
        
        
        //Serial.write((uint8_t*)&tPacket, sizeof(ThermalPacket));
        //printHex(&tPacket, sizeof(ThermalPacket));
        
        vTaskDelay(pdMS_TO_TICKS(12)); 
      }
    
      packet.PreparePacketA_ForSending(seq);
      uint8_t* dataA = (uint8_t*)packet.getPacketA_ReadPtr(); 
      seq++;
      //Serial.write(dataA, sizeof(PacketA));
      //printHex(dataA, 44);
      vTaskDelay(pdMS_TO_TICKS(12));
      packet.PreparePacketB_ForSending(seq);
      uint8_t* dataB = (uint8_t*)packet.getPacketB_ReadPtr();
      seq++;
      //Serial.write(dataB, sizeof(PacketB));
      //printHex(dataB, 44);
      
      vTaskDelay(pdMS_TO_TICKS(12));
    }

    sequenceCounter++;
  }
}

void ReadThermalCam(void *pvParameters) {
  while (1)
  {
    cam.captureFrameToBuffer();

    vTaskDelay(pdMS_TO_TICKS(20)); 
  }
}

void SPICommunication(void *pvParameters) {
    while (1) {
    /*{
        if (readPhase == 0 && writeIndex >= 16384) {
            canSat._file.write(&SD_BUFFER[0], 16384);
            canSat._file.sync();
            readPhase = 1;
        }
        else if (readPhase == 1 && writeIndex < 16384) {
            canSat._file.write(&SD_BUFFER[16384], 16384);
            canSat._file.sync();
            readPhase = 0;
        }*/
        packet.WriteBNODataToBuffer();
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void ReadI2CSensors(void *pvParameters) {
    int seq = 0;
    while(1) {
        packet.WriteI2CSensorDataToBuffer();
        //canSat.I2CScan();
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void setup() {
  dataMutex = xSemaphoreCreateMutex();
  
  canSat.begin();
  delay(100);
  cam.begin(1000000);
  delay(100);

  //xTaskCreatePinnedToCore(ReadThermalCam,   "ThermalReader",  10240, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(SPICommunication, "SPI_BNO",        4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(ReadI2CSensors,   "I2C_Sensors",    4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskRadioSender,  "RadioSender",    8192, NULL, 2, NULL, 0);
  //xTaskCreatePinnedToCore(TaskDebug,        "DebugSender",    8192, NULL, 2, NULL, 0);
}

void loop() {
  vTaskDelete(NULL);
  //canSat.I2CScan();
}

