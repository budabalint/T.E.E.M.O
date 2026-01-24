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

uint8_t SD_BUFFER[16384];

void TaskRadioSender(void *pvParameters) {
  int sequenceCounter = 0;
  while (1) {
    cam.swapBuffersIfNew();

    for (int chunk = 0; chunk < 3; chunk++) {
      for (int i = 0; i < 8; i++) {
        int currentRow = (chunk * 8) + i;
        ThermalPacket tPacket = cam.getPacketFromBuffer(currentRow, sequenceCounter);
        canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, (uint8_t*)&tPacket, sizeof(ThermalPacket));
      }

      PacketA pA;
      memset(&pA, 0xBB, sizeof(PacketA));
      canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, (uint8_t*)&pA, sizeof(PacketA));

      PacketB pB;
      memset(&pB, 0xCC, sizeof(PacketB));
      canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, (uint8_t*)&pB, sizeof(PacketB));
    }

    sequenceCounter++;
  }
}

void ReadI2CSensors() {
  while(1) {

  }
}

void ReadThermalCam(void *pvParameters) {
  while (1)
  {
    cam.captureFrameToBuffer();
  }
}

void SPICommunication() {
  while (1)
  {

  }
}

void Imageprocess() {
  while(1) {

  }
}

void setup() {
  canSat.begin();
  delay(50);
  cam.begin(1000000);
}


int seq = 0;
void loop() {
  Serial.println("1");
    for (size_t row = 0; row < 24; row++) {
      //ThermalPacket pck = cam.GetThermalData(row, seq++);
      ThermalPacket pck;
      memset(&pck, 0xAA, 44);
      uint8_t* data = reinterpret_cast<uint8_t*>(&pck);
      //size_t len = sizeof(ThermalPacket);
      //canSat._file.write(data, len);
      canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, data, sizeof(data));
  }


  PacketA packet1;
  //packet.CreatePacket_A(seq++, false);
  memset(&packet1, 0xBB, 44);
  uint8_t* pck1 = reinterpret_cast<uint8_t*>(&packet1);
  size_t lenA = sizeof(PacketA);
  //canSat._file.write(pck1, lenA);
  canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, pck1, sizeof(pck1));


  PacketB packet2;
  //packet.CreatePacket_B(seq++, false);
  memset(&packet2, 0xCC, 44);
  uint8_t* pck2 = reinterpret_cast<uint8_t*>(&packet2);
  size_t lenB = sizeof(PacketB);
  //canSat._file.write(pck2, lenB);
  //canSat._file.sync();
  canSat.sendRadioMsg(DEST_ADDH, DEST_ADDL, CHANNEL, pck2, sizeof(pck2));



};
