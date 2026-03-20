#include <SPI.h>
#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <math.h>

const uint8_t data_RX = 17;
const uint8_t data_TX = 18;

const uint8_t BNO_MOSI = 37;
const uint8_t BNO_MISO = 38;
const uint8_t BNO_CLK  = 39;
const uint8_t BNO_Cs   = 40;

const int8_t BNO_INT = 47;
const int8_t BNO_RST = 48;

const uint8_t Rotater_motor_forward = 10;
const uint8_t Rotater_motor_backwards = 11;
const uint8_t Lifter_motor_forward = 12;
const uint8_t Lifter_motor_backwards = 13;

const double tracker_lat = 47.501142;
const double tracker_lon = 18.016608;
const float tracker_alt = 180;

int32_t CanSat_lattitude_raw = 0;
int32_t CanSat_longitude_raw = 0;
int32_t CanSat_altitude_raw = 0;

const float YAW_OFFSET = -90.0;     // Irányszög korrekció fokban (pl. 90, 180, -90)
const float PITCH_OFFSET = 0.0;    // Dőlésszög finomhangolása (ha nem teljesen vízszintes a panel)
const bool  INVERT_PITCH = false;  // Állítsd true-ra, ha az antenna felfelé dől, de a szenzor értéke csökken!

double target_lat = 0.0;
double target_lon = 0.0;
float target_alt = 0.0;

float current_yaw = 0.0;   
float current_pitch = 0.0; 

float target_yaw = 0.0;    
float target_pitch = 0.0;  

const float TOLERANCE_YAW = 5.0;   
const float TOLERANCE_PITCH = 3.0; 

Adafruit_BNO08x bno08x(BNO_RST);
sh2_SensorValue_t sensorValue;

const double R_EARTH = 6371000.0; 

uint8_t calculateCRC8(uint8_t *data, uint8_t len);
void parseUART();
void readBNO085();
void calculateTargetOrientation();
void controlMotors();

void setFixedTestCoordinates() {
  target_lat = 47.501117;
  target_lon = 18.003319;
  target_alt = 1180;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial1.begin(115200, SERIAL_8N1, data_RX, data_TX);

  pinMode(Rotater_motor_forward, OUTPUT);
  pinMode(Rotater_motor_backwards, OUTPUT);
  pinMode(Lifter_motor_forward, OUTPUT);
  pinMode(Lifter_motor_backwards, OUTPUT);

  digitalWrite(Rotater_motor_forward, LOW);
  digitalWrite(Rotater_motor_backwards, LOW);
  digitalWrite(Lifter_motor_forward, LOW);
  digitalWrite(Lifter_motor_backwards, LOW);

  SPI.begin(BNO_CLK, BNO_MISO, BNO_MOSI, BNO_Cs);
  
  Serial.println("BNO085 inicializalasa...");

  if (!bno08x.begin_SPI(BNO_Cs, BNO_INT)) {
    Serial.println("Hiba: BNO085 nem talalhato!");
    while (1) { delay(10); }
  }
  Serial.println("BNO085 OK!");

  if (!bno08x.enableReport(SH2_ROTATION_VECTOR)) {
    Serial.println("Nem sikerult bekapcsolni a Rotation Vectort");
  }
  delay(100);
}

void loop() {
  //parseUART();
  setFixedTestCoordinates();
  
  readBNO085();
  calculateTargetOrientation();
  controlMotors();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    
    Serial.print("Aktualis (BNO) -> Irany: "); 
    Serial.print(current_yaw);
    Serial.print(" fok | Doles: "); 
    Serial.print(current_pitch);
    
    Serial.print(" fok  ||  Célzott -> Irany: "); 
    Serial.print(target_yaw);
    Serial.print(" fok | Doles: "); 
    Serial.println(target_pitch);
  }
  
  delay(10);
}

void parseUART() {
  if (Serial1.available() >= 14) {
    if (Serial1.read() == 0xFE) {
      uint8_t buffer[13];
      Serial1.readBytes(buffer, 13);
      
      uint8_t receivedCRC = buffer[12];
      uint8_t calculatedCRC = calculateCRC8(buffer, 12);
      
      if (receivedCRC == calculatedCRC) {
        CanSat_lattitude_raw = (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];
        CanSat_longitude_raw = (buffer[7] << 24) | (buffer[6] << 16) | (buffer[5] << 8) | buffer[4];
        CanSat_altitude_raw  = (buffer[11] << 24) | (buffer[10] << 16) | (buffer[9] << 8) | buffer[8];
        
        target_lat = CanSat_lattitude_raw / 10000000.0;
        target_lon = CanSat_longitude_raw / 10000000.0;
        target_alt = CanSat_altitude_raw / 100.0f;
      } else {
        Serial.println("CRC Hiba a csomagban!");
      }
    }
  }
}

