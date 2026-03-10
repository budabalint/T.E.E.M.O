#include <Arduino.h>
#include <LoRa_E220.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <settings.h>



Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
LoRa_E220 e220ttl(RX_PIN, TX_PIN, &Serial2, AUX_PIN, M0_PIN, M1_PIN, UART_BPS_RATE_9600);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

volatile unsigned long totalPackets = 0;
volatile unsigned long crcErrors = 0;
volatile int shared_rssi_dbm = -255;
volatile bool shared_hasSignal = false;
volatile unsigned long shared_lastSignalTime = 0;

TaskHandle_t LoRaTaskHandle;

uint8_t calculateCRC8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    uint8_t extract = *data++;
    for (uint8_t tempI = 8; tempI; tempI--) {
      uint8_t sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) {
        crc ^= 0x8C;
      }
      extract >>= 1;
    }
  }
  return crc;
}

void LoRaReaderTask(void * pvParameters) {
  for(;;) {
    while (e220ttl.available() >= 45) {
      
      shared_lastSignalTime = millis();
      shared_hasSignal = true;

      ResponseStructContainer rc = e220ttl.receiveMessageRSSI(44);

      if (rc.status.code == E220_SUCCESS) {
        totalPackets++;

        uint8_t *buffer = (uint8_t*) rc.data; 
        Serial.write(buffer, 44);
        Serial.write(rc.rssi);
        uint8_t receivedCRC = buffer[43]; 
        uint8_t calculatedCRC = calculateCRC8(buffer, 43);

        if(receivedCRC != calculatedCRC) {
          crcErrors++; 
        }

        shared_rssi_dbm = rc.rssi - 256;
      }
      rc.close(); 
    }
    vTaskDelay(1); 
  }
}

void setup() {
  Serial.begin(921600);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  digitalWrite(M0_PIN, HIGH);
  digitalWrite(M1_PIN, HIGH);

  delay(1000);

  e220ttl.begin();

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    while(true);
  }

  display.clearDisplay();
  display.setTextSize(2); 
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,20);
  display.print("Indulas...");
  display.display();

  delay(200);
  strip.begin();
  delay(500);
  strip.setPixelColor(6, 0,255,255);
  strip.show();
  delay(500);

  while(Serial2.available()) {
    Serial2.read();
  }

  e220ttl.begin();
  ResponseStructContainer c = e220ttl.getConfiguration();
  if (c.status.code == E220_SUCCESS) {
    Configuration configuration = *(Configuration*) c.data;
    configuration.ADDH = MY_ADDH;
    configuration.ADDL = MY_ADDL;
    configuration.CHAN = CHANNEL;
    configuration.SPED.airDataRate = AIR_DATA_RATE_111_625;
    configuration.SPED.uartBaudRate = UART_BPS_115200;
    configuration.SPED.uartParity = MODE_00_8N1;
    configuration.OPTION.transmissionPower = POWER_10; 
    configuration.OPTION.subPacketSetting = SPS_200_00;
    configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
    configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
    configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
    ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  }
  c.close();
  e220ttl.setMode(MODE_0_NORMAL);
  Serial2.begin(115200);

  shared_lastSignalTime = millis(); 
  xTaskCreatePinnedToCore(LoRaReaderTask, "LoRaReader", 10000, NULL, 3, &LoRaTaskHandle, 0);

}

void loop() {
  /**/
  static int lastNumLeds = -1;
  static uint32_t lastColor = 0;
  static unsigned long lastLoopDisplayPackets = 0; 
  const unsigned long signalTimeout = 3000;
  static bool wasSignalAlive = true; 
  bool isSignalAlive = (millis() - shared_lastSignalTime < signalTimeout);

  if (isSignalAlive) {
      if (!wasSignalAlive) {
          wasSignalAlive = true;
      }

      int rssi_dbm = shared_rssi_dbm;
      
      if (rssi_dbm >= -130) { 
        int numLedsOn = 1;
        uint32_t color = 0;
        int calc_rssi = constrain(rssi_dbm, -91, 0);
        
        if (calc_rssi <= -62) {
          color = strip.Color(255, 0, 0);
          numLedsOn = map(calc_rssi, -91, -62, 1, 8);
        } 
        else if (calc_rssi <= -32) {
          color = strip.Color(255, 255, 0);
          numLedsOn = map(calc_rssi, -61, -32, 1, 8);
        } 
        else {
          color = strip.Color(0, 255, 0);
          numLedsOn = map(calc_rssi, -31, 0, 1, 8);
        }
        numLedsOn = constrain(numLedsOn, 1, 8);

        if (numLedsOn != lastNumLeds || color != lastColor) {
          strip.clear();
          for (int i = 0; i < numLedsOn; i++) {
            strip.setPixelColor(i, color);
          }
          strip.show();
          lastNumLeds = numLedsOn; 
          lastColor = color;
        }

        if (totalPackets - lastLoopDisplayPackets >= 5) {
          display.clearDisplay();
          
          display.setTextSize(2);
          display.setCursor(0, 0);
          display.print("RSSI:");
          display.print(rssi_dbm);

          display.setTextSize(1);
          display.setCursor(0, 30);
          display.print("Csomagok : ");
          display.print(totalPackets);

          display.setCursor(0, 45);
          display.print("CRC Hiba : ");
          display.print(crcErrors);

          display.display();
          
          lastLoopDisplayPackets = totalPackets;
        }
      }
      
  } else {
    if (wasSignalAlive == true) {
        display.clearDisplay();
        display.setTextSize(2); 
        display.setCursor(0, 20);
        display.print("No Signal!");
        display.display();

        strip.clear();
        for (int i = 0; i < LED_COUNT; i++) {
          strip.setPixelColor(i, 255, 0, 0);
        }
        strip.show();

        lastNumLeds = -1;  
        lastColor = 0;
        wasSignalAlive = false; 
    }
  }
  delay(1);
}