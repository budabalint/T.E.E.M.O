#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <math.h>
#include <config.h>
#include <hardware_pins.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// STRUKTÚRÁK (A Web felőli azonosításhoz)
// RX StartMarker: 0xAA
// TX StartMarker: 0xBB
// ID 0x55 = Célpont GPS (20 byte adat)
// ID 0x66 = Bázis GPS (20 byte adat)
// ID 0x33 = Módváltás (1 byte adat)
// ID 0x44 = Manuális Mozgás (9 byte adat)
// ID 0x88 = Telemetria Lekérdezés (0 byte adat)
// ID 0x77 = Telemetria Válasz (8 byte adat - küldött)

struct TelemetryPacket {
  uint8_t startMarker;
  uint8_t Packet_ID;
  float yaw;
  float pitch;
  uint8_t mag_accuracy;
  uint8_t checksum;
} __attribute__((packed));

Adafruit_NeoPixel strip(LED_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_BNO08x bno08x(-1);
sh2_SensorValue_t sensorValue;

SemaphoreHandle_t dataMutex;

double target_lat = 47.501117;
double target_lon = 18.003319;
float target_alt = 1180;

float current_yaw = 0.0;
float current_pitch = 0.0;

float target_yaw = 0.0;
float target_pitch = 0.0;

unsigned long lastUARTDataTime = 0;
float game_yaw = 0.0;
float mag_yaw = 0.0;
float dynamic_yaw_offset = 0.0;

bool motors_were_running = false;
unsigned long last_motor_stop_time = 0;
bool target_is_absolute = false; // Megjegyzi, hogy fix weblapos célt követünk-e

uint8_t mag_accuracy = 0;

bool global_isManual = true;
bool tracking_active = false; // Jelzi, ha már kapott parancsot webről vagy joystickról
bool last_switch_state = false;

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
  for (;;) {
    while (Serial1.available() > 0 && Serial1.peek() != 0xAA) {
      Serial1.read();
    }

    if (Serial1.available() >= 2) { 
      uint8_t start = Serial1.read();
      uint8_t packet_id = Serial1.read();
      int payload_size = -1;

      if (packet_id == 0x55 || packet_id == 0x66) payload_size = 20; 
      else if (packet_id == 0x33) payload_size = 1;  
      else if (packet_id == 0x44) payload_size = 9;  
      else if (packet_id == 0x88) payload_size = 0;  

      if (payload_size != -1) {
        unsigned long startTime = millis();
        while (Serial1.available() < payload_size + 1) {
          if (millis() - startTime > 100) break; 
          vTaskDelay(1 / portTICK_PERIOD_MS);
        }

        if (Serial1.available() >= payload_size + 1) {
          uint8_t buffer[32];
          buffer[0] = start;       
          buffer[1] = packet_id;   
          Serial1.readBytes(&buffer[2], payload_size + 1); 

          uint8_t calculatedCRC = calculateCRC8(buffer, payload_size + 2);
          uint8_t receivedCRC = buffer[payload_size + 2];

          if (calculatedCRC == receivedCRC) {
            xSemaphoreTake(dataMutex, portMAX_DELAY);
            lastUARTDataTime = millis();
            
            if (packet_id == 0x55 || packet_id == 0x66) {
              float p_alt; double p_lat, p_lon;
              memcpy(&p_alt, &buffer[2], 4);
              memcpy(&p_lat, &buffer[6], 8);
              memcpy(&p_lon, &buffer[14], 8);
              if (p_lat != 0.0 && p_lon != 0.0) {
                if (packet_id == 0x55) { target_alt = p_alt; target_lat = p_lat; target_lon = p_lon; } 
                else { tracker_alt = p_alt; tracker_lat = p_lat; tracker_lon = p_lon; }
              }
            } 
            else if (packet_id == 0x33) {
              global_isManual = (buffer[2] != 0);
            } 
            else if (packet_id == 0x44) {
              uint8_t move_type = buffer[2];
              float y_val, p_val;
              memcpy(&y_val, &buffer[3], 4);
              memcpy(&p_val, &buffer[7], 4);

              if (move_type == 0) { target_yaw = y_val; target_pitch = p_val; } 
              else { target_yaw = current_yaw + y_val; target_pitch = current_pitch + p_val; }
              
              while(target_yaw >= 360.0) target_yaw -= 360.0;
              while(target_yaw < 0.0) target_yaw += 360.0;
              
              global_isManual = true;
              tracking_active = true; 
            }

            TelemetryPacket txPacket;
            txPacket.startMarker = 0xBB;
            txPacket.Packet_ID = 0x77;
            txPacket.yaw = current_yaw;
            txPacket.pitch = current_pitch;
            txPacket.mag_accuracy = mag_accuracy;
            xSemaphoreGive(dataMutex);

            txPacket.checksum = calculateCRC8((uint8_t*)&txPacket, sizeof(TelemetryPacket) - 1);
            
            Serial1.write((uint8_t*)&txPacket, sizeof(TelemetryPacket));
            Serial1.flush(); 

          } else {
            Serial.printf("UART Hiba: CRC nem egyezik!\n");
          }
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
      bno08x.enableReport(SH2_GAME_ROTATION_VECTOR);
    }

    if (bno08x.getSensorEvent(&sensorValue)) {
      float qr = sensorValue.un.rotationVector.real;
      float qi = sensorValue.un.rotationVector.i;
      float qj = sensorValue.un.rotationVector.j;
      float qk = sensorValue.un.rotationVector.k;

      float sqr = qr * qr;
      float sqi = qi * qi;
      float sqj = qj * qj;
      float sqk = qk * qk;

      float raw_yaw = -atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr)) * 180.0 / PI;

      // JAVÍTVA: a 180 fokos eltolás kikerült (feleslegesnek bizonyult),
      // a szög tükrözése nélkül most már helyesen adja vissza a valós irányt.
      while (raw_yaw >= 360.0) raw_yaw -= 360.0;
      while (raw_yaw < 0.0) raw_yaw += 360.0;

      float raw_pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr)) * 180.0 / PI;

      xSemaphoreTake(dataMutex, portMAX_DELAY);
      if (sensorValue.sensorId == SH2_ROTATION_VECTOR) {
        mag_yaw = raw_yaw;
        mag_accuracy = sensorValue.status;
      } 
      else if (sensorValue.sensorId == SH2_GAME_ROTATION_VECTOR) {
        game_yaw = raw_yaw;
        current_pitch = raw_pitch + PITCH_OFFSET;
        if (INVERT_PITCH) current_pitch = -current_pitch;
      }
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}
void TaskControl(void *pvParameters) {
  static unsigned long lastDebugPrint = 0;
  
  static bool yaw_correcting = false;
  static bool pitch_correcting = false;
  static bool offset_updated = false;
  static int yaw_dir = 0; 

  const float INNER_TOLERANCE = 0.5; 

  for (;;) {
    bool current_switch = (digitalRead(MODE_SELECTER_BUTTON) == HIGH);
    if (current_switch != last_switch_state) {
      global_isManual = !global_isManual; 
      last_switch_state = current_switch;
    }
    bool isManual = global_isManual;

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    current_yaw = game_yaw + dynamic_yaw_offset + YAW_OFFSET;
    while (current_yaw >= 360.0) current_yaw -= 360.0;
    while (current_yaw < 0.0) current_yaw += 360.0;

    if (!tracking_active) {
      target_yaw = current_yaw;
      target_pitch = current_pitch;
    }
    double t_lat = target_lat;
    double t_lon = target_lon;
    float t_alt = target_alt;
    float c_yaw = current_yaw; 
    float c_pitch = current_pitch;
    unsigned long lastDataTime = lastUARTDataTime;
    xSemaphoreGive(dataMutex);

    bool hasSignal = (millis() - lastDataTime < 1500);

    uint32_t color = strip.Color(0, 0, 0);
    if (isManual) {
      if (hasSignal) color = strip.Color(0, 0, 255);
      else color = strip.Color(255, 0, 0);
    } else {
      if (hasSignal) color = strip.Color(0, 255, 0);
      else color = strip.Color(255, 255, 0);
    }
    for(int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, color);
    strip.show();

    bool r1 = LOW, r2 = LOW, r3 = LOW, r4 = LOW;

    if (isManual) {
      int adc_val = getAveragedADC(ANALOG_BUTTON, 16);
      bool btn_left = false, btn_right = false, btn_up = false, btn_down = false;

      // --- Új, kalibrált ADC gombkiosztás ---
      
      // Két jobboldali (Jobb felső + Jobb alsó) ~ 548 -> Jobbra forog + Emelkedik
      if (adc_val > 450 && adc_val <= 562) { 
        btn_right = true; btn_up = true; 
      } 
      // Bal felső és Jobb alsó ~ 576 -> Balra forog + Emelkedik
      else if (adc_val > 562 && adc_val <= 619) { 
        btn_left = true; btn_up = true; 
      } 
      // Jobb alsó ~ 662 -> Emelkedik
      else if (adc_val > 619 && adc_val <= 1096) { 
        btn_up = true; 
      } 
      // Jobb felső és Bal alsó ~ 1530 -> Jobbra forog + Süllyed
      else if (adc_val > 1096 && adc_val <= 1643) { 
        btn_right = true; btn_down = true; 
      } 
      // Jobb felső ~ 1757 -> Jobbra forog
      else if (adc_val > 1643 && adc_val <= 1778) { 
        btn_right = true; 
      } 
      // Két baloldali (Bal felső + Bal alsó) ~ 1800 -> Balra forog + Süllyed
      else if (adc_val > 1778 && adc_val <= 1955) { 
        btn_left = true; btn_down = true; 
      } 
      // Bal felső ~ 2110 -> Balra forog
      else if (adc_val > 1955 && adc_val <= 2550) { 
        btn_left = true; 
      } 
      // Bal alsó ~ 2990 -> Süllyed
      else if (adc_val > 2550 && adc_val <= 3500) { 
        btn_down = true; 
      }

      if (btn_left || btn_right || btn_up || btn_down) {
        r1 = btn_right; r2 = btn_left; r3 = btn_up; r4 = btn_down;
        // JAVÍTVA: manuális gombnyomás után NE maradjon aktív a korrekció,
        // különben a motor "hintázni" kezd a gomb elengedése után.
        tracking_active = false;

        xSemaphoreTake(dataMutex, portMAX_DELAY);
        target_yaw = c_yaw;
        target_pitch = c_pitch;
        xSemaphoreGive(dataMutex);

        yaw_correcting = false;
        pitch_correcting = false;
        yaw_dir = 0;
      } else {
        float yaw_error = target_yaw - c_yaw;
        while (yaw_error > 180.0) yaw_error -= 360.0;
        while (yaw_error < -180.0) yaw_error += 360.0;

        if (abs(yaw_error) > TOLERANCE_YAW) yaw_correcting = true;
        else if (abs(yaw_error) < INNER_TOLERANCE) yaw_correcting = false;

        if (yaw_correcting) {
          if (abs(yaw_error) < 175.0 || yaw_dir == 0) {
            yaw_dir = (yaw_error > 0) ? 1 : 2; 
          }
          // JAVÍTVA: yaw relék felcserélve, mert a motor fizikai forgásiránya
          // ellentétes volt a kód eredeti feltételezéséhez képest.
          if (yaw_dir == 1) { r2 = HIGH; } 
          else { r1 = HIGH; }
        } else {
          yaw_dir = 0; 
        }

        float pitch_error = target_pitch - c_pitch;
        if (abs(pitch_error) > TOLERANCE_PITCH) pitch_correcting = true;
        else if (abs(pitch_error) < INNER_TOLERANCE) pitch_correcting = false;

        if (pitch_correcting) {
          if (pitch_error > 0) { r3 = HIGH; } 
          else { r4 = HIGH; }
        }
      }

    } else {
      if (t_lat != 0.0 && t_lon != 0.0) {
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

        if (abs(yaw_error) > TOLERANCE_YAW) yaw_correcting = true;
        else if (abs(yaw_error) < INNER_TOLERANCE) yaw_correcting = false;

        if (yaw_correcting) {
          if (abs(yaw_error) < 175.0 || yaw_dir == 0) {
            yaw_dir = (yaw_error > 0) ? 1 : 2;
          }
          // JAVÍTVA: yaw relék felcserélve, ugyanaz az ok, mint fentebb.
          if (yaw_dir == 1) { r2 = HIGH; } 
          else { r1 = HIGH; }
        } else {
          yaw_dir = 0;
        }

        float pitch_error = calc_pitch - c_pitch;
        if (abs(pitch_error) > TOLERANCE_PITCH) pitch_correcting = true;
        else if (abs(pitch_error) < INNER_TOLERANCE) pitch_correcting = false;

        if (pitch_correcting) {
          if (pitch_error > 0) { r3 = HIGH; } 
          else { r4 = HIGH; }
        }
      }
    }

    bool motors_running = (r1 == HIGH || r2 == HIGH || r3 == HIGH || r4 == HIGH);

    if (motors_running) {
      motors_were_running = true;
      offset_updated = false; 
    } else {
      if (motors_were_running) {
        motors_were_running = false;
        last_motor_stop_time = millis();
      }

      if (!offset_updated && (millis() - last_motor_stop_time > 1000)) {
        offset_updated = true; 
        
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        if (mag_accuracy >= 2) {
          float new_offset = mag_yaw - game_yaw;
          while (new_offset > 180.0) new_offset -= 360.0;
          while (new_offset < -180.0) new_offset += 360.0;
          
          float offset_diff = new_offset - dynamic_yaw_offset; 
          dynamic_yaw_offset = new_offset;

          if (isManual) {
            target_yaw += offset_diff;
            while (target_yaw >= 360.0) target_yaw -= 360.0;
            while (target_yaw < 0.0) target_yaw += 360.0;
          }
          
        }
        xSemaphoreGive(dataMutex);
      }
    }

    digitalWrite(relay1, r1);
    digitalWrite(relay2, r2);
    digitalWrite(relay3, r3);
    digitalWrite(relay4, r4);
    if (millis() - lastDebugPrint > 400) {
      lastDebugPrint = millis();
      
      // Kifejezetten a logoláshoz kiolvassuk az analóg pint (így AUTO módban is látod mit csinál)
      int current_adc_val = getAveragedADC(ANALOG_BUTTON, 16);
      
      // Hozzáadtuk a kiíratáshoz az " | ADC: %d" részt, és a current_adc_val változót
      Serial.printf("[INFO] Mód: %s | YAW: %05.1f -> CÉL: %05.1f | PITCH: %05.1f -> CÉL: %05.1f | MOT: %d%d%d%d | ADC: %d\n",
        isManual ? "MANUAL" : "AUTO",
        c_yaw, target_yaw,
        c_pitch, target_pitch,
        r1, r2, r3, r4,
        current_adc_val
      );
    }

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial1.begin(115200, SERIAL_8N1, data_RX, data_TX);

  pinMode(relay1, OUTPUT); pinMode(relay2, OUTPUT);
  pinMode(relay3, OUTPUT); pinMode(relay4, OUTPUT);
  digitalWrite(relay1, LOW); digitalWrite(relay2, LOW);
  digitalWrite(relay3, LOW); digitalWrite(relay4, LOW);

  pinMode(BNO_SCL,INPUT_PULLUP);
  pinMode(BNO_SDA ,INPUT_PULLUP);
  pinMode(MODE_SELECTER_BUTTON, INPUT_PULLDOWN);
  pinMode(ANALOG_BUTTON, INPUT);

  last_switch_state = (digitalRead(MODE_SELECTER_BUTTON) == HIGH);
  global_isManual = true; 

  strip.begin();
  strip.show();

  Wire.begin(BNO_SDA, BNO_SCL);
  Wire.setClock(400000);
  delay(500);

  bool bno_ok = false;
  for (int i = 0; i < 8; i++) {
    if (bno08x.begin_I2C(0x4A, &Wire)) {
      bno_ok = true;
      Serial.println("BNO08x sikeresen csatlakozott!");
      break;
    }
    Serial.println("BNO init hiba, ujraprobalkozas 500ms mulva...");
    delay(500);
  }
  if (!bno_ok) {
    Serial.println("Szar a bno (Nem talalhato az I2C cimen 8 probalkozas utan sem)!");
    for(int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, strip.Color(255, 0, 0)); // Váltsuk pirosra a ledet hiba esetén
    strip.show();
    
    while (1) { 
      delay(10);
    }
  }

  bno08x.enableReport(SH2_ROTATION_VECTOR);
  bno08x.enableReport(SH2_GAME_ROTATION_VECTOR);
  dataMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(TaskUART, "UART", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskSensor, "Sensor", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskControl, "Control", 4096, NULL, 1, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);
}