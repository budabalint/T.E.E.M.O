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
uint8_t SD_BUFFER[16384];

void TaskRadioSender(void *pvParameters) {
  int sequenceCounter = 0;
  while (1) {
    cam.swapBuffersIfNew();

    for (int chunk = 0; chunk < 3; chunk++) {
      for (int i = 0; i < 8; i++) {
        int currentRow = (chunk * 8) + i;
        ThermalPacket tPacket = cam.getPacketFromBuffer(currentRow, 1);
        canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, (uint8_t*)&tPacket, sizeof(ThermalPacket));
        vTaskDelay(pdMS_TO_TICKS(12)); 
      }

      packet.PreparePacketA_ForSending();
      uint8_t* dataA = (uint8_t*)packet.getPacketA_ReadPtr(); 
      canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, dataA, sizeof(PacketA));
      
      vTaskDelay(pdMS_TO_TICKS(12));

      packet.PreparePacketB_ForSending();
      uint8_t* dataB = (uint8_t*)packet.getPacketB_ReadPtr();
      canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, dataB, sizeof(PacketB));
      
      vTaskDelay(pdMS_TO_TICKS(12));
    }

    sequenceCounter++;
  }
}

void TaskDebug(void *pvParameters) {
  int sequenceCounter = 0;
  while (1) {
    cam.swapBuffersIfNew();

    for (int chunk = 0; chunk < 3; chunk++) {
      
      for (int i = 0; i < 8; i++) {
        int currentRow = (chunk * 8) + i;
        ThermalPacket tPacket = cam.getPacketFromBuffer(currentRow, 1);
        
        Serial.write((uint8_t*)&tPacket, sizeof(ThermalPacket));
        
        vTaskDelay(pdMS_TO_TICKS(12)); 
      }
      packet.PreparePacketA_ForSending();
      uint8_t* dataA = (uint8_t*)packet.getPacketA_ReadPtr(); 
      
      Serial.write(dataA, sizeof(PacketA));
      
      vTaskDelay(pdMS_TO_TICKS(12));
      packet.PreparePacketB_ForSending();
      uint8_t* dataB = (uint8_t*)packet.getPacketB_ReadPtr();
      
      Serial.write(dataB, sizeof(PacketB));
      
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
    int seq = 0;
    while (1)
    {
        packet.WriteBNODataToBuffer(1);
        seq++;
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void ReadI2CSensors(void *pvParameters) {
    int seq = 0;
    while(1) {
        packet.WriteI2CSensorDataToBuffer(1);
        seq++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void setup() {
  //dataMutex = xSemaphoreCreateMutex();
  
  //canSat.begin();
  //delay(100);
  //cam.begin(1000000);
  //delay(100);
  canSat.bus_init(SPI_SPEED, I2C_SPEED, UART_SPEED);

  //xTaskCreatePinnedToCore(ReadThermalCam,   "ThermalReader",  10240, NULL, 1, NULL, 1);
  //xTaskCreatePinnedToCore(SPICommunication, "SPI_BNO",        4096, NULL, 1, NULL, 1);
  //xTaskCreatePinnedToCore(ReadI2CSensors,   "I2C_Sensors",    4096, NULL, 1, NULL, 1);
  //xTaskCreatePinnedToCore(TaskRadioSender,  "RadioSender",    8192, NULL, 2, NULL, 0);
  //xTaskCreatePinnedToCore(TaskDebug,        "DebugSender",    8192, NULL, 2, NULL, 0);
}

void loop() {
  //vTaskDelete(NULL);
  canSat.I2CScan();
}