uint8_t calculateCRC8(uint8_t *data, uint8_t len) {
  uint8_t crc = 0x00;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0x07;
      else crc <<= 1;
    }
  }
  return crc;
}

void readBNO085() {
  if (bno08x.wasReset()) {
    Serial.print("BNO085 resetelve, riport ujrainditasa...");
    bno08x.enableReport(SH2_ROTATION_VECTOR);
  }

  if (bno08x.getSensorEvent(&sensorValue)) {
    if (sensorValue.sensorId == SH2_ROTATION_VECTOR) {
      float qr = sensorValue.un.rotationVector.real;
      float qi = sensorValue.un.rotationVector.i;
      float qj = sensorValue.un.rotationVector.j;
      float qk = sensorValue.un.rotationVector.k;

      float sqr = qr * qr;
      float sqi = qi * qi;
      float sqj = qj * qj;
      float sqk = qk * qk;

      float raw_yaw = atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr)) * 180.0 / PI;
      float raw_pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr)) * 180.0 / PI;

      current_yaw = raw_yaw + YAW_OFFSET;
      while (current_yaw >= 360.0) current_yaw -= 360.0;
      while (current_yaw < 0.0) current_yaw += 360.0;
      current_pitch = raw_pitch + PITCH_OFFSET;
      
      if (INVERT_PITCH) {
        current_pitch = -current_pitch;
      }
    }
  }
}
void calculateTargetOrientation() {
  if (target_lat == 0.0 && target_lon == 0.0) return;

  double lat1 = tracker_lat * PI / 180.0;
  double lon1 = tracker_lon * PI / 180.0;
  double lat2 = target_lat * PI / 180.0;
  double lon2 = target_lon * PI / 180.0;

  double dLon = lon2 - lon1;
  double dLat = lat2 - lat1;

  double y = sin(dLon) * cos(lat2);
  double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
  target_yaw = atan2(y, x) * 180.0 / PI;
  if (target_yaw < 0) target_yaw += 360.0;

  double a = sin(dLat/2) * sin(dLat/2) + cos(lat1) * cos(lat2) * sin(dLon/2) * sin(dLon/2);
  double c = 2 * atan2(sqrt(a), sqrt(1-a));
  double distance_ground = R_EARTH * c;

  float altitude_diff = target_alt - tracker_alt;
  target_pitch = atan2(altitude_diff, distance_ground) * 180.0 / PI;
}

void controlMotors() {
  if (target_lat == 0.0 && target_lon == 0.0) return;

  float yaw_error = target_yaw - current_yaw;
  while (yaw_error > 180.0) yaw_error -= 360.0;
  while (yaw_error < -180.0) yaw_error += 360.0;

  if (yaw_error > TOLERANCE_YAW) {
    digitalWrite(Rotater_motor_backwards, LOW); 
    digitalWrite(Rotater_motor_forward, HIGH);
  } 
  else if (yaw_error < -TOLERANCE_YAW) {
    digitalWrite(Rotater_motor_forward, LOW); 
    digitalWrite(Rotater_motor_backwards, HIGH);
  } 
  else {
    digitalWrite(Rotater_motor_forward, LOW);
    digitalWrite(Rotater_motor_backwards, LOW);
  }


  float pitch_error = target_pitch - current_pitch;

  if (pitch_error > TOLERANCE_PITCH) {
    digitalWrite(Lifter_motor_backwards, LOW);
    digitalWrite(Lifter_motor_forward, HIGH);
  } 
  else if (pitch_error < -TOLERANCE_PITCH) {
    digitalWrite(Lifter_motor_forward, LOW);
    digitalWrite(Lifter_motor_backwards, HIGH);
  } 
  else {
    digitalWrite(Lifter_motor_forward, LOW);
    digitalWrite(Lifter_motor_backwards, LOW);
  }
}
