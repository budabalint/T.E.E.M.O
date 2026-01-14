#include <Arduino.h>
#include <CanSat.h>
#include <Create_Packet.h>
#include <ThermalCam.h>
#include <SdFat.h>

const uint32_t RECORD_COUNT = 100000;
const uint32_t FILE_SIZE = RECORD_COUNT * 44;

CanSat canSat;
Packet packet;
ThermalCam cam;
SdFat sd;
SdFile file;

bool SD_init() {
  if(!sd.begin(SD_CARD_CS, SD_SCK_MHZ(8))){
    Serial.println("SD init failed");
    return false;
  }
  return true;
}

bool openFile() {
  if (!file.open("log.bin", O_CREAT | O_WRITE | O_TRUNC)) {
    Serial.println("File open failed");
    return false;
  }

  if (!file.preAllocate(FILE_SIZE)) {
    Serial.println("Preallocate failed");
    return false;
  }
  return true;
}

void writerecord(const PacketA& packet) {
  file.write((uint8_t*)&packet, 44);
  file.sync(); 
}

void setup() {
  pinMode(SD_CARD_CS, OUTPUT);
  digitalWrite(SD_CARD_CS, HIGH);
  pinMode(BNO_CS, OUTPUT);
  digitalWrite(BNO_CS, HIGH);

  delay(50);

  if (!SD_init()) {
    Serial.println("SD error, halt");
  }
  delay(50);

  if (!openFile()) {
    Serial.println("File error, halt");
  }
  delay(50);
  canSat.begin();
  cam.begin(1000000);

}


int seq = 0;
void loop() {
  
    for (size_t row = 0; row < 24; row++) {
      ThermalPacket pck = cam.GetThermalData(row, seq++);

      uint8_t* data = reinterpret_cast<uint8_t*>(&pck);
      size_t len = sizeof(ThermalPacket);

      //Serial.write(data, len);
  }


  PacketA packet1 = packet.CreatePacket_A(seq++, false);
  uint8_t* pck1 = reinterpret_cast<uint8_t*>(&packet1);
  size_t lenA = sizeof(PacketA);
  //Serial.write(pck1, lenA);
  digitalWrite(BNO_CS, HIGH);
  writerecord(packet1);

  PacketB packet2 = packet.CreatePacket_B(seq++, false);
  uint8_t* pck2 = reinterpret_cast<uint8_t*>(&packet2);
  size_t lenB = sizeof(PacketB);
  //Serial.write(pck2, lenB);

  if (seq >= 234) {
      seq = 0;
  }
};
