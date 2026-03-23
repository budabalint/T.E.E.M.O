#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <math.h>
#include <config.h>
#include <hardware_pins.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

struct SatelliteDataPacket {
  uint8_t startMarker;
  uint8_t Packet_ID;
  float altitude;
  double latitude;
  double longitude;
  uint8_t checksum;
} __attribute__((packed));

struct TelemetryPacket {
  uint8_t startMarker;
  uint8_t Packet_ID;
  float yaw;
  float pitch;
  uint8_t checksum;
} __attribute__((packed));

Adafruit_NeoPixel strip(LED_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_BNO08x bno08x(-1);
sh2_SensorValue_t sensorValue;

SemaphoreHandle_t dataMutex;

double target_lat = 0.0;
double target_lon = 0.0;
float target_alt = 0.0;

float current_yaw = 0.0;   
float current_pitch = 0.0; 

float target_yaw = 0.0;    
float target_pitch = 0.0;

unsigned long lastUARTDataTime = 0;

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

int getAveragedADC(uint8_t pin, int samples = 16) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(50);
  }
  return sum / samples;
}

void TaskUART(void *pvParameters) {
  int packetSize = sizeof(SatelliteDataPacket);
  
  for (;;) {
    if (Serial1.available() >= packetSize) {
      if (Serial1.read() == 0xAA) {
        SatelliteDataPacket packet;
        packet.startMarker = 0xAA;
        
        uint8_t* ptr = (uint8_t*)&packet;
        Serial1.readBytes(ptr + 1, packetSize - 1);
        
        uint8_t calculatedCRC = calculateCRC8(ptr, packetSize - 1);
        
        if (packet.checksum == calculatedCRC) {
          TelemetryPacket txPacket;
          txPacket.startMarker = 0xBB;
          txPacket.Packet_ID = 0x77;

          xSemaphoreTake(dataMutex, portMAX_DELAY);
          lastUARTDataTime = millis();
          
          if (packet.Packet_ID == 0x55) {
            target_alt = packet.altitude;
            target_lat = packet.latitude;
            target_lon = packet.longitude;
          } else if (packet.Packet_ID == 0x66) {
            tracker_alt = packet.altitude;
            tracker_lat = packet.latitude;
            tracker_lon = packet.longitude;
          }

          txPacket.yaw = current_yaw;
          txPacket.pitch = current_pitch;
          xSemaphoreGive(dataMutex);

          txPacket.checksum = calculateCRC8((uint8_t*)&txPacket, sizeof(TelemetryPacket) - 1);
          Serial1.write((uint8_t*)&txPacket, sizeof(TelemetryPacket));
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TaskSensor(void *pvParameters) {
  for (;;) {
    if (bno08x.wasReset()) {
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

        xSemaphoreTake(dataMutex, portMAX_DELAY);
        current_yaw = raw_yaw + YAW_OFFSET;
        while (current_yaw >= 360.0) current_yaw -= 360.0;
        while (current_yaw < 0.0) current_yaw += 360.0;
        
        current_pitch = raw_pitch + PITCH_OFFSET;
        if (INVERT_PITCH) current_pitch = -current_pitch;
        xSemaphoreGive(dataMutex);
      }
    }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

void TaskControl(void *pvParameters) {
  for (;;) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    double t_lat = target_lat;
    double t_lon = target_lon;
    float t_alt = target_alt;
    float c_yaw = current_yaw;
    float c_pitch = current_pitch;
    unsigned long lastDataTime = lastUARTDataTime;
    xSemaphoreGive(dataMutex);

    uint32_t color = (millis() - lastDataTime < 1500) ? strip.Color(0, 255, 0) : strip.Color(255, 255, 0);
    for(int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, color);
    strip.show();

    if (digitalRead(MODE_SELECTER_BUTTON) == HIGH) {
      int adc_val = getAveragedADC(ANALOG_BUTTON, 16);
      
      bool btn_left = false;
      bool btn_right = false;
      bool btn_up = false;
      bool btn_down = false;

      if (adc_val > 250 && adc_val <= 650) { btn_left = true; }
      else if (adc_val > 650 && adc_val <= 1000) { btn_right = true; }
      else if (adc_val > 1000 && adc_val <= 1260) { btn_up = true; }
      else if (adc_val > 1260 && adc_val <= 1500) { btn_left = true; btn_up = true; }
      else if (adc_val > 1500 && adc_val <= 1740) { btn_right = true; btn_up = true; }
      else if (adc_val > 1740 && adc_val <= 1930) { btn_down = true; }
      else if (adc_val > 1930 && adc_val <= 2070) { btn_left = true; btn_down = true; }
      else if (adc_val > 2070 && adc_val <= 2300) { btn_right = true; btn_down = true; }
      
      // Serial.printf("ADC: %d | L:%d R:%d U:%d D:%d\n", adc_val, btn_left, btn_right, btn_up, btn_down);

      digitalWrite(relay1, btn_left ? HIGH : LOW);
      digitalWrite(relay2, btn_right ? HIGH : LOW);
      digitalWrite(relay3, btn_up ? HIGH : LOW);
      digitalWrite(relay4, btn_down ? HIGH : LOW);
    } 

    else {
      if (t_lat == 0.0 && t_lon == 0.0) {
        digitalWrite(relay1, LOW); digitalWrite(relay2, LOW);
        digitalWrite(relay3, LOW); digitalWrite(relay4, LOW);
      } else {
        double lat1 = tracker_lat * PI / 180.0;
        double lon1 = tracker_lon * PI / 180.0;
        double lat2 = t_lat * PI / 180.0;
        double lon2 = t_lon * PI / 180.0;

        double dLon = lon2 - lon1;
        double dLat = lat2 - lat1;

        double y = sin(dLon) * cos(lat2);
        double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
        
        float calc_yaw = atan2(y, x) * 180.0 / PI;
        if (calc_yaw < 0) calc_yaw += 360.0;

        double a = sin(dLat/2) * sin(dLat/2) + cos(lat1) * cos(lat2) * sin(dLon/2) * sin(dLon/2);
        double c = 2 * atan2(sqrt(a), sqrt(1-a));
        double distance_ground = R_EARTH * c;

        float calc_pitch = atan2(t_alt - tracker_alt, distance_ground) * 180.0 / PI;

        xSemaphoreTake(dataMutex, portMAX_DELAY);
        target_yaw = calc_yaw;
        target_pitch = calc_pitch;
        xSemaphoreGive(dataMutex);

        float yaw_error = calc_yaw - c_yaw;
        while (yaw_error > 180.0) yaw_error -= 360.0;
        while (yaw_error < -180.0) yaw_error += 360.0;

        if (yaw_error > TOLERANCE_YAW) {
          digitalWrite(relay2, LOW); 
          digitalWrite(relay1, HIGH);
        } else if (yaw_error < -TOLERANCE_YAW) {
          digitalWrite(relay1, LOW); 
          digitalWrite(relay2, HIGH);
        } else {
          digitalWrite(relay1, LOW); 
          digitalWrite(relay2, LOW);
        }

        float pitch_error = calc_pitch - c_pitch;
        if (pitch_error > TOLERANCE_PITCH) {
          digitalWrite(relay4, LOW); 
          digitalWrite(relay3, HIGH);
        } else if (pitch_error < -TOLERANCE_PITCH) {
          digitalWrite(relay3, LOW); 
          digitalWrite(relay4, HIGH);
        } else {
          digitalWrite(relay3, LOW); 
          digitalWrite(relay4, LOW);
        }
      }
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}
void I2CScan() {
    byte error, address;
    int nDevices;
    
    Serial.println("Scanning...");
    
    nDevices = 0;
    for(address = 1; address < 127; address++ )
    {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
    
        if (error == 0)
        {
        Serial.print("I2C device found at address 0x");
        if (address<16)
            Serial.print("0");
        Serial.print(address,HEX);
        Serial.println("  !");
    
        nDevices++;
        }
        else if (error==4)
        {
        Serial.print("Unknown error at address 0x");
        if (address<16)
            Serial.print("0");
        Serial.println(address,HEX);
        }    
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found\n");
    else
        Serial.println("done\n");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial1.begin(115200, SERIAL_8N1, data_RX, data_TX);

  pinMode(relay1, OUTPUT); pinMode(relay2, OUTPUT);
  pinMode(relay3, OUTPUT); pinMode(relay4, OUTPUT);
  digitalWrite(relay1, LOW); digitalWrite(relay2, LOW);
  digitalWrite(relay3, LOW); digitalWrite(relay4, LOW);

  pinMode(MODE_SELECTER_BUTTON, INPUT_PULLDOWN);
  pinMode(ANALOG_BUTTON, INPUT);

  strip.begin();
  strip.show();

  Wire.begin(BNO_SDA, BNO_SCL);
  if (!bno08x.begin_I2C(0x4A, &Wire)) {
    Serial.println("Szar a bno (Nem talalhato az I2C cimen)!");
    for(int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, strip.Color(255, 0, 0));
    strip.show();
    
    while (1) { 
      delay(10);
    }
  }

  bno08x.enableReport(SH2_ROTATION_VECTOR);

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(TaskUART, "UART", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskSensor, "Sensor", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskControl, "Control", 4096, NULL, 1, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);
  //I2CScan();
}